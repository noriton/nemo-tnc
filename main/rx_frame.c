#include "rx_frame.h"
#include "tnc_buffer.h"
#include "rawpacket.h"
#include "ax25.h"
#include "tinyusb_cdc_acm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "RX_FRAME";


// ---------------------------------------------------------------------------
// 1. Hex ダンプ出力
// ---------------------------------------------------------------------------

static void rx_frame_dump_hex(int port_id, const uint8_t *frame, size_t len)
{
    tinyusb_cdcacm_write_queue(port_id,
        (uint8_t *)"\r\n--- RX Frame ---\r\n", 20);
    for (size_t i = 0; i < len; i++) {
        char hex[4];
        sprintf(hex, "%02X ", frame[i]);
        tinyusb_cdcacm_write_queue(port_id, (uint8_t *)hex, 3);
        if ((i + 1) % 16 == 0) {
            tinyusb_cdcacm_write_queue(port_id, (uint8_t *)"\r\n", 2);
        }
    }
    tinyusb_cdcacm_write_queue(port_id, (uint8_t *)"\r\n", 2);
}


// ---------------------------------------------------------------------------
// 3. デコード＆表示
// ---------------------------------------------------------------------------

static void rx_frame_decode_and_print(int port_id, const uint8_t *frame,
                                       size_t len, bool fcs_ok)
{
    const char *fcs_result = fcs_ok ? "OK" : "NG";
    char decoded_info[256];
    int decoded_len = ax25_decode_ui_info(frame, len - 2,  // FCS 2バイトを除外
                                          decoded_info, sizeof(decoded_info));
    if (decoded_len >= 0) {
        char decode_msg[300];
        int d_len = snprintf(decode_msg, sizeof(decode_msg),
                             "Decoded: %s\r\nFCS:%s\r\n", decoded_info, fcs_result);
        tinyusb_cdcacm_write_queue(port_id, (uint8_t *)decode_msg, d_len);
    } else {
        char err_msg[48];
        int e_len = snprintf(err_msg, sizeof(err_msg),
                             "Decode Failed: %d\r\nFCS:%s\r\n", decoded_len, fcs_result);
        tinyusb_cdcacm_write_queue(port_id, (uint8_t *)err_msg, e_len);
    }
}


// ---------------------------------------------------------------------------
// タスク本体
// ---------------------------------------------------------------------------

static void rx_frame_task(void *pvParameters)
{
    size_t item_size;

    for (;;) {
        raw_tx_item_t *item = (raw_tx_item_t *)xRingbufferReceive(
            raw_tx_buf, &item_size, portMAX_DELAY);

        if (item == NULL) {
            continue;
        }

        int port_id            = item->meta.port_id;
        const uint8_t *frame   = item->data;
        size_t len             = item->meta.payload_len;

        rx_frame_dump_hex(port_id, frame, len);
        bool fcs_ok = ax25_fcs_verify(frame, len);
        rx_frame_decode_and_print(port_id, frame, len, fcs_ok);

        tinyusb_cdcacm_write_flush(port_id, pdMS_TO_TICKS(10));
        vRingbufferReturnItem(raw_tx_buf, (void *)item);
    }
}


void rx_frame_init(void)
{
    xTaskCreate(rx_frame_task, "rx_frame_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "RX Frame task started");
}
