#include "tnc_pc_port.h"
#include "esp_console.h"
#include "tinyusb_cdc_acm.h"
#include <string.h>

tnc_pc_port_t pc_ports[2];
SemaphoreHandle_t console_mutex = NULL;

void tnc_pc_ports_init(void) {
    console_mutex = xSemaphoreCreateMutex();
    
    for (int i = 0; i < 2; i++) {
        pc_ports[i].id = i;
        pc_ports[i].mode = PORT_MODE_COMMAND; // 初期状態はどちらもコマンドモード
        pc_ports[i].rx_rb = xRingbufferCreate(2048, RINGBUF_TYPE_NOSPLIT);
        pc_ports[i].tx_rb = xRingbufferCreate(2048, RINGBUF_TYPE_NOSPLIT);
        pc_ports[i].line_pos = 0;
    }
}

// 共通のコマンド処理ロジック
static void handle_command_char(tnc_pc_port_t *port, uint8_t c) {
    // エコーバック（送信バッファ経由でPCへ返す）
    xRingbufferSend(port->tx_rb, &c, 1, 0);

    if (c == '\r' || c == '\n') {
        if (port->line_pos > 0) {
            port->line_buf[port->line_pos] = '\0';
            
            // Mutexで保護してコマンド実行
            if (xSemaphoreTake(console_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                int ret;
                esp_console_run(port->line_buf, &ret);
                xSemaphoreGive(console_mutex);
            }
            port->line_pos = 0;
            const char *prompt = "\r\nTNC> ";
            xRingbufferSend(port->tx_rb, prompt, strlen(prompt), 0);
        }
    } else if (port->line_pos < sizeof(port->line_buf) - 1) {
        port->line_buf[port->line_pos++] = c;
    }
}

void pc_port_task(void *pvParameters) {
    tnc_pc_port_t *port = (tnc_pc_port_t *)pvParameters;
    
    while (1) {
        size_t size;
        uint8_t *data = (uint8_t *)xRingbufferReceive(port->rx_rb, &size, pdMS_TO_TICKS(10));
        
        if (data != NULL) {
            for (int i = 0; i < size; i++) {
                if (port->mode == PORT_MODE_COMMAND) {
                    handle_command_char(port, data[i]);
                } 
                else if (port->mode == PORT_MODE_BRIDGE) {
                    // ToDo 4: 相方のポートへスルー
                    int peer_id = (port->id == 0) ? 1 : 0;
                    xRingbufferSend(pc_ports[peer_id].tx_rb, &data[i], 1, 0);
                }
            }
            vRingbufferReturnItem(port->rx_rb, (void *)data);
        }
    }
}