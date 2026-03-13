#ifndef TNC_PC_PORT_H
#define TNC_PC_PORT_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h" // TickType_tのために必要

#include "kiss_parser.h"

// --- トランスペアレントモード用設定 ---
#define TNC_PACLEN  256          // 最大パケット長 (バイト)
#define TNC_PACTIME 500          // パケット送信待ち時間 (ms) - データが途切れてから送信するまでの時間
typedef enum tnc_port_mode{
    PORT_MODE_UICHAT,       // UIチャットモード (改行文字で区切られた文字列を生フレームとして送信)
    PORT_MODE_TRANSPORT,    // トランスペアレントモード  (受信データを長さとタイマーで分割して送信)
    PORT_MODE_KISS,         // KISSバイナリモード（KISSパケットを分解しAX.25フレーム送信）
//    PORT_MODE_PPP,          // PPPモード（検討中）USBシリアル越しにPPPサーバに見せる（要PPP実装）
//    PORT_MODE_TURNBACK,     // 同一ポートのtoPCに分割して折り返し（デバッグ用）
    PORT_MODE_LOOPBACK,        // 別のポートと相互に接続（デバッグ用）
    PORT_MODE_COMMAND     // コマンドモード (コマンドパーサで処理)
} tnc_port_mode_t;

typedef enum escape_state{
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
    RingbufHandle_t send_tx;   // 無線側への送信リングバッファ (モードによってはto_pcと同じ)
    RingbufHandle_t recv_rx;   // 無線側からの受信リングバッファ (モードによってはfrom_pcと同じ)
    SemaphoreHandle_t mutex;   // ポート状態保護用のミューテックス

    // --- コマンド/チャットモード用のバッファと状態 ---  
    char line_buf[256];        // コマンド,チャット用の一行バッファ
    int line_pos;
    // --- トランスペアレントモード用に追加 ---
    uint8_t trans_buf[TNC_PACLEN]; // 送信待ちデータバッファ
    int trans_len;                 // 現在溜まっているバイト数
    TickType_t last_rx_tick;       // 最後にデータを受信した時刻
    escape_state_t esc_state;

    kiss_context_t kiss_ctx;
} tnc_port_info_t;

void tnc_pc_ports_init(void);

#endif /* TNC_PC_PORT_H */