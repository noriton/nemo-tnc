#ifndef PC_INTERFACE_H
#define PC_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

void pc_interface_init(void);

// 現在コマンドを実行中のポートと同じポートに送信（エコー、プロンプト、進捗表示用）
void pc_write_feedback(const uint8_t *data, size_t len);

// 現在コマンドを実行中のポートとは *別の* ポートに送信（実行結果用）
void pc_write_result(const uint8_t *data, size_t len);

#endif // PC_INTERFACE_H
