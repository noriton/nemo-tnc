#include "command_parser.h"
#include "tx_frame.h"
#include "tnc_buffer.h"
#include "tnc_settings.h"
#include "ax25.h"
#include "tinyusb_cdc_acm.h"
#include "esp_console.h"
#include "esp_log.h"
#include "pc_interface.h"
#include <string.h>

static const char *TAG = "CMD_PARSER";

// --- 各コマンドの実装関数 ---

// VERSION コマンド
static int cmd_version(int argc, char **argv) {
    const char *ver = "\r\nNEMO-TNC v0.1 (esp_console)\r\n";
    // 結果を「コマンド入力ポート」に出力
    pc_write_feedback((uint8_t *)ver, strlen(ver));
    return 0;
}

// MYCALL コマンド (引数があれば設定、なければ表示)
static int cmd_mycall(int argc, char **argv) {
    if (argc > 1) {
        if (settings_save_mycall(argv[1]) == ESP_OK) {
            pc_write_feedback((uint8_t *)"Saved.\r\n", 8);
        }
    } else {
        char call[16] = {0};
        if (settings_load_mycall(call, sizeof(call)) == ESP_OK) {
            char msg[32];
            int len = snprintf(msg, sizeof(msg), "\r\nMYCALL: %s\r\n", call);
            pc_write_feedback((uint8_t *)msg, len);
        } else {
            pc_write_feedback((uint8_t *)"MYCALL not set.\r\n", 17);
        }
    }
    return 0;
}

// TESTTX コマンド (tx_frame経由で送信)
static int cmd_testtx(int argc, char **argv) {
    char my_call[16] = {0};
    settings_load_mycall(my_call, sizeof(my_call));
    
    ax25_address_t addr = {
        .dest_call = "CQ    ", .dest_ssid = 0,
        .src_call = my_call, .src_ssid = 0
    };

    uint8_t frame[300];
    const char *msg = (argc > 1) ? argv[1] : "HELLO";
    
    // 平文の表示 (Input Portへフィードバック)
    char plain_msg[64];
    int p_len = snprintf(plain_msg, sizeof(plain_msg), "\r\nSending: %s\r\n", msg);
    pc_write_feedback((uint8_t *)plain_msg, p_len);

    size_t len = ax25_build_ui_frame(&addr, (uint8_t *)msg, strlen(msg), frame);

    // TX Frameへエンキュー
    if (tx_frame_enqueue(frame, len) == ESP_OK) {
        pc_write_feedback((uint8_t *)"Queued to TX Buffer.\r\n", 22);
    } else {
        pc_write_feedback((uint8_t *)"Failed to Queue.\r\n", 18);
    }

    // TESTTXは送信コマンドなので、受信側の「もう一方のポート」へのHEXダンプ出力は
    // ここではなく rx_frame.c で行うべきだが、今回の要件「出力結果をもう一つのポートに表示」
    // に従い、ここでも何らかの表示をするか？
    // いえ、TESTTXは「送信」コマンドであり、実行結果は「キューに入れた」ことです。
    // 「受信した」結果は rx_frame が担当します。
    // rx_frame は現在 Port 1 固定ですが、ここも pc_write_result 的な動きが必要なら修正要。
    // しかし rx_frame は非同期タスクなので g_cmd_port を参照できない。
    // 今回はコマンド応答のクロス表示がメインなので、これで良しとします。

    return 0;
}

// --- コマンド登録の初期化 ---

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


