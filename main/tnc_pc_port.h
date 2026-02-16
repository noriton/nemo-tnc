#ifndef TNC_PC_PORT_H
#define TNC_PC_PORT_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h" // TickType_tのために必要

// --- トランスペアレントモード用設定 ---
#define TNC_PACLEN  256          // 最大パケット長 (バイト)
#define TNC_PACTIME 500          // パケット送信待ち時間 (ms) - データが途切れてから送信するまでの時間
typedef enum {
    PORT_MODE_COMMAND,      // コマンドモード (コマンドパーサで処理)
    PORT_MODE_UICHAT,       // UIチャットモード (改行文字で区切られた文字列を生フレームとして送信)
    PORT_MODE_TRANSPORT,    // トランスペアレントモード  (受信データを長さとタイマーで分割して送信)
    PORT_MODE_KISS,         // KISSバイナリモード（KISSパケットを分解しAX.25フレーム送信）
    PORT_MODE_PPP,          // PPPモード（検討中）USBシリアル越しにPPPサーバに見せる
    PORT_MODE_TURNBACK,     // 同一ポートのtoPCに分割して折り返し（トランスペアレント）
    PORT_MODE_BRIDGE        // 別のポートと接続
} tnc_port_mode_t;

typedef struct {
    int id;                    // ポート番号 (0 or 1)
    tnc_port_mode_t mode;      // 現在のモード
    RingbufHandle_t rx_rb;     // PC -> TNC (受信)
    RingbufHandle_t tx_rb;     // TNC -> PC (送信)
    char line_buf[128];        // コマンド用の一行バッファ
    int line_pos;
    // --- トランスペアレントモード用に追加 ---
    uint8_t trans_buf[TNC_PACLEN]; // 送信待ちデータバッファ
    int trans_len;                 // 現在溜まっているバイト数
    TickType_t last_rx_tick;       // 最後にデータを受信した時刻
} tnc_pc_port_t;

void tnc_pc_ports_init(void);

#endif /* TNC_PC_PORT_H */