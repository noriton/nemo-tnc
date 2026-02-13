#include "esp_log.h"
#include "freertos/ringbuf.h"


RingbufHandle_t usb_from_pc[2];
RingbufHandle_t usb_to_pc[2];
RingbufHandle_t rx_ringbuf[2];
RingbufHandle_t tx_ringbuf[2];


void tnc_buffer_init(void) {
    // 2048バイトの「バイトバッファ」を作成
    // RINGBUF_TYPE_BYTEBUF は、任意の長さのバイナリを流し込むのに適しています

    usb_from_pc[0] = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);
    usb_from_pc[1] = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);
    usb_to_pc[0] = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);
    usb_to_pc[1] = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);

    rx_ringbuf[0] = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);
    rx_ringbuf[1] = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);

    tx_ringbuf[0] = xRingbufferCreate(4096, RINGBUF_TYPE_BYTEBUF);
    tx_ringbuf[1] = xRingbufferCreate(4096, RINGBUF_TYPE_BYTEBUF);

    if (usb_from_pc[0] == NULL || usb_from_pc[1] == NULL 
        || usb_to_pc[0] == NULL || usb_to_pc[1] == NULL 
        || rx_ringbuf[0] == NULL || rx_ringbuf[1] == NULL 
        || tx_ringbuf[0] == NULL || tx_ringbuf[1] == NULL) {
        ESP_LOGE("BUFFER", "リングバッファの作成に失敗しました");
    }

}