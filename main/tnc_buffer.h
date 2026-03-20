#ifndef TNC_BUFFER_H
#define TNC_BUFFER_H

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

extern RingbufHandle_t usb_from_pc[2]; // PC -> TNC
extern RingbufHandle_t usb_to_pc[2];   // TNC -> PC
extern RingbufHandle_t rx_ringbuf[2];
extern RingbufHandle_t tx_ringbuf[2];
extern RingbufHandle_t rx_to_pc[2];    // TNC -> PC 受信側データキュー (NOSPLIT, rawpacket → tnc_pc_port)
// extern RingbufHandle_t mon_ringbuf[2];  // MON出力キュー ※廃止: rx_to_pc へ統合

void tnc_buffer_init(void);

#endif