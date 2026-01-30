/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//#include "freertos/queue.h"
//#include "tinyusb.h"



#include "sdkconfig.h"

#define TAGNAME "NEMO_TNC"

#include "nemo_tnc.h"

// --- メイン関数 ---

void app_main(void)
{
    tnc_init();  // TNC 初期化処理
    
    ESP_LOGI(TAG, "Starting NEMO-TNC (ESP32S3) by JH1FBM ...");

    // メインループ
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

