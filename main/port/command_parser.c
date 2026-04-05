#include "command_parser.h"
#include "ax25.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_if.h"
#include "tx_frame.h"
#include "indicator.h"
#include "callsign.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ヒストリNVS保存の間引き制御（ポートごと）
#define HIST_SAVE_INTERVAL_US  (3LL * 60 * 1000000LL) // 3分
static int     hist_dirty[2]         = {0, 0};
static int64_t hist_last_save_us[2]  = {0, 0};

// エントリアクセスマクロ
// pool[p+0]: PREV = go_older方向, pool[p+1]: NEXT = go_newer方向
// pool[p+2..]: NUL終端コマンド文字列
#define H_PREV(pool, p)  ((pool)[(p)])
#define H_NEXT(pool, p)  ((pool)[(p)+1])
#define H_CMD(pool, p)   ((char*)&(pool)[(p)+2])

static int hist_entry_len(uint8_t *pool, uint8_t p) {
    return 2 + (int)strlen(H_CMD(pool, p)) + 1;
}

// メモリ上の最新ヒストリを NVS にコミットする（dirty な場合のみ書き込む）
void history_commit(tnc_port_info_t *port)
{
    if (!hist_dirty[port->id]) return;
    nvs_save_history(port->id, port->hist_pool,
                     port->hist_head, port->hist_count);
    hist_dirty[port->id]        = 0;
    hist_last_save_us[port->id] = esp_timer_get_time();
}



static void register_commands(void);
static int cmd_help(int argc, char **argv);

static SemaphoreHandle_t console_mutex = NULL;

static const char *TAG = "CMD_PARSER";

// --- 各コマンドの実装関数 ---

// --- コマンド応答用ヘルパー ---
static void cmd_response(const char *fmt, ...)
{
    // 現在実行中のタスクの TLS からポート情報を取得
    tnc_port_info_t *port =
        (tnc_port_info_t *)pvTaskGetThreadLocalStoragePointer(NULL, 0);

    if (port != NULL) {
        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        // ポートの送信バッファへ書き込み（バッファフル時は最大100ms待機してドロップを防ぐ）
        xRingbufferSend(port->to_pc, (uint8_t *)buf, strlen(buf), pdMS_TO_TICKS(100));
    }
}

// VERSION コマンド (修正版)
static int cmd_version(int argc, char **argv)
{
    cmd_response("\r\nNEMO-TNC v0.1 (esp_console)\r\n");
    return 0;
}

// UIMODE コマンド (修正版)
static int cmd_uimode(int argc, char **argv)
{
    cmd_response("\r\nNEMO-TNC v0.1 (esp_console)\r\n");
    return 0;
}

// --- MYCALL コマンドで参照する外部変数 ---
extern char mycall_list[2][MAX_MYCALL_LIST][16];
extern tnc_port_info_t pc_ports[];

#define MYCALL_DEFAULT "N0CALL-0"

/*
 * MYCALL の set 処理共通ヘルパー
 *   callsign_arg : コールサイン文字列（正規化・バリデーション前）
 *   slot_arg     : スロット指定文字列（NULL または数字文字列 または "-"）
 */
