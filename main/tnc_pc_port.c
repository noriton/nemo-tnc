#define TNC_PC_PORT_C
#include "tnc_pc_port.h"
#include "command_parser.h"
#include "esp_log.h"
#include "frame_metadata.h"
#include "kiss_parser.h" // メタデータ定義などを参照
#include "tnc_buffer.h"
#include "nvs_if.h"
#include <string.h>


void poll_and_display_rx_packets(tnc_port_info_t *port);
void pc_port_task(void *pvParameters);
void enqueue_ui_packet(tnc_port_info_t *port, uint8_t *payload, size_t len, char *dest_call);
static int check_escape_with_guard(tnc_port_info_t *port, const uint8_t *data,
                                   size_t size);

#define MAX_PORTS 2
tnc_port_info_t pc_ports[MAX_PORTS];

int master_console_port = 0;

static const char *TAG = "PC_PORT";
#define GUARD_TIME pdMS_TO_TICKS(1000)

// ポートごとに独立した8枠のMYCALLリスト
char mycall_list[MAX_PORTS][MAX_MYCALL_LIST][16] = {
    {"N0CALL-0", "N0CALL-0", "N0CALL-0", "N0CALL-0",
     "N0CALL-0", "N0CALL-0", "N0CALL-0", "N0CALL-0"}, // port0
    {"N0CALL-0", "N0CALL-0", "N0CALL-0", "N0CALL-0",
     "N0CALL-0", "N0CALL-0", "N0CALL-0", "N0CALL-0"}, // port1
};

/**
 * 現在のポートがマスターコンソールか判定する
 */
bool is_master_console(tnc_port_info_t *port)
{
    return (port->id == master_console_port);
}

/**
 * マスターコンソールに対してのみ、またはマスターコンソールを含めて
 * システム通知を送るヘルパー
 */
void system_notify(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        // マスターコンソールのポート情報を取得（管理配列 pc_ports 等から）
        tnc_port_info_t *master = &pc_ports[master_console_port];
        if (master->to_pc) {
            xRingbufferSend(master->to_pc, (uint8_t *)buf, len, 0);
        }
    }
}

void tnc_pc_ports_init(void)
{
    for (int i = 0; i < MAX_PORTS; i++) {
        // ポートごとの MYCALLリストを NVS から復元
        nvs_load_mycall_list(i, mycall_list[i]);
        pc_ports[i].id = i;
        //        pc_ports[i].mode = PORT_MODE_COMMAND; //
        //        初期状態はどちらもコマンドモード

        if (i == master_console_port) {
            // マスターコンソールポートは常にコマンドモードで起動
            pc_ports[i].mode = PORT_MODE_COMMAND;
        } else {
            // その他のポートは NVS から前回のモードを復元（未保存なら COMMAND）
            nvs_load_port_mode((int *)&pc_ports[i].mode, i, PORT_MODE_COMMAND);
        }

        // ループバックや相互接続のペアリング設定
        if (i == MAX_PORTS - 1 && (i % 2 == 0)) {
            // 1. 自分が最後の要素、かつ、ペアになる相手（i+1）が範囲外になる場合
            pc_ports[i].peer_id = i; // ループバック
        } else {
            // 2. それ以外はペアになる相手を指定
            pc_ports[i].peer_id = (i % 2 == 0) ? i + 1 : i - 1;
        }

        // 受信バッファは ひとまずUSB受信バッファ (usb_from_pc) を参照する
        // これで usb_descriptors.c が usb_from_pc に入れたデータをここで吸い出せる
        // 送信バッファは to_pc を参照する形で統一 (usb_to_pc と同じものを指す)
        // 将来的にはモードによっては別のIF（シリアルなど）を使う可能性もあるため、
        // ここで切り替えられるようにする

        pc_ports[i].from_pc = usb_from_pc[i];
        pc_ports[i].to_pc = usb_to_pc[i]; // PCへの送信(戻り）用リングバッファ
        pc_ports[i].send_tx = tx_ringbuf[i]; // ToDo：リネーム検討
        pc_ports[i].recv_rx = rx_ringbuf[i]; // ToDo：リネーム検討

        pc_ports[i].line_pos = 0;

        // ポートのMYCALLインデックスをNVSから復元（未保存ならポートIDをデフォルトに）
        nvs_load_port_mycall_idx(i, &pc_ports[i].mycall_idx, i);

        // コマンドヒストリをNVSから復元　ここも関数に切り出してもいいかも
        pc_ports[i].hist_head    = HIST_NIL;
        pc_ports[i].hist_wp      = 0;
        pc_ports[i].hist_count   = 0;
        pc_ports[i].hist_pos_off = -1;
        pc_ports[i].hist_enabled = 1;
        memset(pc_ports[i].hist_pool, 0, HIST_POOL_SIZE);
        {
            // 注意: char hist_cmds[32][256] = 8KBでは
            // スタック確保となりapp_mainスタックをオーバーフローさせる。
            // staticにしてBSSセグメントに配置すること。
            static char hist_cmds[32][256];
            int  hist_loaded = 0;
            nvs_load_history(i, hist_cmds, &hist_loaded);
            for (int j = 0; j < hist_loaded; j++) {
                hist_push_raw(&pc_ports[i], hist_cmds[j]);
            }
        }

        kiss_init(&(pc_ports[i].kiss_ctx)); // KISSパーサのコンテキスト初期化
        // タスク起動
        char task_name[16];
        snprintf(task_name, sizeof(task_name), "pc_port_%d", i);
        xTaskCreate(pc_port_task, task_name, 4096, &pc_ports[i], 5, NULL);
    }
}

