#include "tnc_pc_port.h"
#include "tnc_buffer.h"
#include "command_parser.h"
#include "tinyusb_cdc_acm.h"
#include "esp_log.h"
#include <string.h>


void pc_port_task(void *pvParameters);

tnc_pc_port_t pc_ports[2];

void tnc_pc_ports_init(void) {
    for (int i = 0; i < 2; i++) {
        pc_ports[i].id = i;
        pc_ports[i].mode = PORT_MODE_COMMAND; // 初期状態はどちらもコマンドモード
        
        // RXバッファは USB受信バッファ (usb_from_pc) を参照する形にする
        // これで usb_descriptors.c が usb_from_pc に入れたデータをここで吸い出せる
        pc_ports[i].rx_rb = usb_from_pc[i];
        
        // TXバッファは独自に作成 (あるいはこれも一本化可能だが、ひとまず既存維持)
        if (pc_ports[i].tx_rb == NULL) {
             pc_ports[i].tx_rb = xRingbufferCreate(2048, RINGBUF_TYPE_NOSPLIT);
        }
        pc_ports[i].line_pos = 0;

        // タスク起動
        char task_name[16];
        snprintf(task_name, sizeof(task_name), "pc_port_%d", i);
        xTaskCreate(pc_port_task, task_name, 4096, &pc_ports[i], 5, NULL);
    }
}

static void flush_transport_buffer(tnc_pc_port_t *port) {
    if (port->trans_len > 0) {
        
        // ★本来はここで AX.25 UIフレーム生成関数などを呼ぶ場所です
        // 例: ax25_send_ui_frame(port->trans_buf, port->trans_len);
        
        // --- [デバッグ用] 送信したつもりでPCへ通知 ---
        // 実際にパケットになったことが分かるようにフォーマットしてPCへ返す
        char debug_msg[64];
        snprintf(debug_msg, sizeof(debug_msg), "\r\n[TX Packet: %d bytes]\r\n", port->trans_len);
        xRingbufferSend(port->tx_rb, (uint8_t*)debug_msg, strlen(debug_msg), 0);
        
        // データ本体もエコーバックしてみる（確認用）
        xRingbufferSend(port->tx_rb, port->trans_buf, port->trans_len, 0);
        
        // -----------------------------------------------------

        // 送信完了したのでバッファをクリア
        port->trans_len = 0;
    }
}

void pc_port_task(void *pvParameters) {
    tnc_pc_port_t *port = (tnc_pc_port_t *)pvParameters;

    // 現在のタスクのTLSにポート情報を保存 (Index 0)
    vTaskSetThreadLocalStoragePointer(NULL, 0, port);

    port->trans_len = 0;
    port->last_rx_tick = xTaskGetTickCount();

    while (1) {
        size_t size;
        uint8_t *data = (uint8_t *)xRingbufferReceive(port->rx_rb, &size, pdMS_TO_TICKS(10));
        
        // トランスペアレントモード用のタイムアウト処理用
        TickType_t now = xTaskGetTickCount();

        if (data != NULL) {
            port->last_rx_tick = now; // 最終受信時刻を更新
            // 一括処理へ変更
            if (port->mode == PORT_MODE_COMMAND) {
                process_command_input(port, data, size);
            } else if (port->mode == PORT_MODE_UICHAT) {
                // ToDo 7: UIチャットモード処理
                // 改行文字で区切られた文字列を生フレームとして送信 
                // モードからエスケープする処理
                // if (data[i] == ?) { //  +++ もしくは^C^C^C
                //     port->mode = PORT_MODE_COMMAND;
                // }
            } else if (port->mode == PORT_MODE_TRANSPORT) {
                // ToDo 6: トランスポートモード処理
                for (int i = 0; i < size; i++) {
                    uint8_t c = data[i];

                    // トランスポートモードからエスケープする処理
                    // if (data[i] == ?) { //  最終的には+++ もしくは^C^C^C
                    //     port->mode = PORT_MODE_COMMAND;
                    // }

                    // エスケープ処理 (Ctrl+C = 0x03 でコマンドモードへ戻る)
                    if (c == 0x03) {
                        flush_transport_buffer(port); // 残っているデータを吐き出す
                        
                        port->mode = PORT_MODE_COMMAND;
                        const char *msg = "\r\nCommand Mode\r\nTNC> ";
                        xRingbufferSend(port->tx_rb, (uint8_t*)msg, strlen(msg), 0);
                        
                        // バッファリセット
                        port->line_pos = 0; 
                        break; 
                    }

                    // バッファに格納
                    if (port->trans_len < TNC_PACLEN) {
                        port->trans_buf[port->trans_len++] = c;
                    }

                    // [条件1: PACLEN到達] 
                    // バッファがいっぱいになったら即送信
                    if (port->trans_len >= TNC_PACLEN) {
                        flush_transport_buffer(port);
                    } else if (port->trans_len > 0) {
                    // [条件2: PACTIME経過]
                    // 最後のデータ受信から一定時間(TNC_PACTIME)経過したら送信
                        if ((now - port->last_rx_tick) > pdMS_TO_TICKS(TNC_PACTIME)) {
                            flush_transport_buffer(port);
                        }
                    }
                }
            } else if (port->mode == PORT_MODE_KISS) {
                // ToDo 5: KISSモード処理
                // main command port ではこのモードにならない。

            } else if (port->mode == PORT_MODE_TURNBACK) {
                // 同一ポートのtoPCに分割して折り返し
                // トランスペアレントモードからエスケープする処理
                // if (data[i] == ?) { //  +++ もしくは^C^C^C
                //     port->mode = PORT_MODE_COMMAND;
                // }
                xRingbufferSend(port->tx_rb, data, size, 0);
            } else if (port->mode == PORT_MODE_BRIDGE) {
                // 別のポートへスルー
                int peer_id = (port->id == 0) ? 1 : 0;
                xRingbufferSend(pc_ports[peer_id].tx_rb, data, size, 0);
            } else {

            }
            vRingbufferReturnItem(port->rx_rb, (void *)data);
        }
    }
}