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
 * パーサーの初期化
 */
void kiss_init(kiss_context_t *ctx);

/**
 * ストリームデータを解析し、フレームが完成したらメタデータを付けて
 * 指定されたリングバッファ (No-Split) に投入する
 */
void kiss_process_stream(kiss_context_t *ctx, const uint8_t *data, size_t len, int port_id, RingbufHandle_t target_rb);

#endif // KISS_PARSER_H