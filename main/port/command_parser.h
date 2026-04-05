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
void process_command_input(tnc_port_info_t *port, uint8_t *data, size_t len);

/**
 * @brief コマンドパーサーの初期化
 * コンソールの初期化、コマンドの登録、Mutexの作成を行います。
 */
void command_parser_init(void);

/**
 * @brief コマンドプロンプトを送出する
 * 形式: "PORT<id>:<CALL>-<SSID>> "
 */
void send_prompt(tnc_port_info_t *port);

/**
 * @brief メモリ上の最新ヒストリを NVS にコミットする
 * dirty フラグが立っている場合のみ書き込む。
 * モード遷移・シャットダウン時など明示的に保存したい場面で呼ぶ。
 */
void history_commit(tnc_port_info_t *port);
void hist_push_raw(tnc_port_info_t *port, const char *cmd);

#endif // COMMAND_PARSER_H