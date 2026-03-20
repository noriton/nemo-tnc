#include "packet_monitor.h"
#include "tnc_buffer.h"
#include "rawpacket.h"
#include "frame_metadata.h"
#include "ax25.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PKT_MON";


// ---------------------------------------------------------------------------
// タスク本体
// ---------------------------------------------------------------------------

static void packet_monitor_task(void *pvParameters)
{
    size_t item_size;

    for (;;) {
        raw_tx_item_t *item = (raw_tx_item_t *)xRingbufferReceive(
            raw_tx_buf, &item_size, portMAX_DELAY);

        if (item == NULL) {
            continue;
        }

        int    port_id   = item->meta.port_id;
        size_t frame_len = item->meta.payload_len;

        // メタデータをコピーして type を META_TYPE_RX_FRAME に変更し、
        // 生 AX.25 フレームとともに rx_to_pc へ投入する
        uint8_t pkt[sizeof(tnc_meta_header_t) + RAW_PACKET_MAX_LEN_WITH_FCS];
        tnc_meta_header_t *hdr = (tnc_meta_header_t *)pkt;
        *hdr             = item->meta;                       // src/dest/digi 等を引き継ぐ
        hdr->type        = META_TYPE_RX_FRAME;
        hdr->header_len  = (uint16_t)sizeof(tnc_meta_header_t);
        hdr->payload_len = (uint16_t)frame_len;
        memcpy(pkt + sizeof(tnc_meta_header_t), item->data, frame_len);

        xRingbufferSend(rx_to_pc[port_id], pkt,
                        sizeof(tnc_meta_header_t) + frame_len, pdMS_TO_TICKS(100));

        vRingbufferReturnItem(raw_tx_buf, (void *)item);
    }
}


void packet_monitor_init(void)
{
    xTaskCreate(packet_monitor_task, "pkt_mon", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "packet_monitor_init done");
}
