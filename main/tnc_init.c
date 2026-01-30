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

void tnc_init(void)
{
    usb_init(); // USB 初期化処理の呼び出し
}

