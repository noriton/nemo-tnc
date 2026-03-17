/*
 * usb_descriptors.c
 *
 * NEMO-TNC USBディスクリプタ定義と初期化
 *
 * Copyright (c) 2026 by Norito Nemoto, JH1FBM
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

#include "tusb.h"
#include "nemo_tnc.h"
#include "indicator.h"
#include "tnc_buffer.h"

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

static const char *TAG = "USB_DESC";

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
    "TNC-Data"                     // 5: Port 1 Name
};

// usb_to_pc リングバッファのデータをUSBへ送り返すタスク
static void usb_tx_task(void *pvParameters)
{
    for (;;) {
        for (int itf = 0; itf < 2; itf++) {
            size_t size;
            uint8_t *data = (uint8_t *)xRingbufferReceive(usb_to_pc[itf], &size, 0);
            if (data != NULL) {
                // write_queue はCDC TXバッファサイズ分しか受け付けないため
                // 全バイト送り切るまでループする
                size_t sent = 0;
                while (sent < size) {
                    size_t written = tinyusb_cdcacm_write_queue(itf, data + sent, size - sent);
                    tinyusb_cdcacm_write_flush(itf, pdMS_TO_TICKS(20));
                    if (written == 0) vTaskDelay(1); // TXバッファフル時は少し待つ
                    else sent += written;
                }
                vRingbufferReturnItem(usb_to_pc[itf], (void *)data);
            }
        }
        vTaskDelay(1);  // 必ずIDLEにyield
    }
}

// USBポート入力用コールバック (共通化)
void usb_port_rx_callback(int itf, cdcacm_event_t *event)
{
    uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
    size_t rx_size = 0;

    // USBからデータを読み取る
    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, sizeof(buf), &rx_size);
    
    if (ret == ESP_OK && rx_size > 0) {
        // インジケータ制御
        if (itf == 0) {
            indicator_set_state(TNC_ST_RX); //USBデータレシーブポート0
        } else {
            indicator_set_state(TNC_ST_RX); //USBデータレシーブポート1
        }

        // リングバッファへデータを送る (範囲チェック付き)
        if (itf < 2) {
            BaseType_t res = xRingbufferSend(usb_from_pc[itf], buf, rx_size, 0);
            if (res != pdTRUE) {
                ESP_LOGW("USB_RX", "Ringbuffer %d full, data dropped!", itf);
            }
        }
    }
}

void usb_init(void)
{
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

    // CDC ポート 0 の設定
    tinyusb_config_cdcacm_t acm_cfg_0 = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = &usb_port_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg_0));

    // CDC ポート 1 の設定
    tinyusb_config_cdcacm_t acm_cfg_1 = {
        .cdc_port = TINYUSB_CDC_ACM_1,
        .callback_rx = &usb_port_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg_1));

    xTaskCreate(usb_tx_task, "usb_tx", 2048, NULL, 5, NULL);
    ESP_LOGI(TAG, "USB Dual CDC installation complete.");
}
