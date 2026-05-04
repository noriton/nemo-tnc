#ifndef TNC_BUFFER_H
#define TNC_BUFFER_H

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

extern RingbufHandle_t usb_from_pc[2]; // PC -> TNC (BYTEBUF)
extern RingbufHandle_t usb_to_pc[2];   // TNC -> PC (BYTEBUF)
extern RingbufHandle_t rx_ringbuf[2];  // TNC内部 → tnc_pc_port (NOSPLIT, メタ+ペイロード形式)
                                       // 投入元: rawpacket(MON_TEXT), packet_monitor(TX_MON),
                                       //         radio受信(RX_FRAME, 将来)
extern RingbufHandle_t tx_ringbuf[2];  // tnc_pc_port → rawpacket (NOSPLIT)
extern RingbufHandle_t afsk_tx_buf;    // rawpacket → AFSKモデム (NOSPLIT, raw_tx_item_t)

void tnc_buffer_init(void);

#endif