#ifndef INDICATOR_H
#define INDICATOR_H

#include <stdint.h>

// TNCの状態定義
typedef enum tnc_state {
    TNC_ST_BOOT,       // 起動中
    TNC_ST_IDLE,       // 待機中（USB接続済み）
    TNC_ST_TX,         // 送信中
    TNC_ST_RX,         // 受信中
    TNC_ST_ERROR,      // エラー発生
    TNC_ST_OFF         // 消灯
} tnc_state_t;

// 初期化
void indicator_init(void);

// GPIO38 のオンボードRGB LED (ESP32-S3-DevKitC-1 v1.1) を一度だけ消灯する
// (I2S配線のノイズ等で意図せず点灯することがあるための対策。GPIO4の外部LEDとは無関係)
void indicator_turn_off_onboard_led(void);

// 状態設定関数
void indicator_set_state(tnc_state_t new_state); // 他のファイルからこれを呼ぶ

// LED強制消灯 (1: 消灯, 0: 通常動作に戻す)
void indicator_set_forced_off(int off);



// 色の設定 (R, G, B: 0-255)
// void indicator_set_color(uint8_t r, uint8_t g, uint8_t b);

// プリセット：状態に応じた色
void indicator_status_usb_conn(void);   // 例：青
void indicator_status_data_rx(void);    // 例：緑
void indicator_status_error(void);      // 例：赤
void indicator_status_off(void);        // 消灯

#endif