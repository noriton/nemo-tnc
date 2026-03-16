/*
 * tnc_init.c
 *
 * NEMO-TNC モジュール初期化呼び出し
 *
 * Copyright (c) 2026 by Norito Nemoto, JH1FBM
 */
#define TNC_INIT_C
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nemo_tnc.h"
#include "indicator.h"
#include "tnc_buffer.h"
#include "command_parser.h"

#include "tnc_pc_port.h"
#include "nvs_flash.h"
#include "nvs_if.h"
#include "rx_frame.h"
#include "rawpacket.h"

char mycall[16] = "N0CALL"; // デフォルトコールサイン

void tnc_init(void)
{
    // NVSの初期化（お約束のコード）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (nvs_load_mycall(mycall, sizeof(mycall)) == ESP_OK) {
        ESP_LOGI("TNC", "********************************");
        ESP_LOGI("TNC", "  NEMO-TNC Starting...         ");
        ESP_LOGI("TNC", "  Callsign: %s                 ", mycall);
        ESP_LOGI("TNC", "********************************");
    }

    // ... その他の初期化 (indicator, buffer, etc.) ...
    indicator_init(); // インジケータ初期化
    // 起動中表示(高速点滅) さすがにインジケータ初期化を先にやらざるを得ない
    indicator_set_state(TNC_ST_BOOT); // 起動中状態に設定
    
    tnc_buffer_init(); // TNC用バッファ初期化
    usb_init(); // USB 初期化処理の呼び出し

    command_parser_init();
    tnc_pc_ports_init();


    // その他のTNC全体の初期化処理があればここに追加

    rawpacket_init(); // L3生パケット生成タスク起動

    rx_frame_init(); // RXフレーム受信タスク起動 (現状はLoopback/Debug用)

}

