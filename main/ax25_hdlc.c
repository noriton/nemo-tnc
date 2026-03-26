#include "ax25_hdlc.h"
#include <string.h>

// ---------------------------------------------------------------------------
// ビット書き込みヘルパー（LSBファースト）
// ---------------------------------------------------------------------------

/**
 * バッファの bit_pos 番目のビットに bit を書き込む
 * bit_pos はバッファ先頭からの絶対ビット位置
 * out_buf[0] の bit0 = 位置0（最初に送信されるビット）
 */
static inline int write_bit(uint8_t *buf, size_t buf_size,
                              size_t *bit_pos, uint8_t bit)
{
    size_t byte_idx = *bit_pos >> 3;
    size_t bit_idx  = *bit_pos & 7;
    if (byte_idx >= buf_size) return -1;
    if (bit) {
        buf[byte_idx] |=  (uint8_t)(1u << bit_idx);
    } else {
        buf[byte_idx] &= ~(uint8_t)(1u << bit_idx);
    }
    (*bit_pos)++;
    return 0;
}

/**
 * 1バイトを LSBファーストで書き込む（フラグ用、ビットスタッフィングなし）
 */
static inline int write_byte_raw(uint8_t *buf, size_t buf_size,
                                  size_t *bit_pos, uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        if (write_bit(buf, buf_size, bit_pos, (byte >> i) & 1u) != 0) return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// HDLCフレーミング本体
// ---------------------------------------------------------------------------

size_t ax25_hdlc_frame(const uint8_t *in_data, size_t in_len,
                        uint16_t preamble,
                        uint8_t *out_buf, size_t out_buf_size,
                        size_t *out_bits, uint8_t *out_pad_bits)
{
    if (in_data == NULL || out_buf == NULL || out_buf_size == 0) return 0;

    memset(out_buf, 0, out_buf_size);
    size_t bit_pos = 0;

    // 1. プリアンブルフラグ（ビットスタッフィングなし）
    for (uint16_t i = 0; i < preamble; i++) {
        if (write_byte_raw(out_buf, out_buf_size, &bit_pos, HDLC_FLAG) != 0) return 0;
    }

    // 2. 開始フラグ（ビットスタッフィングなし）
    if (write_byte_raw(out_buf, out_buf_size, &bit_pos, HDLC_FLAG) != 0) return 0;

    // 3. データ + ビットスタッフィング
    int ones = 0;   // 直前の連続する1ビット数
    for (size_t i = 0; i < in_len; i++) {
        for (int b = 0; b < 8; b++) {
            uint8_t bit = (in_data[i] >> b) & 1u;

            if (write_bit(out_buf, out_buf_size, &bit_pos, bit) != 0) return 0;

            if (bit) {
                ones++;
                if (ones == HDLC_STUFF_LEN) {
                    // 5連続1の後にスタッフビット0を挿入
                    if (write_bit(out_buf, out_buf_size, &bit_pos, 0) != 0) return 0;
                    ones = 0;
                }
            } else {
                ones = 0;
            }
        }
    }

    // 4. 終了フラグ（ビットスタッフィングなし）
    if (write_byte_raw(out_buf, out_buf_size, &bit_pos, HDLC_FLAG) != 0) return 0;

    // 5. バイト境界へのパディング（0ビットでフィル）
    //    write_bit は 0 フィル済みバッファに書いているので、
    //    1 ビットを書く必要はなく、bit_pos を進めるだけでよい
    uint8_t pad = 0;
    if (bit_pos & 7u) {
        pad = (uint8_t)(8u - (bit_pos & 7u));
        bit_pos += pad;   // memset(0) 済みなので書き込み不要
    }

    if (out_bits)     *out_bits     = bit_pos;
    if (out_pad_bits) *out_pad_bits = pad;

    return bit_pos >> 3;  // バイト数 = ビット数 / 8（パディング後は常に整数）
}
