#ifndef PACKET_PARSER_H
#define PACKET_PARSER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief パケット解析タスク本体
 * * リングバッファからデータを吸い出し、コマンド解析やポート間のエコーバックを行います。
 */
// void packet_parser_task(void *pvParameters);

/**
 * @brief (オプション) 解析タスクの初期化や起動を管理する関数
 * app_mainでの記述をシンプルにしたい場合に用意します。
 */
void packet_parser_init(void);

#endif // PACKET_PARSER_H