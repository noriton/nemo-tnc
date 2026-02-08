#include "esp_log.h"
#include "freertos/ringbuf.h"

//RingbufHandle_t usb_rx_ringbuf;
RingbufHandle_t usb_rb[2];
RingbufHandle_t tx_ringbuf;

//void tnc_buffer_init(void) {
//    usb_rb[0] = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);
//    usb_rb[1] = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);
//}

void tnc_buffer_init(void) {
    // 2048バイトの「バイトバッファ」を作成
    // RINGBUF_TYPE_BYTEBUF は、任意の長さのバイナリを流し込むのに適しています
//    usb_rx_ringbuf = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);
    usb_rb[0] = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);
    usb_rb[1] = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);
    tx_ringbuf = xRingbufferCreate(4096, RINGBUF_TYPE_BYTEBUF);

    if (usb_rb[0] == NULL || usb_rb[1] == NULL || tx_ringbuf == NULL) {
        ESP_LOGE("BUFFER", "リングバッファの作成に失敗しました");
    }

}