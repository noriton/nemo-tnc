#include "command_parser.h"
#include "ax25.h"
#include "esp_console.h"
#include "esp_log.h"
#include "nvs_if.h"
#include "tx_frame.h"
#include <stdlib.h>
#include <string.h>


#include "frame_metadata.h" // メタデータ構造体定義を使用

static void register_commands(void);

static SemaphoreHandle_t console_mutex = NULL;

static const char *TAG = "CMD_PARSER";

// --- 各コマンドの実装関数 ---

// --- コマンド応答用ヘルパー ---
static void cmd_response(const char *fmt, ...) {
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
static int cmd_version(int argc, char **argv) {
  cmd_response("\r\nNEMO-TNC v0.1 (esp_console)\r\n");
  return 0;
}

// UIMODE コマンド (修正版)
static int cmd_uimode(int argc, char **argv) {
  cmd_response("\r\nNEMO-TNC v0.1 (esp_console)\r\n");
  return 0;
}

// --- MYCALL コマンドで参照する外部変数 ---
extern char mycall_list[MAX_MYCALL_LIST][16];
extern tnc_port_info_t pc_ports[];

#define MYCALL_DEFAULT "N0CALL-0"

// MYCALL コマンド (サブコマンド: list, set, use, del)
static int cmd_mycall(int argc, char **argv) {
  tnc_port_info_t *port =
      (tnc_port_info_t *)pvTaskGetThreadLocalStoragePointer(NULL, 0);
  if (port == NULL)
    return 1;

  // 引数なし: 現在のポートに割り当てられている MYCALL を表示
  if (argc < 2) {
    int idx = port->mycall_idx;
    cmd_response("\r\nMYCALL[%d]: %s\r\n", idx, mycall_list[idx]);
    return 0;
  }

  // --- list サブコマンド ---
  if (strcmp(argv[1], "list") == 0) {
    cmd_response("\r\nPort %d -> slot %d (%s)\r\n", port->id, port->mycall_idx,
                 mycall_list[port->mycall_idx]);
    for (int i = 0; i < MAX_MYCALL_LIST; i++) {
      cmd_response("  [%d] %s\r\n", i, mycall_list[i]);
    }
    return 0;
  }

  // --- set サブコマンド ---
  if (strcmp(argv[1], "set") == 0) {
    if (argc < 3) {
      cmd_response("Usage: mycall set <CALLSIGN> [0-7|-]\r\n");
      return 1;
    }
    const char *callsign = argv[2];
    int slot;

    if (argc >= 4) {
      if (strcmp(argv[3], "-") == 0) {
        // 空き枠を検索
        slot = -1;
        for (int i = 0; i < MAX_MYCALL_LIST; i++) {
          if (strcmp(mycall_list[i], MYCALL_DEFAULT) == 0) {
            slot = i;
            break;
          }
        }
        if (slot < 0) {
          cmd_response("Error: No empty slot available.\r\n");
          return 1;
        }
      } else {
        slot = atoi(argv[3]);
        if (slot < 0 || slot >= MAX_MYCALL_LIST) {
          cmd_response("Error: Slot must be 0-%d.\r\n", MAX_MYCALL_LIST - 1);
          return 1;
        }
      }
    } else {
      // 番号省略: 現在のポートの枠に上書き
      slot = port->mycall_idx;
    }

    strncpy(mycall_list[slot], callsign, sizeof(mycall_list[slot]) - 1);
    mycall_list[slot][sizeof(mycall_list[slot]) - 1] = '\0';
    nvs_save_mycall_list_item(slot, mycall_list[slot]);

    // "-" 指定時はポートのインデックスも更新
    if (argc >= 4 && strcmp(argv[3], "-") == 0) {
      port->mycall_idx = slot;
      nvs_save_port_mycall_idx(port->id, slot);
    }

    cmd_response("Saved [%d]: %s\r\n", slot, mycall_list[slot]);
    return 0;
  }

  // --- use サブコマンド ---
  if (strcmp(argv[1], "use") == 0) {
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
                 mycall_list[slot]);
    return 0;
  }

  // --- del サブコマンド ---
  if (strcmp(argv[1], "del") == 0) {
    if (argc < 3) {
      cmd_response("Usage: mycall del <0-7>\r\n");
      return 1;
    }
    int slot = atoi(argv[2]);
    if (slot < 0 || slot >= MAX_MYCALL_LIST) {
      cmd_response("Error: Slot must be 0-%d.\r\n", MAX_MYCALL_LIST - 1);
      return 1;
    }
    strncpy(mycall_list[slot], MYCALL_DEFAULT, sizeof(mycall_list[slot]) - 1);
    mycall_list[slot][sizeof(mycall_list[slot]) - 1] = '\0';
    nvs_save_mycall_list_item(slot, mycall_list[slot]);
    cmd_response("Deleted [%d]\r\n", slot);
    return 0;
  }

  cmd_response("Unknown subcommand: %s\r\n", argv[1]);
  return 1;
}