/**
 * トランスペアレントモードのバッファをフラッシュし、
 * メタデータを付与して送信キュー(No-Split RB)へ投入する
 */
static void flush_transport_buffer(tnc_port_info_t *port)
{
    if (port->trans_len == 0) {
        return;
    }

    // 1. 必要サイズの計算
    size_t header_len = sizeof(tnc_meta_header_t);
    size_t payload_len = port->trans_len;
    size_t total_len = header_len + payload_len;

    // 2. 一時送信バッファの確保 (スタック利用)
    // TNC_PACLEN + ヘッダサイズ (約512+32バイト)
    uint8_t send_tmp[TNC_PACLEN + 64];

    if (total_len <= sizeof(send_tmp)) {
        tnc_meta_header_t *meta = (tnc_meta_header_t *)send_tmp;

        // 3. メタデータヘッダの構築
        meta->version = TNC_META_VERSION_1;
        meta->type = META_TYPE_DATA_UI; // トランスペアレントはUIフレーム扱い
        meta->header_len = (uint16_t)header_len;
        meta->payload_len = (uint16_t)payload_len;
        meta->port_id = (uint8_t)port->id;
        meta->reserved = 0;

        // 4. ペイロードをヘッダの直後にコピー
        memcpy(send_tmp + header_len, port->trans_buf, payload_len);

        // 5. No-Split リングバッファ (ax25_packet_queue) へ投入
        // タイムアウトを設けて、バッファが一時的にいっぱいの時でも少し待機させる
        BaseType_t res =
            xRingbufferSend(port->send_tx, send_tmp, total_len, pdMS_TO_TICKS(20));

        if (res != pdTRUE) {
            // バッファフル時のエラー処理 (必要に応じてログ出力や統計カウンタ加算)
            // ESP_LOGW("TNC", "TX Queue Full - Packet dropped");
        }
    }

    // 6. 送信完了(または破棄)したのでバッファをクリア
    port->trans_len = 0;
}

void run_mode_command(tnc_port_info_t *port)
{
    while (port->mode == PORT_MODE_COMMAND) {
        size_t size;
        uint8_t *data =
            (uint8_t *)xRingbufferReceive(port->from_pc, &size, pdMS_TO_TICKS(100));

        if (data != NULL) {
            process_command_input(port, data, size);
            vRingbufferReturnItem(port->from_pc, (void *)data);
        }
    }
}

