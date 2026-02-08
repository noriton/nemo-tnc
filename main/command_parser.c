#include "command_parser.h"
#include "tnc_buffer.h"
#include "tnc_settings.h"
#include "ax25.h"
#include "tinyusb_cdc_acm.h"
#include "esp_console.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CMD_PARSER";
static uint8_t line_buf[128];
static int line_pos = 0;

// --- 各コマンドの実装関数 ---

// VERSION コマンド
static int cmd_version(int argc, char **argv) {
    const char *ver = "\r\nNEMO-TNC v0.1 (esp_console)\r\n";
    tinyusb_cdcacm_write_queue(0, (uint8_t *)ver, strlen(ver));
    tinyusb_cdcacm_write_flush(0, 0);
    return 0;
}

// MYCALL コマンド (引数があれば設定、なければ表示)
static int cmd_mycall(int argc, char **argv) {
    if (argc > 1) {
        if (settings_save_mycall(argv[1]) == ESP_OK) {
            tinyusb_cdcacm_write_queue(0, (uint8_t *)"Saved.\r\n", 8);
        }
    } else {
        char call[16] = {0};
        if (settings_load_mycall(call, sizeof(call)) == ESP_OK) {
            char msg[32];
            int len = snprintf(msg, sizeof(msg), "\r\nMYCALL: %s\r\n", call);
            tinyusb_cdcacm_write_queue(0, (uint8_t *)msg, len);
        } else {
            tinyusb_cdcacm_write_queue(0, (uint8_t *)"MYCALL not set.\r\n", 17);
        }
    }
    tinyusb_cdcacm_write_flush(0, 0);
    return 0;
}

// TESTTX コマンド (ITF 1 へダンプを流す)
static int cmd_testtx(int argc, char **argv) {
    char my_call[16] = {0};
    settings_load_mycall(my_call, sizeof(my_call));
    
    ax25_address_t addr = {
        .dest_call = "CQ    ", .dest_ssid = 0,
        .src_call = my_call, .src_ssid = 0
    };

    uint8_t frame[300];
    const char *msg = (argc > 1) ? argv[1] : "HELLO";
    size_t len = ax25_build_ui_frame(&addr, (uint8_t *)msg, strlen(msg), frame);

    // ポート1へ出力
    tinyusb_cdcacm_write_queue(1, (uint8_t *)"\r\n--- AX.25 Frame ---\r\n", 23);
    for (size_t i = 0; i < len; i++) {
        char hex[4];
        sprintf(hex, "%02X ", frame[i]);
        tinyusb_cdcacm_write_queue(1, (uint8_t *)hex, 3);
        if ((i + 1) % 16 == 0) tinyusb_cdcacm_write_queue(1, (uint8_t *)"\r\n", 2);
    }
    tinyusb_cdcacm_write_queue(1, (uint8_t *)"\r\n", 2);
    tinyusb_cdcacm_write_flush(1, pdMS_TO_TICKS(10));

    tinyusb_cdcacm_write_queue(0, (uint8_t *)"Sent to Data Port.\r\n", 20);
    tinyusb_cdcacm_write_flush(0, 0);
    return 0;
}

// --- コマンド登録の初期化 ---
// command_parser.c の register_commands 内

void register_commands(void) {
    // --- 標準のヘルプコマンドを登録  ---
    esp_console_register_help_command();

    // --- その他の自作コマンドを登録 ---
    const esp_console_cmd_t cmds[] = {
        {"VERSION", "Show version", NULL, &cmd_version, NULL},
        {"MYCALL",  "Set/Get callsign", "<call>", &cmd_mycall, NULL},
        {"TESTTX",  "Send test packet", "[message]", &cmd_testtx, NULL},
    };

    for (int i = 0; i < sizeof(cmds) / sizeof(esp_console_cmd_t); i++) {
        esp_console_cmd_register(&cmds[i]);
    }
}


