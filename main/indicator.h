#ifndef INDICATOR_H
#define INDICATOR_H

#include <stdint.h>

// 初期化
void indicator_init(void);

// 色の設定 (R, G, B: 0-255)
void indicator_set_color(uint8_t r, uint8_t g, uint8_t b);

// プリセット：状態に応じた色
void indicator_status_usb_conn(void);   // 例：青
void indicator_status_data_rx(void);    // 例：緑
void indicator_status_error(void);      // 例：赤
void indicator_status_off(void);        // 消灯

#endif