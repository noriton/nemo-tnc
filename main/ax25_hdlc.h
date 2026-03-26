#ifndef AX25_HDLC_H
#define AX25_HDLC_H

#include <stdint.h>
#include <stddef.h>

// HDLC フレーミング定数
#define HDLC_FLAG       0x7E    // AX.25 フラグバイト
#define HDLC_STUFF_LEN  5       // ビットスタッフィング: 連続1の最大数

// 出力バッファサイズの計算マクロ
// 入力 n バイトに対して最悪ケース（全ビット1）のビットスタッフィング込みサイズ
//   データ部: n*8 + n*8/5 ビット (最悪スタッフィング)
//   フラグ部: (preamble+2)*8 ビット (プリアンブル+開始+終了)
//   パディング: 最大7ビット
//   端数繰り上げ: /8 +1
#define AX25_HDLC_OUT_MAX(n, preamble) \
    (((size_t)(preamble) + 2) + ((n) * 12 + 7) / 8 + 2)

/**
 * FCS付きAX.25フレームにHDLCフレーミングを施す
 *
 * ビット順序: LSBファースト (AX.25/HDLC標準)
 *   out_buf[0] の bit0 が最初に送信されるビット
 *
 * フレーム構成:
 *   [プリアンブルフラグ × preamble] [開始フラグ 0x7E]
 *   [データ + ビットスタッフィング]
 *   [終了フラグ 0x7E]
 *   [パディング 0ビット × (0〜7)] ← バイト境界に合わせる
 *
 * フラグ部分にはビットスタッフィングを適用しない。
 * 終了フラグの後、バイト境界に満たない場合は 0 でフィルする。
 * パディングビット数は out_pad_bits に格納する。
 *
 * @param in_data      入力: FCS付きAX.25フレーム
 * @param in_len       入力バイト数
 * @param preamble     プリアンブルに送出するフラグ(0x7E)の個数
 * @param out_buf      出力バッファ (AX25_HDLC_OUT_MAX(in_len, preamble) 以上)
 * @param out_buf_size 出力バッファサイズ
 * @param out_bits     [out] パディング込みの総出力ビット数 (NULL可)
 * @param out_pad_bits [out] 末尾に付加したパディングビット数 0〜7 (NULL可)
 * @return 出力バイト数、エラー時は 0
 */
size_t ax25_hdlc_frame(const uint8_t *in_data, size_t in_len,
                        uint16_t preamble,
                        uint8_t *out_buf, size_t out_buf_size,
                        size_t *out_bits, uint8_t *out_pad_bits);

#endif /* AX25_HDLC_H */