// --- メインタスク ---
void command_parser_task(void *pvParameters) {
    esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    esp_console_init(&console_config);
    register_commands();

    while (1) {
        size_t size;
        uint8_t *data = (uint8_t *)xRingbufferReceiveUpTo(usb_rb[0], &size, 0, 128);

        if (data != NULL) {
            for (int i = 0; i < size; i++) {
                // エコーバック
                tinyusb_cdcacm_write_queue(0, &data[i], 1);
                tinyusb_cdcacm_write_flush(0, 0);

                if (data[i] == '\r' || data[i] == '\n') {
                    if (line_pos > 0) {
                        line_buf[line_pos] = '\0';
                        int ret;
                        esp_err_t err = esp_console_run((char *)line_buf, &ret);
                        if (err == ESP_ERR_NOT_FOUND) {
                            tinyusb_cdcacm_write_queue(0, (uint8_t *)"\r\nUnknown command\r\n", 19);
                        }
                        line_pos = 0;
                        tinyusb_cdcacm_write_queue(0, (uint8_t *)"\r\nTNC> ", 7);
                    }
                } else if (line_pos < sizeof(line_buf) - 1) {
                    line_buf[line_pos++] = data[i];
                }
            }
            vRingbufferReturnItem(usb_rb[0], (void *)data);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void command_parser_init (void) {
    register_commands();
    // 解析タスクの起動
    xTaskCreate(command_parser_task, "command_parser", 8192, NULL, 10, NULL);
}

#ifdef ndef
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
#include "ax25.h"
#include "tnc_settings.h"
/* プロジェクト固有のヘッダー */
#include "tnc_buffer.h"   // usb_rx_ringbuf のハンドルを参照するため
#include "indicator.h"    // もし解析中にLEDを光らせるなら必要
#include "command_parser.h" // 自身のヘッダー（タスクのプロトタイプ宣言など）
#include "tnc_buffer.h"
#include "tinyusb_cdc_acm.h"
#include "esp_log.h"
#include "tnc_settings.h"
#include "ax25.h" // encode_callsign を使うため

static const char *TAG = "PARSER";

// コマンド解析用の一時バッファ
static uint8_t line_buf[128];
static int line_pos = 0;

// 16進数ダンプ用のヘルパー
void hex_dump(const char *label, const uint8_t *data, size_t len) {
    printf("%s (%d bytes):\n", label, (int)len);
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
}

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
    } else if (strncmp(cmd, "ENCODE ", 7) == 0) {
        char *call = cmd + 7;
        // テスト用: "JH1XXX" をAX.25形式にエンコードして表示
        // "JH1XXX" の場合、期待される出力（一部）:
        // 'J' (0x4A) -> 0x94
        // 'H' (0x48) -> 0x90
        // '1' (0x31) -> 0x62
        char encoded[7];
        encode_callsign((uint8_t *)encoded, call, 0, true); 

        const char *sn = "\r\n";
        tinyusb_cdcacm_write_queue(0, (uint8_t *)sn, strlen(sn));

        for (int i = 0; i < 7; i++) {
            char byte_str[5];
            snprintf(byte_str, sizeof(byte_str), "%02X ", encoded[i]);
            tinyusb_cdcacm_write_queue(0, (uint8_t *)byte_str, strlen(byte_str));
        }
        tinyusb_cdcacm_write_queue(0, (uint8_t *)sn, strlen(sn));
    } else if (strcmp(cmd, "TESTTX") == 0) {
        char my_call[16] = {0};
        if (settings_load_mycall(my_call, sizeof(my_call)) != ESP_OK) {
            strcpy(my_call, "NOCALL");
        }

        ax25_address_t addr = {
            .dest_call = "CQ    ",
            .dest_ssid = 0,
            .src_call = my_call,
            .src_ssid = 0
        };

        const char *msg = "HELLO";
        uint8_t frame[300];
        size_t len = ax25_build_ui_frame(&addr, (uint8_t *)msg, strlen(msg), frame);
        // ポート1（データポート）へ16進数文字列として出力
        char hex_str[4];
        const char *header = "--- AX.25 Frame ---\r\n";
        tinyusb_cdcacm_write_queue(1, (uint8_t *)header, strlen(header));

        for (size_t i = 0; i < len; i++) {
            sprintf(hex_str, "%02X ", frame[i]);
            tinyusb_cdcacm_write_queue(1, (uint8_t *)hex_str, strlen(hex_str));
            
            // 16バイトごとに改行して見やすくする
            if ((i + 1) % 16 == 0) {
                tinyusb_cdcacm_write_queue(1, (uint8_t *)"\r\n", 2);
            }
        }
        
        tinyusb_cdcacm_write_queue(1, (uint8_t *)"\r\n--------------------\r\n", 24);
        tinyusb_cdcacm_write_flush(1, pdMS_TO_TICKS(10)); // ポート1をフラッシュ！

        // コマンドポート(0)には完了通知だけ出す
        const char *done_msg = "\r\nPacket sent to Data Port (ITF 1).\r\n";
        tinyusb_cdcacm_write_queue(0, (uint8_t *)done_msg, strlen(done_msg));
        tinyusb_cdcacm_write_flush(0, 0);

    } else if (strncmp(cmd, "TX ", 3) == 0) {
        char *message = cmd + 3;
        char my_call[16];
        settings_load_mycall(my_call, sizeof(my_call));

        ax25_address_t addr = {
            .dest_call = "CQ    ",
            .dest_ssid = 0,
            .src_call = my_call,
            .src_ssid = 0
        };

        uint8_t frame[300];
        size_t frame_len = ax25_build_ui_frame(&addr, (uint8_t *)message, strlen(message), frame);
        tinyusb_cdcacm_write_queue(0, (uint8_t *)message, strlen(message));
    } else if (strcmp(cmd, "HELP") == 0) {
        const char *msg = "\r\nUnknown command\r\n";
        tinyusb_cdcacm_write_queue(0, (uint8_t *)msg, strlen(msg));
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
#endif