#ifndef BITSTAFF_H
#define BITSTAFF_H

#include <stdint.h>

void init_hdlc_tables(void);

/**
 * @brief フレームデータをビットスタッフィングして出力バッファに書き込む
 *
 * 呼び出し前に init_hdlc_tables() を実行しておくこと。
 * out は呼び出し側で 0 クリアしておくこと（端数ビットは OR で書き込まれる）。
 *
 * @param[in]  in      スタッフィング前のデータ
 * @param[in]  in_len  in のバイト数
 * @param[out] out     スタッフィング後のデータを格納するバッファ
 * @return int         スタッフィング後の正確なビット数
 *                     バイト数が必要な場合は (戻り値 + 7) / 8 で算出すること
 */
int hdlc_stuff_frame(const uint8_t *in, int in_len, uint8_t *out);

/**
 * @brief ビットスタッフィングされたフレームデータをアンスタッフして出力バッファに書き込む
 *
 * @param[in]  in      アンスタッフィング前のデータ
 * @param[in]  in_len  in のバイト数
 * @param[out] out     アンスタッフィング後のデータを格納するバッファ
 * @return int         out に書き込んだバイト数。アボート検出時は -1
 */
int hdlc_unstuff_frame(const uint8_t *in, int in_len, uint8_t *out);

#endif /* BITSTAFF_H */
