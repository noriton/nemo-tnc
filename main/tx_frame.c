#include "tx_frame.h"
#include "tnc_buffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"

static const char *TAG = "TX_FRAME";

esp_err_t tx_frame_enqueue(const uint8_t *frame, size_t len) {
    if (tx_ringbuf == NULL) {
        ESP_LOGE(TAG, "TX Ringbuffer not initialized");
        return ESP_FAIL;
    }

    // Send data to ring buffer
    // xRingbufferSend guarantees that all bytes are copied, or none if insufficient space (unless wait time is used).
    // Using portMAX_DELAY to wait until space is available, or a shorter timeout if preferred.
    // For now, let's use a small timeout to avoid blocking indefinitely if something is wrong.
    BaseType_t res = xRingbufferSend(tx_ringbuf[ifp], frame, len, pdMS_TO_TICKS(100));

    if (res != pdTRUE) {
        ESP_LOGE(TAG, "Failed to enqueue frame (buffer full?)");
        return ESP_FAIL;
    }

    return ESP_OK;
}