static void run_mode_uichat(tnc_port_info_t *port)
{
    const char *prompt = "\r\n(UICHAT)> ";
    xRingbufferSend(port->to_pc, (uint8_t *)prompt, strlen(prompt), 0);

    port->line_pos = 0;
    port->esc_state = ESC_STATE_IDLE;
    port->last_rx_tick = xTaskGetTickCount();

    while (port->mode == PORT_MODE_UICHAT) {
        size_t size;
        uint8_t *data =
            (uint8_t *)xRingbufferReceive(port->from_pc, &size, pdMS_TO_TICKS(10));
        TickType_t now = xTaskGetTickCount();

        // 1. ガードタイム付きエスケープ判定
        if (check_escape_with_guard(port, data, size)) {
            port->mode = PORT_MODE_COMMAND;
            xRingbufferSend(port->to_pc, (uint8_t *)"\r\nOK\r\n", 6, 0);
            send_prompt(port);

            if (data != NULL) {
                vRingbufferReturnItem(port->from_pc, (void *)data);
            }
            break;
        }

        if (data != NULL) {
            port->last_rx_tick = now;

            for (size_t i = 0; i < size; i++) {
                uint8_t c = data[i];

                // エコーバック (PC画面への返し)
                xRingbufferSend(port->to_pc, &c, 1, 0);

                // 改行判定
                if (c == '\r' || c == '\n') {
                    if (port->line_pos > 0) {
                        // ★メタデータ付きパケット送信
                        // (トランスポートと共通のロジックを流用可能)
                        enqueue_ui_packet(port, (uint8_t *)port->line_buf, port->line_pos, NULL); // TODO: 宛先call情報追加必要

                        // 送信後の改行とプロンプト
                        xRingbufferSend(port->to_pc, (uint8_t *)"\r\n(UICHAT)> ", 13, 0);
                        port->line_pos = 0;
                    }
                } else if (c >= 0x20 && c <= 0x7E) {
                    // 表示可能文字のみバッファへ
                    if (port->line_pos < sizeof(port->line_buf) - 1) {
                        port->line_buf[port->line_pos++] = c;
                    }
                }
            }
            vRingbufferReturnItem(port->from_pc, (void *)data);
        } else {
            // 受信パケットのポーリング処理（ダミー）
            // poll_and_display_rx_packets(port);
        }
    }
}

#define NOT_USED
#ifndef NOT_USED
void run_mode_transport(tnc_port_info_t *port)
{
    port->trans_len = 0;
    port->esc_state = ESC_STATE_IDLE; // エスケープ判定状態を初期化
    port->last_rx_tick = xTaskGetTickCount();

    while (port->mode == PORT_MODE_TRANSPORT) {
        size_t size;
        // PACTIMEやガードタイム監視のため、10msでタイムアウトさせる
        uint8_t *data =
            (uint8_t *)xRingbufferReceive(port->from_pc, &size, pdMS_TO_TICKS(10));
        TickType_t now = xTaskGetTickCount();

        // 1. エスケープシーケンス判定 (データの有無に関わらず毎回呼ぶ)
        if (check_escape_with_guard(port, data, size)) {
            flush_transport_buffer(port); // 残っているデータをパケット化して送信
            port->mode = PORT_MODE_COMMAND;
            xRingbufferSend(port->to_pc, (uint8_t *)"\r\nOK\r\n", 6, 0);
            send_prompt(port);

            if (data != NULL) {
                vRingbufferReturnItem(port->from_pc, (void *)data);
            }
            break; // whileループを抜けてメインのディスパッチャに戻る
        }

        if (data != NULL) {
            // 2. 受信データがある場合の通常処理

            port->last_rx_tick = now; // 最終受信時刻を更新

            for (size_t i = 0; i < size; i++) {
                // バッファに格納
                if (port->trans_len < TNC_PACLEN) {
                    port->trans_buf[port->trans_len++] = data[i];
                }

                // [条件1] PACLEN到達による送信
                if (port->trans_len >= TNC_PACLEN) {
                    flush_transport_buffer(port);
                }
            }
            vRingbufferReturnItem(port->from_pc, (void *)data);
        } else {
            // 3. データがない（タイムアウト）場合のPACTIMEチェック
            if (port->trans_len > 0) {
                // [条件2] PACTIME（無通信時間）経過による送信
                if ((now - port->last_rx_tick) > pdMS_TO_TICKS(TNC_PACTIME)) {
                    flush_transport_buffer(port);
                }
            }
        }
    }
}
#endif

void run_mode_kiss(tnc_port_info_t *port)
{
    kiss_context_t kiss_ctx;
    kiss_init(&kiss_ctx);

    while (port->mode == PORT_MODE_KISS) {
        // KISSストリームを受信して解析、AX.25送信処理へ
        // KISSはバイトストリームなので、受信データをそのまま kiss_process_stream
        // に渡す KISSモードからの離脱はマスターポートのコマンドコンソールから行う。
        size_t size;
        uint8_t *data =
            (uint8_t *)xRingbufferReceive(port->from_pc, &size, pdMS_TO_TICKS(100));

        if (data != NULL) {
            // KISSストリームを解析してAX.25送信処理へ
            kiss_process_stream(&kiss_ctx, data, size, port->id, port->send_tx);
            vRingbufferReturnItem(port->from_pc, (void *)data);
        }
    }
}