// TESTTX コマンド (tx_frame経由で送信)
static int cmd_testtx(int argc, char **argv) {
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
int cmd_uisend(int argc, char **argv) {
  if (argc < 3) {
    printf("Usage: uisend <DEST_CALL> <MESSAGE>\n");
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

  // --- メタデータ付きパケットの構築 ---
  // ここで重要なのは、後段に「宛先コールサイン」をどう伝えるかです。
  // メタデータ構造体に dest_call 領域を追加するか、
  // ペイロードの先頭に特定のフォーマットで埋め込むのが良いでしょう。

  // 今回は拡張性を考え、メタデータに「宛先指定フラグ」を持たせたと仮定します
  size_t header_len = sizeof(tnc_meta_header_t);
  size_t total_len = header_len + payload_len;

  uint8_t send_tmp[512];
  if (total_len > sizeof(send_tmp)) {
    printf("Error: Message too long\n");
    return 1;
  }

  tnc_meta_header_t *meta = (tnc_meta_header_t *)send_tmp;
  meta->version = TNC_META_VERSION_1;
  meta->type = META_TYPE_DATA_UI;
  meta->header_len = (uint16_t)header_len;
  meta->payload_len = (uint16_t)payload_len;
  meta->port_id = (uint8_t)port->id;

  // 【拡張案】メタデータに宛先コールサインを一時的に保持
  // 構造体に char dest[10] を追加しておくとスムーズです
  strncpy((char *)meta->dest_call, dest_call,
          12); // 12は dest_call のサイズ (11文字 + NULL)
  //    strncpy((char*)meta->src_call, port->my_call, 12); // 12は src_call
  //    のサイズ (11文字 + NULL)
  memcpy(send_tmp + header_len, message, payload_len);

  // 送信キューへ投入
  xRingbufferSend(port->send_tx, send_tmp, total_len, pdMS_TO_TICKS(10));

  // printf("UI Frame queued to %s\n", dest_call);

  // コマンド実行後は自動的にコマンドプロンプトに戻る（esp_consoleの仕様）
  return 0;
}

// --- コマンド解析ロジック ---

void process_command_input(tnc_port_info_t *port, uint8_t *data, size_t len) {
  if (port == NULL || data == NULL)
    return;

  for (size_t i = 0; i < len; i++) {
    uint8_t c = data[i];

    if (c == '\n')
      continue;

    if (c == '\r') {
      const char *crlf = "\r\n";
      xRingbufferSend(port->to_pc, (uint8_t *)crlf, 2, 0);

      if (port->line_pos > 0) {
        port->line_buf[port->line_pos] = '\0';

        // Mutex で保護して実行
        if (xSemaphoreTake(console_mutex, pdMS_TO_TICKS(100))) {
          // ★ここでTLSにポート情報をセット（リファクタリング適用）
          vTaskSetThreadLocalStoragePointer(NULL, 0, port);

          int ret;
          esp_console_run(port->line_buf, &ret);
          xSemaphoreGive(console_mutex);
        } else {
          const char *busy = "Busy\r\n";
          xRingbufferSend(port->to_pc, (uint8_t *)busy, strlen(busy), 0);
        }
      }
      // コマンド実行後、バッファをリセットしてプロンプトを表示
      port->line_pos = 0;
      const char *prompt = "TNC> "; // 既に \r\n しているのでここはシンプルに
      xRingbufferSend(port->to_pc, (uint8_t *)prompt, strlen(prompt), 0);
    } else {
      // Backspace (0x08) や Delete (0x7F) の対応
      if (c == 0x08 || c == 0x7F) {
        if (port->line_pos > 0) {
          port->line_pos--;
          // 画面上の文字を消すエスケープシーケンス "\b \b"
          const char *bs = "\b \b";
          xRingbufferSend(port->to_pc, (uint8_t *)bs, 3, 0);
        }
      } else if (c >= 0x20 && c <= 0x7E) {
        // 表示可能文字のみバッファリング
        if (port->line_pos < sizeof(port->line_buf) - 1) {
          port->line_buf[port->line_pos++] = c;
          // 入力文字をそのままエコーバック
          xRingbufferSend(port->to_pc, &c, 1, 0);
        }
      }
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

static void register_commands(void) {
  // --- 標準のヘルプコマンドを登録  ---
  esp_console_register_help_command();

  // --- その他の自作コマンドを登録 ---
  const esp_console_cmd_t cmds[] = {
      {"VERSION", "Show version", NULL, &cmd_version, NULL},
      {"MYCALL", "Manage and select MYCALL", NULL, &cmd_mycall, NULL},
      {"TESTTX", "Send test packet", "[message]", &cmd_testtx, NULL},
      {"UISEND", "Send UI information", "<call>", &cmd_uisend, NULL},
      {"UIMODE", "Set UI mode", "<mode>", &cmd_uimode, NULL},
  };

  for (int i = 0; i < sizeof(cmds) / sizeof(esp_console_cmd_t); i++) {
    esp_console_cmd_register(&cmds[i]);
  }
}
