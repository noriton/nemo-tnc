#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief コマンド解析タスク本体
 * * リングバッファからデータを吸い出し、コマンド解析やポート間のエコーバックを行います。
 */
/**
 * @brief コンソールコマンドを登録する関数
 * pc_interface から呼び出されます。
 */
void register_commands(void);

// void command_parser_init(void); // 廃止


#endif // COMMAND_PARSER_H