#ifndef KISS_PARSER_H
#define KISS_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "frame_metadata.h" // メタデータ定義などを参照

// --- KISS Protocol Constants ---
#define KISS_FEND  0xC0
#define KISS_FESC  0xDB
#define KISS_TFEND 0xDC
#define KISS_TFESC 0xDD

#define KISS_MAX_RAW_LEN 512 // 1パケットの最大長


// --- Parser Context ---
typedef struct kiss_context {
    uint8_t buf[KISS_MAX_RAW_LEN];
    int idx;
    int escape_mode;
    int in_frame;
} kiss_context_t;

// --- API ---

/**
 * @brief KISS パーサーコンテキストを初期化する
 *
 * @param ctx 初期化対象のパーサーコンテキスト
 */
void kiss_init(kiss_context_t *ctx);

/**
 * @brief KISS ストリームを解析し、完成したフレームをリングバッファへ投入する
 *
 * FEND で区切られた KISS フレームを逐次処理し、コマンドバイトが 0x00
 * （データフレーム）であれば tnc_meta_header_t を付けて target_rb へ送出する。
 * バイトスタッフィング（FESC/TFEND/TFESC）を自動解除する。
 *
 * @param ctx       パーサーコンテキスト（呼び出し間で状態を保持）
 * @param data      受信データバッファ
 * @param len       data のバイト数
 * @param port_id   メタデータに記録するポート番号
 * @param target_rb 完成フレームの投入先 No-Split リングバッファ
 */
void kiss_process_stream(kiss_context_t *ctx, const uint8_t *data, size_t len, int port_id, RingbufHandle_t target_rb);

#endif // KISS_PARSER_H