static int mycall_do_set(tnc_port_info_t *port, const char *callsign_arg, const char *slot_arg)
{
    char callsign_buf[CALLSIGN_BUFSIZE];
    strncpy(callsign_buf, callsign_arg, sizeof(callsign_buf) - 1);
    callsign_buf[sizeof(callsign_buf) - 1] = '\0';
    callsign_normalize(callsign_buf);
    if (!callsign_validate(callsign_buf)) {
        cmd_response("Error: Invalid callsign: %s\r\n", callsign_arg);
        return 1;
    }

    int slot;
    if (slot_arg == NULL) {
        slot = port->mycall_idx;
    } else if (strcmp(slot_arg, "-") == 0) {
        slot = -1;
        for (int i = 0; i < MAX_MYCALL_LIST; i++) {
            if (strcmp(mycall_list[port->id][i], MYCALL_DEFAULT) == 0) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            cmd_response("Error: No empty slot available.\r\n");
            return 1;
        }
        port->mycall_idx = slot;
        nvs_save_port_mycall_idx(port->id, slot);
    } else {
        slot = atoi(slot_arg);
        if (slot < 0 || slot >= MAX_MYCALL_LIST) {
            cmd_response("Error: Slot must be 0-%d.\r\n", MAX_MYCALL_LIST - 1);
            return 1;
        }
        port->mycall_idx = slot;
        nvs_save_port_mycall_idx(port->id, slot);
    }

    strncpy(mycall_list[port->id][slot], callsign_buf, sizeof(mycall_list[port->id][slot]) - 1);
    mycall_list[port->id][slot][sizeof(mycall_list[port->id][slot]) - 1] = '\0';
    nvs_save_mycall_list_item(port->id, slot, mycall_list[port->id][slot]);
    cmd_response("Saved [%d]: %s\r\n", slot, mycall_list[port->id][slot]);
    return 0;
}

