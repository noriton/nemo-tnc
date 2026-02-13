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

void pc_port_task(void *pvParameters) {
    tnc_pc_port_t *port = (tnc_pc_port_t *)pvParameters;
    
    // 現在のタスクのTLSにポート情報を保存 (Index 0)
    vTaskSetThreadLocalStoragePointer(NULL, 0, port);

    while (1) {
        size_t size;
        uint8_t *data = (uint8_t *)xRingbufferReceive(port->rx_rb, &size, pdMS_TO_TICKS(10));
        
        if (data != NULL) {
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
                // ToDo 6: トランスペアレントモード処理
                // トランスペアレントモードからエスケープする処理
                // if (data[i] == ?) { //  +++ もしくは^C^C^C
                //     port->mode = PORT_MODE_COMMAND;
                // }
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
            }
            vRingbufferReturnItem(port->rx_rb, (void *)data);
        }
    }
}