void run_mode_loopback(tnc_port_info_t *port)
{
    int peer_id = (port->id == 0) ? 1 : 0;

    while (port->mode == PORT_MODE_LOOPBACK) {
        size_t size;
        uint8_t *data =
            (uint8_t *)xRingbufferReceive(port->from_pc, &size, pdMS_TO_TICKS(100));

        if (data != NULL) {
            // 受信したデータをecho-backする
            xRingbufferSend(port->to_pc, data, size, 0);
            // peerportが自分自身でなければ、peerポートのto_pcへ送信も行う
            if (peer_id != port->id) {
                xRingbufferSend(pc_ports[peer_id].to_pc, data, size, 0);
            }
            // 送信後は受信バッファを返却
            vRingbufferReturnItem(port->from_pc, (void *)data);
        }
    }
}

#define GUARD_TIME pdMS_TO_TICKS(1000) // 1000ms

/**
 * ガードタイム付きエスケープシーケンス判定
 * @return 1: モード脱出確定, 0: 継続
 */
static int check_escape_with_guard(tnc_port_info_t *port, const uint8_t *data,
                                   size_t size)
{
    TickType_t now = xTaskGetTickCount();
    TickType_t interval = now - port->last_rx_tick;

    // 1. データを受信した場合の判定
    if (data != NULL && size > 0) {
        // 前回の受信から1秒以上経っていれば READY 状態へ
        if (interval >= GUARD_TIME) {
            port->esc_state = ESC_STATE_READY;
        }

        // READY状態で、かつ届いたデータが正確に "+++" のみであるかチェック
        if (port->esc_state == ESC_STATE_READY && size == 3 && data[0] == '+' &&
            data[1] == '+' && data[2] == '+') {
            port->esc_state = ESC_STATE_DETECTED;
            // 受信時刻を更新して、ここからさらに1秒待機
            port->last_rx_tick = now;
            return 0;
        }

        // それ以外のデータが来たら IDLE に戻る（シーケンス失敗）
        port->esc_state = ESC_STATE_IDLE;
        port->last_rx_tick = now;
        return 0;
    } else {
        // 2. データを受信していない（タイムアウト時）の判定

        // "+++" 受信後に1秒以上無音が続けば、脱出成功
        if (port->esc_state == ESC_STATE_DETECTED && interval >= GUARD_TIME) {
            port->esc_state = ESC_STATE_IDLE; // リセット
            return 1;
        }
    }

    return 0;
}

#ifndef NOT_USED
/**
 * ガードタイム付きエスケープシーケンス判定
 */
static int check_escape_with_guard(tnc_port_info_t *port, const uint8_t *data,
                                   size_t size)
{
    TickType_t now = xTaskGetTickCount();
    TickType_t interval = now - port->last_rx_tick;

    if (data != NULL && size > 0) {
        if (interval >= GUARD_TIME) {
            port->esc_state = ESC_STATE_READY;
        }

        if (port->esc_state == ESC_STATE_READY && size == 3 && data[0] == '+' &&
            data[1] == '+' && data[2] == '+') {
            port->esc_state = ESC_STATE_DETECTED;
            port->last_rx_tick = now;
            return 0;
        }

        port->esc_state = ESC_STATE_IDLE;
        port->last_rx_tick = now;
        return 0;
    } else {
        if (port->esc_state == ESC_STATE_DETECTED && interval >= GUARD_TIME) {
            port->esc_state = ESC_STATE_IDLE;
            return 1;
        }
    }
    return 0;
}
#endif

/**
 * トランスポート（データ）モードの実行ループ
 */