// MYCALL コマンド (サブコマンド: list, set, use, del)
static int cmd_mycall(int argc, char **argv)
{
    tnc_port_info_t *port =
        (tnc_port_info_t *)pvTaskGetThreadLocalStoragePointer(NULL, 0);
    if (port == NULL)
        return 1;

    // 引数なし: 現在のポートに割り当てられている MYCALL を表示
    if (argc < 2) {
        int idx = port->mycall_idx;
        cmd_response("\r\nMYCALL[%d]: %s\r\n", idx, mycall_list[port->id][idx]);
        return 0;
    }

    // --- list サブコマンド ---
    if (strcasecmp(argv[1],"list") == 0) {
        cmd_response("\r\nPort %d -> slot %d (%s)\r\n", port->id, port->mycall_idx,
                     mycall_list[port->id][port->mycall_idx]);
        for (int i = 0; i < MAX_MYCALL_LIST; i++) {
            cmd_response("  [%d] %s\r\n", i, mycall_list[port->id][i]);
        }
        return 0;
    }

    // --- set サブコマンド ---
    if (strcasecmp(argv[1],"set") == 0) {
        if (argc < 3) {
            cmd_response("Usage: mycall set <CALLSIGN> [0-7|-]\r\n");
            return 1;
        }
        return mycall_do_set(port, argv[2], argc >= 4 ? argv[3] : NULL);
    }

    // --- use サブコマンド ---
    if (strcasecmp(argv[1],"use") == 0) {
        if (argc < 3) {
            cmd_response("Usage: mycall use <0-7>\r\n");
            return 1;
        }
        int slot = atoi(argv[2]);
        if (slot < 0 || slot >= MAX_MYCALL_LIST) {
            cmd_response("Error: Slot must be 0-%d.\r\n", MAX_MYCALL_LIST - 1);
            return 1;
        }
        port->mycall_idx = slot;
        nvs_save_port_mycall_idx(port->id, slot);
        cmd_response("Port %d -> slot %d (%s)\r\n", port->id, slot,
                     mycall_list[port->id][slot]);
        return 0;
    }

    // --- del サブコマンド ---
    if (strcasecmp(argv[1],"del") == 0) {
        if (argc < 3) {
            cmd_response("Usage: mycall del <0-7>\r\n");
            return 1;
        }
        int slot = atoi(argv[2]);
        if (slot < 0 || slot >= MAX_MYCALL_LIST) {
            cmd_response("Error: Slot must be 0-%d.\r\n", MAX_MYCALL_LIST - 1);
            return 1;
        }
        strncpy(mycall_list[port->id][slot], MYCALL_DEFAULT, sizeof(mycall_list[port->id][slot]) - 1);
        mycall_list[port->id][slot][sizeof(mycall_list[port->id][slot]) - 1] = '\0';
        nvs_save_mycall_list_item(port->id, slot, mycall_list[port->id][slot]);
        cmd_response("Deleted [%d]\r\n", slot);
        return 0;
    }

    // --- help サブコマンド ---
    if (strcasecmp(argv[1],"help") == 0) {
        cmd_response("\r\nmycall subcommands:\r\n");
        cmd_response("  (none)               Show current MYCALL\r\n");
        cmd_response("  help                 Show this help\r\n");
        cmd_response("  list                 List all MYCALL slots\r\n");
        cmd_response("  set <CALL> [0-7|-]   Save callsign to slot\r\n");
        cmd_response("  use <0-7>            Switch active slot\r\n");
        cmd_response("  del <0-7>            Reset slot to default\r\n");
        cmd_response("  <CALL> [0-7|-]       Shorthand for set\r\n");
        return 0;
    }

    // --- 暗黙のset: argv[1] が有効なコールサインならsetとして動作 ---
    {
        char tmp[CALLSIGN_BUFSIZE];
        strncpy(tmp, argv[1], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        callsign_normalize(tmp);
        if (callsign_validate(tmp)) {
            return mycall_do_set(port, argv[1], argc >= 3 ? argv[2] : NULL);
        }
    }

    cmd_response("Unknown subcommand: %s\r\n", argv[1]);
    return 1;
}

// LED コマンド (on/off)
static int cmd_led(int argc, char **argv)
{
    if (argc < 2) {
        cmd_response("Usage: led <on|off>\r\n");
        return 1;
    }
    if (strcasecmp(argv[1],"off") == 0) {
        indicator_set_forced_off(1);
        cmd_response("LED off\r\n");
    } else if (strcasecmp(argv[1],"on") == 0) {
        indicator_set_forced_off(0);
        cmd_response("LED on\r\n");
    } else {
        cmd_response("Usage: led <on|off>\r\n");
        return 1;
    }
    return 0;
}

// TESTTX コマンド (tx_frame経由で送信)
static int cmd_testtx(int argc, char **argv)
{
    char my_call[16] = {0};
    nvs_load_mycall(my_call, sizeof(my_call));

    ax25_address_t addr = {.dest_call = "CQ    ",
                           .dest_ssid = 0,
                           .src_call = my_call,
                           .src_ssid = 0};

    uint8_t frame[300];
    const char *msg = (argc > 1) ? argv[1] : "HELLO";

    // 平文の表示 (Input Portへフィードバック)
    char plain_msg[64];
    int p_len =
        snprintf(plain_msg, sizeof(plain_msg), "\r\nSending: %s\r\n", msg);
    // 平文の表示 (Input Portへフィードバック)
    cmd_response("\r\nSending: %s\r\n", msg);

    size_t len = ax25_build_ui_frame(&addr, (uint8_t *)msg, strlen(msg), frame);

    // echoへエンキュー
    uint8_t portnum = 0; // 暫定
    if (tx_frame_enqueue(frame, len, portnum) == ESP_OK) {
        cmd_response("Queued to TX Buffer.\r\n");
    } else {
        cmd_response("Failed to Queue.\r\n");
    }

    return 0;
}

/**
 * uisend コマンドの実装
 * 形式: uisend <DEST_CALL> <MESSAGE...>
 */
int cmd_uisend(int argc, char **argv)
{
    if (argc < 3) {
        cmd_response("Usage: uisend <DEST_CALL> <MESSAGE>\r\n");
        return 1;
    }

    // 引数の取得
    const char *dest_call = argv[1];

    // DEST_CALLのバリデーション（CQ/BEACON等の特殊アドレスも通す）
    if (!ax25_validate_dest(dest_call)) {
        cmd_response("Error: Invalid callsign: %s\r\n", dest_call);
        return 1;
    }
    uint8_t dest_ax25[7];
    ax25_encode_dest(dest_ax25, dest_call, false);

    // メッセージ部分はスペース区切りの全引数を結合するか、
    // 簡易的に argv[2] 以降を連結する（ここでは argv[2] 以降を使用）
    char message[256] = {0};
    for (int i = 2; i < argc; i++) {
        strcat(message, argv[i]);
        if (i < argc - 1)
            strcat(message, " ");
    }

    size_t payload_len = strlen(message);

    // 現在実行中のタスクから TLS 経由でポート情報を取得
    tnc_port_info_t *port =
        (tnc_port_info_t *)pvTaskGetThreadLocalStoragePointer(NULL, 0);
    if (port == NULL)
        return 1;

    enqueue_ui_packet(port, (uint8_t *)message, payload_len, (char *)dest_call);

    return 0;
}

// --- コマンドヒストリ操作 ---

// 最古エントリを循環リストから切り離す
static void hist_evict_oldest(tnc_port_info_t *port)
{
    if (port->hist_count == 0) return;
    uint8_t *pool = port->hist_pool;

    if (port->hist_count == 1) {
        port->hist_head  = HIST_NIL;
        port->hist_count = 0;
        port->hist_wp    = 0;
        return;
    }

    uint8_t oldest     = H_NEXT(pool, port->hist_head);
    uint8_t new_oldest = H_NEXT(pool, oldest);
    H_NEXT(pool, port->hist_head) = new_oldest;
    H_PREV(pool, new_oldest)      = port->hist_head;

    if (port->hist_pos_off == (int)oldest)
        port->hist_pos_off = (int)new_oldest;

    port->hist_count--;
}

// プールを oldest→newest 順に先頭から詰め直す
static void hist_compact(tnc_port_info_t *port)
{
    if (port->hist_count == 0) { port->hist_wp = 0; return; }

    uint8_t new_pool[HIST_POOL_SIZE];
    uint8_t old_off[64], new_off[64];
    int n = 0, wp = 0;

    uint8_t p = H_NEXT(port->hist_pool, port->hist_head); // oldest
    for (int i = 0; i < port->hist_count; i++) {
        old_off[n] = p;
        new_off[n] = (uint8_t)wp;
        n++;
        const char *s = H_CMD(port->hist_pool, p);
        int slen = (int)strlen(s) + 1;
        memcpy(&new_pool[wp + 2], s, slen);
        wp += 2 + slen;
        p = H_NEXT(port->hist_pool, p);
    }
    // 循環ポインタ再構築 (i=0が最古、i=n-1が最新)
    for (int i = 0; i < n; i++) {
        new_pool[new_off[i]]     = new_off[(i - 1 + n) % n]; // PREV=older
        new_pool[new_off[i] + 1] = new_off[(i + 1) % n];     // NEXT=newer
    }
    memcpy(port->hist_pool, new_pool, HIST_POOL_SIZE);
    port->hist_head = new_off[n - 1]; // newest
    port->hist_wp   = (uint8_t)wp;

    if (port->hist_pos_off >= 0) {
        for (int i = 0; i < n; i++) {
            if (old_off[i] == (uint8_t)port->hist_pos_off) {
                port->hist_pos_off = new_off[i]; break;
            }
        }
    }
}

// needed バイトの書き込み領域を確保してオフセットを返す
static uint8_t hist_pool_alloc(tnc_port_info_t *port, int needed)
{
    if ((int)port->hist_wp + needed > HIST_POOL_SIZE)
        hist_compact(port);

    while (port->hist_count > 0) {
        uint8_t oldest  = H_NEXT(port->hist_pool, port->hist_head);
        int o_start = (int)oldest;
        int o_end   = o_start + hist_entry_len(port->hist_pool, oldest);
        int n_start = (int)port->hist_wp;
        int n_end   = n_start + needed;
        if (n_start < o_end && n_end > o_start)
            hist_evict_oldest(port);
        else
            break;
    }
    uint8_t off = port->hist_wp;
    port->hist_wp += (uint8_t)needed;
    return off;
}

// NVSロード用（dirty/save制御なし）
void hist_push_raw(tnc_port_info_t *port, const char *cmd)
{
    if (cmd[0] == '\0') return;
    int needed = 2 + (int)strlen(cmd) + 1;
    uint8_t off = hist_pool_alloc(port, needed);
    uint8_t *pool = port->hist_pool;
    strcpy(H_CMD(pool, off), cmd);
    if (port->hist_count == 0) {
        H_PREV(pool, off) = off; H_NEXT(pool, off) = off;
        port->hist_head = off;
    } else {
        uint8_t oldest = H_NEXT(pool, port->hist_head);
        H_PREV(pool, off) = port->hist_head;
        H_NEXT(pool, off) = oldest;
        H_NEXT(pool, port->hist_head) = off;
        H_PREV(pool, oldest) = off;
        port->hist_head = off;
    }
    port->hist_count++;
}

// コマンドをヒストリに追加（重複は最新に移動、記録停止中は無視）
static void hist_push(tnc_port_info_t *port, const char *cmd)
{
    if (cmd[0] == '\0') return;
    if (!port->hist_enabled) return;

    uint8_t *pool = port->hist_pool;

    // 重複検索（oldest→newest 方向）
    if (port->hist_count > 0) {
        uint8_t p = H_NEXT(pool, port->hist_head); // oldest
        for (int i = 0; i < port->hist_count; i++) {
            if (strcmp(H_CMD(pool, p), cmd) == 0) {
                if (p != port->hist_head) {
                    uint8_t go_older = H_PREV(pool, p);
                    uint8_t go_newer = H_NEXT(pool, p);
                    H_NEXT(pool, go_older) = go_newer;
                    H_PREV(pool, go_newer) = go_older;
                    uint8_t oldest = H_NEXT(pool, port->hist_head);
                    H_PREV(pool, p) = port->hist_head;
                    H_NEXT(pool, p) = oldest;
                    H_NEXT(pool, port->hist_head) = p;
                    H_PREV(pool, oldest) = p;
                    port->hist_head = p;
                }
                goto mark_dirty;
            }
            p = H_NEXT(pool, p);
        }
    }

    hist_push_raw(port, cmd);

mark_dirty:
    hist_dirty[port->id] = 1;
    int64_t now = esp_timer_get_time();
    if ((now - hist_last_save_us[port->id]) >= HIST_SAVE_INTERVAL_US)
        history_commit(port);
}

// 現在の入力行を消去して str を表示
static void hist_redraw(tnc_port_info_t *port, const char *str)
{
    for (int i = 0; i < port->line_pos; i++) {
        xRingbufferSend(port->to_pc, (uint8_t *)"\b \b", 3, 0);
    }
    size_t len = strlen(str);
    if (len >= sizeof(port->line_buf)) len = sizeof(port->line_buf) - 1;
    memcpy(port->line_buf, str, len);
    port->line_buf[len] = '\0';
    port->line_pos = (int)len;
    xRingbufferSend(port->to_pc, (uint8_t *)str, len, 0);
}

// 上キー: より古いエントリへ
static void hist_up(tnc_port_info_t *port)
{
    if (port->hist_count == 0) return;
    uint8_t *pool = port->hist_pool;

    if (port->hist_pos_off == -1) {
        strncpy(port->hist_saved, port->line_buf, sizeof(port->hist_saved) - 1);
        port->hist_saved[sizeof(port->hist_saved) - 1] = '\0';
        port->hist_saved_pos = port->line_pos;
        port->hist_pos_off = (int)port->hist_head; // 最新から開始
    } else {
        uint8_t cur    = (uint8_t)port->hist_pos_off;
        uint8_t oldest = H_NEXT(pool, port->hist_head);
        if (cur == oldest) return; // 最古に達した
        port->hist_pos_off = (int)H_PREV(pool, cur);
    }
    hist_redraw(port, H_CMD(pool, (uint8_t)port->hist_pos_off));
}

// 下キー: より新しいエントリへ
static void hist_down(tnc_port_info_t *port)
{
    if (port->hist_pos_off == -1) return;
    uint8_t *pool = port->hist_pool;
    uint8_t cur = (uint8_t)port->hist_pos_off;

    if (cur == port->hist_head) {
        port->hist_pos_off = -1;
        hist_redraw(port, port->hist_saved);
        return;
    }
    port->hist_pos_off = (int)H_NEXT(pool, cur);
    hist_redraw(port, H_CMD(pool, (uint8_t)port->hist_pos_off));
}

// --- コマンド解析ロジック ---

void send_prompt(tnc_port_info_t *port)
{
    // コンソール状態機械が制御している間はプロンプト出力を抑制
    if (port->con_suppress_prompt) return;

    char base[7];
    int  ssid;
    callsign_to_ax25(mycall_list[port->id][port->mycall_idx], base, &ssid);

    char prompt[32];
    if (ssid == 0) {
        snprintf(prompt, sizeof(prompt), "PORT%d::%s> ", port->id, base);
    } else {
        snprintf(prompt, sizeof(prompt), "PORT%d::%s-%d> ", port->id, base, ssid);
    }
    xRingbufferSend(port->to_pc, (uint8_t *)prompt, strlen(prompt), 0);
}

void process_command_input(tnc_port_info_t *port, uint8_t *data, size_t len)
{
    if (port == NULL || data == NULL)
        return;

    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];

        if (c == '\n') continue;

        // --- ANSIエスケープシーケンス処理 ---
        if (port->ansi_state == 1) {
            if (c == '[') { port->ansi_state = 2; continue; }
            port->ansi_state = 0; // 不明シーケンス → リセット
        } else if (port->ansi_state == 2) {
            port->ansi_state = 0;
            if      (c == 'A') { hist_up(port);   continue; } // 上キー
            else if (c == 'B') { hist_down(port); continue; } // 下キー
            continue; // 左右キー等は無視
        }

        if (c == 0x1B) { // ESC
            port->ansi_state = 1;
        } else if (c == '\r') {
            const char *crlf = "\r\n";
            xRingbufferSend(port->to_pc, (uint8_t *)crlf, 2, 0);
            port->hist_pos_off = -1; // ブラウズ状態をリセット

            if (port->line_pos > 0) {
                port->line_buf[port->line_pos] = '\0';

                // ヒストリに追加（h/hs/hn/hy は記録しない）
                if (strcmp(port->line_buf, "h")  != 0 &&
                    strcmp(port->line_buf, "hs") != 0 &&
                    strcmp(port->line_buf, "hn") != 0 &&
                    strcmp(port->line_buf, "hy") != 0) {
                    hist_push(port, port->line_buf);
                }

                // コマンド名（最初のトークン）を小文字化
                for (int j = 0; port->line_buf[j] != '\0' && port->line_buf[j] != ' '; j++) {
                    port->line_buf[j] = tolower((unsigned char)port->line_buf[j]);
                }

                // Mutex で保護して実行
                if (xSemaphoreTake(console_mutex, pdMS_TO_TICKS(100))) {
                    vTaskSetThreadLocalStoragePointer(NULL, 0, port);
                    int ret;
                    esp_err_t err = esp_console_run(port->line_buf, &ret);
                    xSemaphoreGive(console_mutex);
                    if (err == ESP_ERR_NOT_FOUND) {
                        cmd_response("Unknown command: %s (type 'help' for list)\r\n",
                                     port->line_buf);
                    }
                } else {
                    const char *busy = "Busy\r\n";
                    xRingbufferSend(port->to_pc, (uint8_t *)busy, strlen(busy), 0);
                }
            }
            // コマンド実行後、バッファをリセットしてプロンプトを表示
            port->line_pos = 0;
            send_prompt(port);
        } else if (c == 0x08 || c == 0x7F) {
            // Backspace / Delete
            if (port->line_pos > 0) {
                port->line_pos--;
                xRingbufferSend(port->to_pc, (uint8_t *)"\b \b", 3, 0);
            }
        } else if (c >= 0x20 && c <= 0x7E) {
            // 表示可能文字のみバッファリング
            if (port->line_pos < (int)sizeof(port->line_buf) - 1) {
                port->line_buf[port->line_pos++] = c;
                xRingbufferSend(port->to_pc, &c, 1, 0);
            }
        }
    }
}

