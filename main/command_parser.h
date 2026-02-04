#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief コマンド解析タスク本体
 * * リングバッファからデータを吸い出し、コマンド解析やポート間のエコーバックを行います。
 */
// void command_parser_task(void *pvParameters);

/**
 * @brief (オプション) 解析タスクの初期化や起動を管理する関数
 * app_mainでの記述をシンプルにしたい場合に用意します。
 */
void command_parser_init(void);

#endif // COMMAND_PARSER_H