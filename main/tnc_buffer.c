#include "esp_log.h"
#include "freertos/ringbuf.h"


RingbufHandle_t usb_from_pc[2];
RingbufHandle_t usb_to_pc[2];
RingbufHandle_t rx_ringbuf[2];
RingbufHandle_t tx_ringbuf[2];


void tnc_buffer_init(void)
{
    // PC <-> TNC 間のバイトストリームバッファ
    usb_from_pc[0] = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);
    usb_from_pc[1] = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);
    usb_to_pc[0]   = xRingbufferCreate(4096, RINGBUF_TYPE_BYTEBUF);
    usb_to_pc[1]   = xRingbufferCreate(4096, RINGBUF_TYPE_BYTEBUF);

    // NOSPLIT: 1アイテム = 1パケット（メタヘッダ + ペイロード境界を保証）
    // TNC内部 → tnc_pc_port への統合キュー（MON_TEXT / TX_MON / RX_FRAME 等）
    rx_ringbuf[0] = xRingbufferCreate(4096, RINGBUF_TYPE_NOSPLIT);
    rx_ringbuf[1] = xRingbufferCreate(4096, RINGBUF_TYPE_NOSPLIT);

    // NOSPLIT: 1アイテム = 1パケット（メタヘッダ + ペイロード境界を保証）
    // tnc_pc_port → rawpacket への送信キュー
    tx_ringbuf[0] = xRingbufferCreate(4096, RINGBUF_TYPE_NOSPLIT);
    tx_ringbuf[1] = xRingbufferCreate(4096, RINGBUF_TYPE_NOSPLIT);

    if (usb_from_pc[0] == NULL || usb_from_pc[1] == NULL
        || usb_to_pc[0] == NULL || usb_to_pc[1] == NULL
        || rx_ringbuf[0] == NULL || rx_ringbuf[1] == NULL
        || tx_ringbuf[0] == NULL || tx_ringbuf[1] == NULL) {
        ESP_LOGE("BUFFER", "リングバッファの作成に失敗しました");
    }
}