void command_parser_init(void)
{
    console_mutex = xSemaphoreCreateMutex();

    esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    esp_console_init(&console_config);

    register_commands();

    ESP_LOGI(TAG, "Command Parser Initialized");
}

// --- コマンド登録の初期化 ---

static int cmd_hist_show(int argc, char **argv)
{
    tnc_port_info_t *port = pvTaskGetThreadLocalStoragePointer(NULL, 0);
    if (!port) return 1;
    if (port->hist_count == 0) { cmd_response("(no history)\r\n"); return 0; }
    uint8_t *pool = port->hist_pool;
    uint8_t p = H_NEXT(pool, port->hist_head); // oldest
    for (int i = 1; i <= port->hist_count; i++) {
        cmd_response("%2d: %s\r\n", i, H_CMD(pool, p));
        p = H_NEXT(pool, p);
    }
    return 0;
}

static int cmd_hist_save(int argc, char **argv)
{
    tnc_port_info_t *port = pvTaskGetThreadLocalStoragePointer(NULL, 0);
    if (!port) return 1;
    hist_dirty[port->id] = 1;
    history_commit(port);
    cmd_response("History saved.\r\n");
    return 0;
}

static int cmd_hist_off(int argc, char **argv)
{
    tnc_port_info_t *port = pvTaskGetThreadLocalStoragePointer(NULL, 0);
    if (!port) return 1;
    port->hist_enabled = 0;
    cmd_response("History off.\r\n");
    return 0;
}

