#include "pc_interface.h"
#include "tnc_buffer.h"
#include "command_parser.h"
#include "tinyusb_cdc_acm.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PC_IF";

// 現在コマンドを実行しているポート (-1: なし, 0: Port0, 1: Port1)
static volatile int g_cmd_port = -1;
static SemaphoreHandle_t g_cmd_mutex = NULL;

// ラインバッファ (各ポート用)
static uint8_t line_buf_0[128];
static int line_pos_0 = 0;

static uint8_t line_buf_1[128];
static int line_pos_1 = 0;


void pc_write_feedback(const uint8_t *data, size_t len) {
    if (g_cmd_port >= 0) {
        tinyusb_cdcacm_write_queue(g_cmd_port, data, len);
        tinyusb_cdcacm_write_flush(g_cmd_port, 0);
    }
}

void pc_write_result(const uint8_t *data, size_t len) {
    if (g_cmd_port >= 0) {
        // 現在のポートではない方に出力 (0->1, 1->0)
        int target_port = g_cmd_port ^ 1; 
        tinyusb_cdcacm_write_queue(target_port, data, len);
        tinyusb_cdcacm_write_flush(target_port, 0);
    }
}

static void process_char(int port, uint8_t ch, uint8_t *line_buf, int *line_pos) {
    // エコーバック
    tinyusb_cdcacm_write_queue(port, &ch, 1);
    tinyusb_cdcacm_write_flush(port, 0);

    if (ch == '\r' || ch == '\n') {
        if (*line_pos > 0) {
            line_buf[*line_pos] = '\0';
            
            // 排他制御でコマンド実行
            if (xSemaphoreTake(g_cmd_mutex, portMAX_DELAY) == pdTRUE) {
                g_cmd_port = port; // 現在のポートを設定

                int ret;
                esp_err_t err = esp_console_run((char *)line_buf, &ret);
                
                if (err == ESP_ERR_NOT_FOUND) {
                    pc_write_feedback((uint8_t *)"\r\nUnknown command\r\n", 19);
                }

                g_cmd_port = -1; // リセット
                xSemaphoreGive(g_cmd_mutex);
            }

            *line_pos = 0;
            tinyusb_cdcacm_write_queue(port, (uint8_t *)"\r\nTNC> ", 7);
            tinyusb_cdcacm_write_flush(port, 0);
        }
    } else if (*line_pos < 127) {
        line_buf[(*line_pos)++] = ch;
    }
}

static void pc_port_task_0(void *pvParameters) {
    size_t size;
    while (1) {
        uint8_t *data = (uint8_t *)xRingbufferReceiveUpTo(usb_rb[0], &size, pdMS_TO_TICKS(10), 64);
        if (data != NULL) {
            for (int i = 0; i < size; i++) {
                process_char(0, data[i], line_buf_0, &line_pos_0);
            }
            vRingbufferReturnItem(usb_rb[0], (void *)data);
        }
    }
}

static void pc_port_task_1(void *pvParameters) {
    size_t size;
    while (1) {
        uint8_t *data = (uint8_t *)xRingbufferReceiveUpTo(usb_rb[1], &size, pdMS_TO_TICKS(10), 64);
        if (data != NULL) {
            for (int i = 0; i < size; i++) {
                process_char(1, data[i], line_buf_1, &line_pos_1);
            }
            vRingbufferReturnItem(usb_rb[1], (void *)data);
        }
    }
}

void pc_interface_init(void) {
    g_cmd_mutex = xSemaphoreCreateMutex();
    
    // Console初期化とコマンド登録 (command_parser.c の機能を利用)
    // command_parser_init() の代わりにここで初期化フローの一部を行うか、
    // あるいは command_parser_init() を呼ぶが、あちらのタスク生成を止める必要がある。
    // 今回は command_parser.c を「コマンド登録」のみに専念させ、タスクは削除する予定なので、
    // ここで esp_console_init を呼ぶのは妥当。
    // ただし command_parser.c 側で register_commands を呼ぶ必要がある。
    
    esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    esp_console_init(&console_config);
    
    register_commands(); // command_parser.h に宣言されている前提

    xTaskCreate(pc_port_task_0, "pc_task_0", 4096, NULL, 5, NULL);
    xTaskCreate(pc_port_task_1, "pc_task_1", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "PC Interface Initialized (Dual Port)");
}
