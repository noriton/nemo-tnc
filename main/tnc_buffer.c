#include "esp_log.h"
#include "freertos/ringbuf.h"

RingbufHandle_t usb_rx_ringbuf;


void packet_parser_task(void *pvParameters) {
    while (1) {
        size_t received_size;
        // バッファからデータが来るのを待つ（最大100ms待機）
        uint8_t *data = (uint8_t *)xRingbufferReceiveUpTo(usb_rx_ringbuf, &received_size, pdMS_TO_TICKS(100), 128);

        if (data != NULL) {
            // ここで解析を行う（今はとりあえず表示するだけ）
            ESP_LOGI("PARSER", "Received %d bytes from RingBuffer", received_size);
            for(int i = 0; i < received_size; i++) {
                printf("%02X ", data[i]); // 16進数でダンプ
            }
            printf("\n");

            // ★重要：使い終わったメモリをバッファに返却する
            vRingbufferReturnItem(usb_rx_ringbuf, (void *)data);
        }
    }
}

void tnc_buffer_init(void) {
    // 2048バイトの「バイトバッファ」を作成
    // RINGBUF_TYPE_BYTEBUF は、任意の長さのバイナリを流し込むのに適しています
    usb_rx_ringbuf = xRingbufferCreate(2048, RINGBUF_TYPE_BYTEBUF);

    if (usb_rx_ringbuf != NULL) {
        // 2. パケット解析タスクを起動
        // スタックサイズは解析処理が増えることを見越して 4096 程度確保
        // 優先度は USB 受信(コールバック)より低く、Indicatorより少し高めに設定（例: 10）
        xTaskCreate(packet_parser_task, "packet_parser", 4096, NULL, 10, NULL);

    } else {
        ESP_LOGE("BUFFER", "リングバッファの作成に失敗しました");
    }

}