#include "ax25_hdlc.h"
#include "bitstaff.h"
#include <string.h>

// ---------------------------------------------------------------------------
// ビット読み書きヘルパー（LSBファースト）
// ---------------------------------------------------------------------------

/**
 * バッファの in_pos 番目のビットを読む（LSB first）
 */
static inline uint8_t read_bit(const uint8_t *buf, size_t in_pos)
{
    return (buf[in_pos >> 3] >> (in_pos & 7u)) & 1u;
}

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
    // bit_pos はフラグ書き込み後の時点で常にバイト境界に整合している
    int stuffed_bits = hdlc_stuff_frame(in_data, (int)in_len, out_buf + (bit_pos >> 3));
    bit_pos += (size_t)stuffed_bits;

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

// ---------------------------------------------------------------------------
// HDLCデスタッフィング（RX側）
// ---------------------------------------------------------------------------

int ax25_hdlc_destuff(const uint8_t *in_buf, size_t in_bits,
                       uint8_t *out_buf, size_t out_size,
                       size_t *out_len)
{
    if (in_buf == NULL || out_buf == NULL || out_size == 0) return -2;

    memset(out_buf, 0, out_size);

    size_t in_pos  = 0;   // 入力ビット位置
    size_t out_pos = 0;   // 出力ビット位置
    int    ones    = 0;   // 直前の連続する1ビット数

    while (in_pos < in_bits) {
        uint8_t bit = read_bit(in_buf, in_pos++);

        if (bit) {
            ones++;
            if (ones == 6) {
                // 6連続1 = アボートシーケンス
                return -1;
            }
            // 有効な1ビットを出力
            if ((out_pos >> 3) >= out_size) return -2;
            out_buf[out_pos >> 3] |= (uint8_t)(1u << (out_pos & 7u));
            out_pos++;
        } else {
            if (ones == HDLC_STUFF_LEN) {
                // 5連続1の後の0 = スタッフビット: 捨てる
                ones = 0;
            } else {
                // 通常の0ビットを出力（バッファは memset(0) 済み）
                ones = 0;
                if ((out_pos >> 3) >= out_size) return -2;
                out_pos++;  // 0ビットは書き込み不要
            }
        }
    }

    // デスタッフ後がバイト境界に整合しているか確認
    if (out_pos & 7u) return -3;

    if (out_len) *out_len = out_pos >> 3;
    return 0;
}
