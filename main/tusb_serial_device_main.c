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
#include "freertos/queue.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "sdkconfig.h"
#include "tusb_cdc_acm.h"
#include "tusb.h"

static const char *TAG = "NEMO_TNC";

// --- USB インターフェース番号の定義 ---
enum {
    ITF_NUM_CDC_0 = 0,
    ITF_NUM_CDC_0_DATA,
    ITF_NUM_CDC_1,
    ITF_NUM_CDC_1_DATA,
    ITF_NUM_TOTAL
};

// --- USB ディスクリプタ定義 (2ポート構成) ---
#define TUSB_DESC_CONFIG_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_CDC_DESC_LEN)

static const uint8_t const_config_desc[] = {
    // 構成ヘッダー: インターフェース総数は 4 (CDCポートあたり2つ使用)
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_CONFIG_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Port 0 (Command用): ITFs 0 & 1, Endpoints 0x81, 0x82/0x02
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_0, 4, 0x81, 8, 0x82, 0x02, 64),

    // Port 1 (Data用): ITFs 2 & 3, Endpoints 0x83, 0x84/0x04
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_1, 5, 0x83, 8, 0x84, 0x04, 64)
};

static const char* const_string_desc[] = {
    (const char[]) { 0x09, 0x04 }, // 0: Language English
    "JH1FBM",                      // 1: Manufacturer
    "NEMO-TNC-S3a",                // 2: Product
    "SN00001a",                    // 3: Serial
    "TNC-Command",                 // 4: Port 0 Name
    "TNC-Data-KISS"                // 5: Port 1 Name
};

// --- 受信コールバック関数 ---

// ポート0: コマンド入力用 (エコーバック実装)
static void command_port_rx_callback(int itf, cdcacm_event_t *event) {
    size_t rx_size = 0;
    uint8_t buf[64];
    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, sizeof(buf), &rx_size);
    if (ret == ESP_OK && rx_size > 0) {
        // テスト用: 受信した文字をそのまま打ち返す(Echo)
        tinyusb_cdcacm_write_queue(itf, buf, rx_size);
        tinyusb_cdcacm_write_flush(itf, 0);
        ESP_LOGI(TAG, "Port 0 (Command) received: %.*s", rx_size, buf);
    }
}

// ポート1: KISSデータ用
static void data_port_rx_callback(int itf, cdcacm_event_t *event) {
    size_t rx_size = 0;
    uint8_t buf[512];
    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, sizeof(buf), &rx_size);
    if (ret == ESP_OK && rx_size > 0) {
        // ここにKISSパケット処理を記述
        ESP_LOGI(TAG, "Port 1 (Data) received %d bytes", rx_size);
    }
}

// --- メイン関数 ---

void app_main(void)
{
    ESP_LOGI(TAG, "Starting NEMO-TNC (ESP32S3) by JH1FBM ...");

    // 1. TinyUSB メイン構成の設定 (v5.5.2 対応)
    const tinyusb_config_t tusb_cfg = {
        .port = TINYUSB_PORT_FULL_SPEED_0, 
        .phy = {
            .skip_setup = 0,
            .self_powered = 0,
            .vbus_monitor_io = -1,
        },
        .task = {
            .size = 4096,
            .priority = 5,
            .xCoreID = 1,
        },
        .descriptor = {
            .device = NULL,
            .string = const_string_desc,
            .string_count = sizeof(const_string_desc) / sizeof(const_string_desc[0]),
            .full_speed_config = const_config_desc,
            .high_speed_config = NULL,
        },
        .event_cb = NULL,
        .event_arg = NULL
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    // 2. CDC ポート 0 (Command) の初期化
    tinyusb_config_cdcacm_t acm_cfg_0 = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = &command_port_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL
    };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg_0));

    // 3. CDC ポート 1 (Data/KISS) の初期化
    tinyusb_config_cdcacm_t acm_cfg_1 = {
        .cdc_port = TINYUSB_CDC_ACM_1,
        .callback_rx = &data_port_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL
    };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg_1));

    ESP_LOGI(TAG, "USB Dual CDC installation complete.");

    // メインループ
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

