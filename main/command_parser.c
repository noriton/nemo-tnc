#include <stdio.h>
#include <string.h>

/* FreeRTOS 関連 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

/* ESP-IDF 関連 */
#include "esp_log.h"
#include "tinyusb.h"
// または tinyusb_cdc_acm.h

/* プロジェクト固有のヘッダー */
#include "tnc_buffer.h"   // usb_rx_ringbuf のハンドルを参照するため
#include "indicator.h"    // もし解析中にLEDを光らせるなら必要
#include "command_parser.h" // 自身のヘッダー（タスクのプロトタイプ宣言など）
#include "tnc_buffer.h"
#include "tinyusb_cdc_acm.h"
#include "esp_log.h"
#include "tnc_settings.h"

static const char *TAG = "PARSER";

// コマンド解析用の一時バッファ
static uint8_t line_buf[128];
static int line_pos = 0;

// 仮のコマンド処理関数
static void process_command(char *cmd) {
    // 簡易的な解析: "MYCALL " で始まるかチェック
    if (strncmp(cmd, "MYCALL ", 7) == 0) {
        char *new_call = cmd + 7; // "MYCALL " の後の文字列
        if (settings_save_mycall(new_call) == ESP_OK) {
            const char *msg = "\r\nCallsign saved!\r\n";
            tinyusb_cdcacm_write_queue(0, (uint8_t *)msg, strlen(msg));
        }
    } else if (strcmp(cmd, "MYCALL") == 0) {
        char saved_call[16] = {0};
        if (settings_load_mycall(saved_call, sizeof(saved_call)) == ESP_OK) {
            char msg[32];
            snprintf(msg, sizeof(msg), "\r\nMYCALL is %s\r\n", saved_call);
            tinyusb_cdcacm_write_queue(0, (uint8_t *)msg, strlen(msg));
        } else {
            const char *msg = "\r\nMYCALL not set\r\n";
            tinyusb_cdcacm_write_queue(0, (uint8_t *)msg, strlen(msg));
        }
    } else if (strcmp(cmd, "VERSION") == 0) {
        const char *ver = "\r\nNEMO-TNC v0.1\r\n";
        tinyusb_cdcacm_write_queue(0, (uint8_t *)ver, strlen(ver));
    }

    tinyusb_cdcacm_write_flush(0, 0);
}

static void command_parser_task(void *pvParameters) {
    while (1) {
        for (int itf = 0; itf < 2; itf++) {
            size_t size;
            // 各ポートのバッファをチェック（待ち時間は0にして高速に回す）
            uint8_t *data = (uint8_t *)xRingbufferReceiveUpTo(usb_rb[itf], &size, 0, 128);

            if (data != NULL) {
                if (itf == 0) {
                    // --- ポート0: コマンド解析モード ---
                    for (int i = 0; i < size; i++) {
                        // エコーバック（打った文字が自分に見えるように）
                        tinyusb_cdcacm_write_queue(0, &data[i], 1);
                        tinyusb_cdcacm_write_flush(0, 0);

                        if (data[i] == '\r' || data[i] == '\n') {
                            if (line_pos > 0) {
                                line_buf[line_pos] = '\0';
                                process_command((char *)line_buf);
                                line_pos = 0;
                            }
                        } else if (line_pos < sizeof(line_buf) - 1) {
                            line_buf[line_pos++] = data[i];
                        }
                    }
                } else {
                    // --- ポート1: データ転送モード（逆ポートへスルー） ---
                    tinyusb_cdcacm_write_queue(0, data, size); // 1から来たものを0へ
                    tinyusb_cdcacm_write_flush(0, 0);
                }
                vRingbufferReturnItem(usb_rb[itf], (void *)data);
            }
        }
        // CPUを占有しないよう、少しだけ休む
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void command_parser_init (void) {
    // 解析タスクの起動
    xTaskCreate(command_parser_task, "command_parser", 4096, NULL, 10, NULL);
}


/*

*/
