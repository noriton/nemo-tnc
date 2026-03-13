#include "frame_metadata.h"
#include "tnc_pc_port.h"


void ax25_tx_task(void *pvParameters) {
    size_t item_size;
    uint8_t *item_ptr;

    for (;;) {
        item_ptr = (uint8_t *)xRingbufferReceive(ax25_packet_queue, &item_size, portMAX_DELAY);

        if (item_ptr != NULL) {
            tnc_meta_header_t *meta = (tnc_meta_header_t *)item_ptr;

            // バージョンチェックやヘッダサイズの検証
            if (meta->version == TNC_META_VERSION_1) {
                // ペイロードの先頭ポインタを算出
                uint8_t *payload_ptr = item_ptr + meta->header_len;
                
                // 種別に応じた処理
                if (meta->type == META_TYPE_DATA_KISS) {
                    // KISSデータの処理 (CRC付加など)
                    process_kiss_tx(payload_ptr, meta->payload_len, meta);
                } else if (meta->type == META_TYPE_DATA_UI) {
                    // UIチャットデータの処理
                    process_ui_tx(payload_ptr, meta->payload_len, meta);
                }
            }

            vRingbufferReturnItem(ax25_packet_queue, (void *)item_ptr);
        }
    }
}