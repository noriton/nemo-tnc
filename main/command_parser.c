#include "command_parser.h"
#include "tx_frame.h"
#include "tnc_buffer.h"
#include "tnc_settings.h"
#include "ax25.h"
#include "tinyusb_cdc_acm.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_log.h"
#include <string.h>

static SemaphoreHandle_t console_mutex = NULL;

static const char *TAG = "CMD_PARSER";

// --- 各コマンドの実装関数 ---

// VERSION コマンド
static int cmd_version(int argc, char **argv) {
    const char *ver = "\r\nNEMO-TNC v0.1 (esp_console)\r\n";
    // 結果を現在のポート（引数で渡すのが理想だが、ここでは簡易的に最後にコマンドを実行したポートへの返信は
    // プロセス関数内で行うか、あるいはコマンド自体は標準出力を使う想定にする）
    // esp_console_run は標準出力(stdout)を使う設定もできるが、今回は「入力ポートへの書き戻し」が必要。
    // しかし esp_console のコマンド関数シグネチャは変えられない。
    // ここでは簡易的に「コマンド実行中にグローバル変数などでポートを特定する」か、
    // あるいは「コマンドからの出力は標準出力に吐き、それをリダイレクトする」等の仕組みが必要だが、
    // 今回の要件では「pc_write_feedback」を使っていた。
    // pc_interface がなくなるので、直接書き込む手段が必要。
    // ただし、この関数内からは port オブジェクトが見えない。
    // 暫定的に、コマンド実行時コンテキストを持つ変数を用意するか、
    // またはコマンド自体は文字列を構築して返すだけの設計にするのが綺麗だが、
    // 既存コードをあまり壊さないようにするため、
    // コマンド実行の前後で「現在のポート」を保持するstatic変数を用意するアプローチをとる。
    return 0;
}

// --- コマンド応答用ヘルパー ---
static void cmd_response(const char *fmt, ...) {
    // 現在実行中のタスクの TLS からポート情報を取得
    tnc_pc_port_t *port = (tnc_pc_port_t *)pvTaskGetThreadLocalStoragePointer(NULL, 0);

    if (port != NULL) {
        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        // ポートの送信バッファへ書き込み
        xRingbufferSend(port->tx_rb, (uint8_t *)buf, strlen(buf), 0);
    }
}

// VERSION コマンド (修正版)
static int cmd_version(int argc, char **argv) {
    cmd_response("\r\nNEMO-TNC v0.1 (esp_console)\r\n");
    return 0;
}

// MYCALL コマンド (引数があれば設定、なければ表示)
static int cmd_mycall(int argc, char **argv) {
    if (argc > 1) {
        if (settings_save_mycall(argv[1]) == ESP_OK) {
            cmd_response("Saved.\r\n");
        }
    } else {
        char call[16] = {0};
        if (settings_load_mycall(call, sizeof(call)) == ESP_OK) {
            cmd_response("\r\nMYCALL: %s\r\n", call);
        } else {
            cmd_response("MYCALL not set.\r\n");
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
    // 平文の表示 (Input Portへフィードバック)
    cmd_response("\r\nSending: %s\r\n", msg);

    size_t len = ax25_build_ui_frame(&addr, (uint8_t *)msg, strlen(msg), frame);

    // TX Frameへエンキュー
    if (tx_frame_enqueue(frame, len) == ESP_OK) {
        cmd_response("Queued to TX Buffer.\r\n");
    } else {
        cmd_response("Failed to Queue.\r\n");
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

// --- コマンド解析ロジック ---

void process_command_input(tnc_pc_port_t *port, uint8_t *data, size_t len) {
    if (port == NULL || data == NULL) return;

    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];

        // エコーバック
        xRingbufferSend(port->tx_rb, &c, 1, 0);

        if (c == '\r' || c == '\n') {
            if (port->line_pos > 0) {
                port->line_buf[port->line_pos] = '\0';
                
                // Mutexで保護してコマンド実行
                if (xSemaphoreTake(console_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    int ret;
                    esp_err_t err = esp_console_run(port->line_buf, &ret);
                    
                    if (err == ESP_ERR_NOT_FOUND) {
                         const char *msg = "\r\nUnknown command\r\n";
                         xRingbufferSend(port->tx_rb, (uint8_t *)msg, strlen(msg), 0);
                    }


                    xSemaphoreGive(console_mutex);
                }
                port->line_pos = 0;
                
                const char *prompt = "\r\nTNC> ";
                xRingbufferSend(port->tx_rb, (uint8_t *)prompt, strlen(prompt), 0);
            }
        } else if (port->line_pos < sizeof(port->line_buf) - 1) {
            port->line_buf[port->line_pos++] = (char)c;
        }
    }
}

void command_parser_init(void) {
    console_mutex = xSemaphoreCreateMutex();
    
    esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    esp_console_init(&console_config);

    register_commands();
    
    ESP_LOGI(TAG, "Command Parser Initialized");
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


