#ifndef TX_FRAME_H
#define TX_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * @brief Enqueue a frame for transmission.
 * 
 * @param frame Pointer to the frame data.
 * @param len   Length of the frame data.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on failure (e.g., buffer full).
 */
esp_err_t tx_frame_enqueue(const uint8_t *frame, size_t len);

#endif // TX_FRAME_H
