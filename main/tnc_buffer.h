#ifndef TNC_BUFFER_H
#define TNC_BUFFER_H

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

extern RingbufHandle_t usb_rb[2]; // 配列にしておくと管理が楽です
extern RingbufHandle_t rx_ringbuf[2];
extern RingbufHandle_t tx_ringbuf[2];

void tnc_buffer_init(void);

#endif