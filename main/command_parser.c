#include "command_parser.h"
#include "ax25.h"
#include "esp_console.h"
#include "esp_log.h"
#include "nvs_if.h"
#include "tx_frame.h"
#include "indicator.h"
#include "callsign.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>



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

        // ポートの送信バッファへ書き込み
        xRingbufferSend(port->to_pc, (uint8_t *)buf, strlen(buf), 0);
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

// コマンドをヒストリに追加（連続重複は無視）
static void hist_push(tnc_port_info_t *port, const char *cmd)
{
    if (cmd[0] == '\0') return;

    // 直前と同じコマンドは登録しない
    if (port->hist_count > 0) {
        int prev = (port->hist_head - 1 + CMD_HISTORY_SIZE) % CMD_HISTORY_SIZE;
        if (strcmp(port->history[prev], cmd) == 0) return;
    }

    strncpy(port->history[port->hist_head], cmd, sizeof(port->history[0]) - 1);
    port->history[port->hist_head][sizeof(port->history[0]) - 1] = '\0';
    port->hist_head = (port->hist_head + 1) % CMD_HISTORY_SIZE;
    if (port->hist_count < CMD_HISTORY_SIZE) port->hist_count++;
}

// 現在の入力行を消去して str を表示
static void hist_redraw(tnc_port_info_t *port, const char *str)
{
    // バックスペースで現在の入力を消す
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

// ヒストリを古い方向へ（上キー）
static void hist_up(tnc_port_info_t *port)
{
    if (port->hist_count == 0) return;

    if (port->hist_pos == -1) {
        // 現在の入力を退避してブラウズ開始
        strncpy(port->hist_saved, port->line_buf, sizeof(port->hist_saved) - 1);
        port->hist_saved[sizeof(port->hist_saved) - 1] = '\0';
        port->hist_saved_pos = port->line_pos;
        port->hist_pos = 0;
    } else if (port->hist_pos < port->hist_count - 1) {
        port->hist_pos++;
    } else {
        return; // 最古に達している
    }

    int idx = (port->hist_head - 1 - port->hist_pos + CMD_HISTORY_SIZE * 2) % CMD_HISTORY_SIZE;
    hist_redraw(port, port->history[idx]);
}

// ヒストリを新しい方向へ（下キー）
static void hist_down(tnc_port_info_t *port)
{
    if (port->hist_pos == -1) return;

    if (port->hist_pos > 0) {
        port->hist_pos--;
        int idx = (port->hist_head - 1 - port->hist_pos + CMD_HISTORY_SIZE * 2) % CMD_HISTORY_SIZE;
        hist_redraw(port, port->history[idx]);
    } else {
        // 最新より新しい → 退避した入力に戻す
        port->hist_pos = -1;
        hist_redraw(port, port->hist_saved);
    }
}

// --- コマンド解析ロジック ---

void send_prompt(tnc_port_info_t *port)
{
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
            port->hist_pos = -1; // ブラウズ状態をリセット

            if (port->line_pos > 0) {
                port->line_buf[port->line_pos] = '\0';

                // ヒストリに追加
                hist_push(port, port->line_buf);

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

static const esp_console_cmd_t s_cmds[] = {
    {"version", "Show version",                     NULL,          &cmd_version, NULL},
    {"mycall",  "Manage and select MYCALL",          NULL,          &cmd_mycall,  NULL},
    {"my",      "Alias for mycall",                  NULL,          &cmd_mycall,  NULL},
    {"testtx",  "Send test packet",                  "[message]",   &cmd_testtx,  NULL},
    {"uisend",  "Send UI information",               "<call>",      &cmd_uisend,  NULL},
    {"uimode",  "Set UI mode",                       "<mode>",      &cmd_uimode,  NULL},
    {"led",     "Control LED (on/off)",              "<on|off>",    &cmd_led,     NULL},
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
