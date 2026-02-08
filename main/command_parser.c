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
    
    // 平文の表示
    char plain_msg[64];
    int p_len = snprintf(plain_msg, sizeof(plain_msg), "\r\nSending: %s\r\n", msg);
    tinyusb_cdcacm_write_queue(0, (uint8_t *)plain_msg, p_len);

    size_t len = ax25_build_ui_frame(&addr, (uint8_t *)msg, strlen(msg), frame);

    // ポート1へ出力 (Hex Dump)
    tinyusb_cdcacm_write_queue(1, (uint8_t *)"\r\n--- AX.25 Frame ---\r\n", 23);
    for (size_t i = 0; i < len; i++) {
        char hex[4];
        sprintf(hex, "%02X ", frame[i]);
        tinyusb_cdcacm_write_queue(1, (uint8_t *)hex, 3);
        if ((i + 1) % 16 == 0) tinyusb_cdcacm_write_queue(1, (uint8_t *)"\r\n", 2);
    }
    tinyusb_cdcacm_write_queue(1, (uint8_t *)"\r\n", 2);
    tinyusb_cdcacm_write_flush(1, pdMS_TO_TICKS(10));

    // デコードテスト
    char decoded_info[256];
    int decoded_len = ax25_decode_ui_info(frame, len, decoded_info, sizeof(decoded_info));

    if (decoded_len >= 0) {
        char decode_msg[300];
        int d_len = snprintf(decode_msg, sizeof(decode_msg), "Decoded: %s\r\n", decoded_info);
        tinyusb_cdcacm_write_queue(0, (uint8_t *)decode_msg, d_len);
    } else {
        char err_msg[32];
        snprintf(err_msg, sizeof(err_msg), "Decode Failed: %d\r\n", decoded_len);
        tinyusb_cdcacm_write_queue(0, (uint8_t *)err_msg, strlen(err_msg));
    }

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

