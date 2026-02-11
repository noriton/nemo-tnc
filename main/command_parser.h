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
#include "tnc_pc_port.h"

/**
 * @brief コマンド解析タスク本体
 * コマンド入力として受け取ったデータを処理し、エコーバックやコマンド実行を行います。
 */
void process_command_input(tnc_pc_port_t *port, uint8_t *data, size_t len);

/**
 * @brief コマンドパーサーの初期化
 * コンソールの初期化、コマンドの登録、Mutexの作成を行います。
 */
void command_parser_init(void);

#endif // COMMAND_PARSER_H