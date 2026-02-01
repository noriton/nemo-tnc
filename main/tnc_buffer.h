#ifndef TNC_BUFFER_H
#define TNC_BUFFER_H

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

extern RingbufHandle_t usb_rx_ringbuf;

void tnc_buffer_init(void);

#endif