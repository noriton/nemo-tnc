#include "rx_frame.h"
#include "tnc_buffer.h"
#include "rawpacket.h"
#include "ax25.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "RX_FRAME";

// Hex ダンプの最大バッファサイズ
// ヘッダ(20) + バイトあたり3文字 × RAW_PACKET_MAX_LEN_WITH_FCS(330) + 改行分 + 末尾改行
#define HEX_DUMP_BUF_SIZE (20 + RAW_PACKET_MAX_LEN_WITH_FCS * 3 + 44 + 2)


// ---------------------------------------------------------------------------
// 1. Hex ダンプ出力
// ---------------------------------------------------------------------------

static void rx_frame_dump_hex(int port_id, const uint8_t *frame, size_t len)
{
    char buf[HEX_DUMP_BUF_SIZE];
    int pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - pos, "\r\n--- RX Frame ---\r\n");

    for (size_t i = 0; i < len && pos + 4 < (int)sizeof(buf); i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X ", frame[i]);
        if ((i + 1) % 16 == 0) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, "\r\n");
        }
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "\r\n");

    xRingbufferSend(usb_to_pc[port_id], (uint8_t *)buf, pos, pdMS_TO_TICKS(100));
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
        char msg[300];
        int n = snprintf(msg, sizeof(msg),
                         "Decoded: %s\r\nFCS:%s\r\n", decoded_info, fcs_result);
        xRingbufferSend(usb_to_pc[port_id], (uint8_t *)msg, n, pdMS_TO_TICKS(100));
    } else {
        char msg[48];
        int n = snprintf(msg, sizeof(msg),
                         "Decode Failed: %d\r\nFCS:%s\r\n", decoded_len, fcs_result);
        xRingbufferSend(usb_to_pc[port_id], (uint8_t *)msg, n, pdMS_TO_TICKS(100));
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

        int port_id          = item->meta.port_id;
        const uint8_t *frame = item->data;
        size_t len           = item->meta.payload_len;

        rx_frame_dump_hex(port_id, frame, len);
        bool fcs_ok = ax25_fcs_verify(frame, len);
        rx_frame_decode_and_print(port_id, frame, len, fcs_ok);

        vRingbufferReturnItem(raw_tx_buf, (void *)item);
    }
}


void rx_frame_init(void)
{
    xTaskCreate(rx_frame_task, "rx_frame_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "RX Frame task started");
}
