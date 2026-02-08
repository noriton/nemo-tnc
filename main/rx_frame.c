#include "rx_frame.h"
#include "tnc_buffer.h"
#include "ax25.h"
#include "tinyusb_cdc_acm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "RX_FRAME";

static void rx_frame_task(void *pvParameters) {
    size_t item_size;
    uint8_t *item;

    while (1) {
        // Receive item from ring buffer
        item = (uint8_t *)xRingbufferReceive(tx_ringbuf, &item_size, portMAX_DELAY);

        if (item != NULL) {
            // 1. Hex Dump to Port 1
            tinyusb_cdcacm_write_queue(1, (uint8_t *)"\r\n--- RX Frame ---\r\n", 18);
            for (size_t i = 0; i < item_size; i++) {
                char hex[4];
                sprintf(hex, "%02X ", item[i]);
                tinyusb_cdcacm_write_queue(1, (uint8_t *)hex, 3);
                if ((i + 1) % 16 == 0) tinyusb_cdcacm_write_queue(1, (uint8_t *)"\r\n", 2);
            }
            tinyusb_cdcacm_write_queue(1, (uint8_t *)"\r\n", 2);

            // 2. Decode and display message
            char decoded_info[256];
            int decoded_len = ax25_decode_ui_info(item, item_size, decoded_info, sizeof(decoded_info));

            if (decoded_len >= 0) {
                char decode_msg[300];
                int d_len = snprintf(decode_msg, sizeof(decode_msg), "Decoded: %s\r\n", decoded_info);
                tinyusb_cdcacm_write_queue(1, (uint8_t *)decode_msg, d_len);
            } else {
                 char err_msg[32];
                 snprintf(err_msg, sizeof(err_msg), "Decode Failed: %d\r\n", decoded_len);
                 tinyusb_cdcacm_write_queue(1, (uint8_t *)err_msg, strlen(err_msg));
            }
            
            tinyusb_cdcacm_write_flush(1, pdMS_TO_TICKS(10));

            // Return item
            vRingbufferReturnItem(tx_ringbuf, (void *)item);
        }
    }
}

void rx_frame_init(void) {
    xTaskCreate(rx_frame_task, "rx_frame_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "RX Frame task started");
}
