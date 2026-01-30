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
#include "indicator.h"


// --- メイン関数 ---

void app_main(void)
{
    ESP_LOGI(TAG, "FX.25 TNC with Command mode by JH1FBM");

    tnc_init();  // TNC 初期化処理
    ESP_LOGI(TAG, "TNC initialized. System running.");
    indicator_set_color(0, 0, 0); 

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

}