static int cmd_hist_on(int argc, char **argv)
{
    tnc_port_info_t *port = pvTaskGetThreadLocalStoragePointer(NULL, 0);
    if (!port) return 1;
    port->hist_enabled = 1;
    cmd_response("History on.\r\n");
    return 0;
}

static const esp_console_cmd_t s_cmds[] = {
    {"version", "Show version",                     NULL,          &cmd_version, NULL},
    {"mycall",  "Manage and select MYCALL",          "[help|list|set|use|del|<CALL>]", &cmd_mycall,  NULL},
    {"my",      "Alias for mycall",                  "[help|list|set|use|del|<CALL>]", &cmd_mycall,  NULL},
    {"testtx",  "Send test packet",                  "[message]",   &cmd_testtx,  NULL},
    {"uisend",  "Send UI information",               "<call>",      &cmd_uisend,  NULL},
    {"uimode",  "Set UI mode",                       "<mode>",      &cmd_uimode,  NULL},
    {"led",     "Control LED (on/off)",              "<on|off>",    &cmd_led,     NULL},
    {"h",       "Show command history",              NULL,          &cmd_hist_show, NULL},
    {"hs",      "Save history to NVS now",           NULL,          &cmd_hist_save, NULL},
    {"hn",      "Stop recording history",            NULL,          &cmd_hist_off,  NULL},
    {"hy",      "Start recording history",           NULL,          &cmd_hist_on,   NULL},
    {"help",    "Show available commands",           NULL,          &cmd_help,    NULL},
};

static int cmd_help(int argc, char **argv)
{
    cmd_response("\r\nAvailable commands:\r\n");
    for (int i = 0; i < (int)(sizeof(s_cmds) / sizeof(s_cmds[0])); i++) {
        if (s_cmds[i].hint) {
            cmd_response("  %-10s %-16s  %s\r\n",
                         s_cmds[i].command, s_cmds[i].hint, s_cmds[i].help);
        } else {
            cmd_response("  %-10s  %s\r\n",
                         s_cmds[i].command, s_cmds[i].help);
        }
    }
    return 0;
}

static void register_commands(void)
{
    for (int i = 0; i < (int)(sizeof(s_cmds) / sizeof(s_cmds[0])); i++) {
        esp_console_cmd_register(&s_cmds[i]);
    }
}
