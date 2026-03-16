#ifndef TNC_PC_PORT_H
#define TNC_PC_PORT_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h" // TickType_tのために必要

#include "kiss_parser.h"
#define MAX_MYCALL_LIST 8


// --- トランスペアレントモード用設定 ---
#define TNC_PACLEN  256          // 最大パケット長 (バイト)
#define TNC_PACTIME 500          // パケット送信待ち時間 (ms) - データが途切れてから送信するまでの時間

typedef enum tnc_port_mode {
    PORT_MODE_COMMAND,          // コマンドモード (コマンドパーサで処理)
    PORT_MODE_TRANSPORT,        // トランスペアレントモード  (受信データを長さとタイマーで分割して送信)
    PORT_MODE_KISS,             // KISSバイナリモード（KISSパケットを分解しAX.25フレーム送信）
    PORT_MODE_UICHAT,           // UIチャットモード (改行文字で区切られた文字列を生フレームとして送信)
//  PORT_MODE_PPP,              // PPPモード（検討中）USBシリアル越しにPPPサーバに見せる（要PPP実装）
    PORT_MODE_BRIDGE,           // ブリッジモード （別ポートに各プロトコル段で送受信を相互に接続。プロトコルテスト用）
    PORT_MODE_LOOPBACK,         // ループバックモード （同一ポートにデータをそのまま送り返す。テスト用）
    PORT_MODE_MAX               // モード数のカウント用兼番兵（新モード追加時はこれより前に挿入）
} tnc_port_mode_t;

typedef struct mode_info {
    tnc_port_mode_t mode;
    const char *nvs_str;   // NVS保存用 (例: "KISS")
    const char *desc;      // 画面表示用 (例: "KISS Mode")
} mode_info_t;

#include "frame_metadata.h"
// tnc_pc_port.cのみ実体宣言、それ以外のファイルでは extern で参照
#ifdef TNC_PC_PORT_C
const mode_info_t tnc_mode_table[] = {
    { PORT_MODE_COMMAND,   "CMD",  "Command Mode" },
    { PORT_MODE_TRANSPORT, "DATA", "Transport Mode" },
    { PORT_MODE_KISS,      "KISS", "KISS Mode" },
//    { PORT_MODE_PPP,       "PPP",  "PPP Mode" },
    { PORT_MODE_UICHAT,    "UI",   "UI Chat Mode" },
    { PORT_MODE_BRIDGE,    "BRDG", "Bridge Mode" },
    { PORT_MODE_LOOPBACK,  "LOOP", "Loopback Mode" }
};
#else
extern const mode_info_t tnc_mode_table[];
#endif



typedef enum escape_state {
    ESC_STATE_IDLE,    // 無音待ち（1秒以上経過を待っている）
    ESC_STATE_READY,   // 無音確認済み（'+++' を受け入れ可能）
    ESC_STATE_DETECTED // '+++' 受信済み（その後の1秒無音を待っている）
} escape_state_t;

typedef struct tnc_port_info {
    int id;                    // ポート番号 (0 or 1) - 将来拡張でさらにポート増設可能とする
    int peer_id;               // ループバックや相互接続時の接続先のポート番号

    tnc_port_mode_t mode;      // 現在のモード
    RingbufHandle_t from_pc;   // PC -> TNC (受信)
    RingbufHandle_t to_pc;     // TNC -> PC (送信)
    RingbufHandle_t send_tx;   // 無線側への送信リングバッファ 
    RingbufHandle_t recv_rx;   // 無線側からの受信リングバッファ

    int mycall_idx;         // 当該ポートのMYCALLへのインデックス
                            // NVSに保存して起動時に復元する

    SemaphoreHandle_t mutex;   // コマンド解析部の保護用のミューテックス

    // --- コマンド/チャットモード用のバッファと状態 ---  
    char line_buf[256];        // コマンド,チャット用の一行バッファ
    int line_pos;
    // --- コマンドヒストリ ---
#define CMD_HISTORY_SIZE 8
    char history[CMD_HISTORY_SIZE][256]; // 循環バッファ
    int  hist_head;    // 次に書き込む位置
    int  hist_count;   // 保存済みエントリ数
    int  hist_pos;     // ブラウズ位置 (-1=ブラウズ中でない)
    char hist_saved[256]; // ブラウズ開始時の入力を退避
    int  hist_saved_pos;
    uint8_t ansi_state;   // ANSIエスケープ解析状態 (0=通常 1=ESC受信 2=ESC[受信)

    // --- トランスペアレントモード用に追加 ---
    uint8_t trans_buf[TNC_PACLEN]; // 送信待ちデータバッファ
    int trans_len;                 // 現在溜まっているバイト数
    TickType_t last_rx_tick;       // 最後にデータを受信した時刻
    escape_state_t esc_state;

    kiss_context_t kiss_ctx;
} tnc_port_info_t;

void tnc_pc_ports_init(void);
void enqueue_ui_packet(tnc_port_info_t *port, uint8_t *payload, size_t len, char *dest_call); // TODO: 場所はとりあえず暫定

#endif /* TNC_PC_PORT_H */