void run_mode_transport(tnc_port_info_t *port)
{
    port->trans_len = 0;
    port->esc_state = ESC_STATE_IDLE;
    port->last_rx_tick = xTaskGetTickCount();

    ESP_LOGI(TAG, "Port %d: Entered TRANSPORT mode", port->id);

    while (port->mode == PORT_MODE_TRANSPORT) {
        size_t size;
        uint8_t *data =
            (uint8_t *)xRingbufferReceive(port->from_pc, &size, pdMS_TO_TICKS(10));
        TickType_t now = xTaskGetTickCount();

        // エスケープ判定
        if (check_escape_with_guard(port, data, size)) {
            port->mode = PORT_MODE_COMMAND;
            xRingbufferSend(port->to_pc, (uint8_t *)"\r\nOK\r\n", 6, 0);
            send_prompt(port);
            if (data != NULL) {
                vRingbufferReturnItem(port->from_pc, (void *)data);
            }
            break;
        }

        if (data != NULL) {
            port->last_rx_tick = now;
            for (size_t i = 0; i < size; i++) {
                if (port->trans_len < TNC_PACLEN) {
                    port->trans_buf[port->trans_len++] = data[i];
                }
                if (port->trans_len >= TNC_PACLEN) {
                    enqueue_ui_packet(port, port->trans_buf, port->trans_len, NULL); // TODO: 宛先call情報追加必要
                    port->trans_len = 0;
                }
            }
            vRingbufferReturnItem(port->from_pc, (void *)data);
        } else {
            if (port->trans_len > 0) {
                if ((now - port->last_rx_tick) > pdMS_TO_TICKS(TNC_PACTIME)) {
                    enqueue_ui_packet(port, port->trans_buf, port->trans_len, NULL); // TODO: 宛先call情報追加必要
                    port->trans_len = 0;
                }
            }
        }
    }
}

void poll_and_display_rx_packets(tnc_port_info_t *port)
{
    // 受信キューを監視し、自局宛のUIフレームがあれば to_pc へ送る
    // ここではダミー実装として、実際の受信処理は省略
    // 将来的には、rx_ringbuf などから受信フレームを取り出し、to_pc
    // へ送る処理を実装する
}

/**
 * UIフレーム（UICHAT / TRANSPORT）用の共通パケット投入関数
 */
void enqueue_ui_packet(tnc_port_info_t *port, uint8_t *payload, size_t len, char *dest_call)
{
    size_t header_len = sizeof(tnc_meta_header_t);
    size_t total_len = header_len + len;

    // スタックバッファ
    uint8_t send_tmp[sizeof(tnc_meta_header_t) + KISS_MAX_RAW_LEN];
    if (total_len > sizeof(send_tmp))
        return;

    tnc_meta_header_t *meta = (tnc_meta_header_t *)send_tmp;
    // 未使用フィールド（digi等）を含め全てゼロ初期化
    memset(meta, 0, sizeof(tnc_meta_header_t));
    meta->version    = TNC_META_VERSION_1;
    meta->type       = META_TYPE_DATA_UI; // UIフレーム種別
    meta->header_len = (uint16_t)header_len;
    meta->payload_len = (uint16_t)len;
    meta->port_id    = (uint8_t)port->id;

    if (dest_call != NULL) {
        strncpy(meta->dest_call, dest_call, sizeof(meta->dest_call) - 1);
        meta->dest_call[sizeof(meta->dest_call) - 1] = '\0';
    } else {
        meta->dest_call[0] = '\0';
    }

    // src_call: 現在選択中のMYCALL
    const char *mycall = mycall_list[port->id][port->mycall_idx];
    strncpy(meta->src_call, mycall, sizeof(meta->src_call) - 1);
    meta->src_call[sizeof(meta->src_call) - 1] = '\0';

    memcpy(send_tmp + header_len, payload, len);

    // No-Split リングバッファへ
    xRingbufferSend(port->send_tx, send_tmp, total_len, pdMS_TO_TICKS(10));
}

void pc_port_task(void *pvParameters)
{
    tnc_port_info_t *port = (tnc_port_info_t *)pvParameters;

    // 現在のタスクのTLSにポート情報を保存 (Index 0)
    vTaskSetThreadLocalStoragePointer(NULL, 0, port);

    for (;;) {
        switch (port->mode) {
        case PORT_MODE_COMMAND:
            run_mode_command(port);
            break;

        case PORT_MODE_TRANSPORT:
            run_mode_transport(port);
            break;

        case PORT_MODE_KISS:
            run_mode_kiss(port);
            break;

        case PORT_MODE_LOOPBACK:
            run_mode_loopback(port);
            break;

        case PORT_MODE_UICHAT:
            run_mode_uichat(port);
            break;

        default:
            // 未実装のモードは強制的にコマンドモードへ
            port->mode = PORT_MODE_COMMAND;
            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        }
    }
}
