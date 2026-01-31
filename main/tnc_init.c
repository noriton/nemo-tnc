/*
 * tnc_init.c
 *
 * NEMO-TNC モジュール初期化呼び出し
 *
 * Copyright (c) 2026 by Norito Nemoto, JH1FBM
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
// #include "esp_log.h"

#include "nemo_tnc.h"
#include "indicator.h"

void tnc_init(void)
{
    indicator_init(); // インジケータ初期化
    // 起動中表示(高速点滅) さすがにインジケータ初期化を先にやらざるを得ない
    indicator_set_state(TNC_ST_BOOT); // 起動中状態に設定
    usb_init(); // USB 初期化処理の呼び出し

}

