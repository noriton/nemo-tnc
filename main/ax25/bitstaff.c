#include <stdint.h>
#include <stdbool.h>

// --- データ構造の定義 ---

typedef struct StuffEncodeResult {
    uint16_t out_bits;  // スタッフ後のビット列 (8ビット入力で最大10ビットになるため16bit幅)
    uint8_t out_len;    // 出力されたビット数 (8〜10)
    uint8_t next_state; // 次の状態 (末尾の連続する1の数: 0〜5)
} StuffEncodeResult_t;

typedef struct UnstuffDecodeResult {
    uint8_t out_bits;   // アンスタッフ後のデータ (最大8ビット)
    uint8_t out_len;    // 抽出されたビット数 (スタッフビットが抜かれるため少なくなる)
    uint8_t next_state; // 次の状態 (末尾の連続する1の数: 0〜5)
    bool has_flag;      // このバイト内にフラグ(0x7E)が含まれていたか
    bool has_abort;     // このバイト内にアボート(1が7連続以上)が含まれていたか
} UnstuffDecodeResult_t;

// 1536パターンの巨大テーブル（SRAMに配置）
StuffEncodeResult_t stuff_byte_table[6][256];
UnstuffDecodeResult_t unstuff_byte_table[6][256];

// --- テーブル初期化関数 (起動時に1回だけ呼ぶ) ---

/**
 * @brief HDLC スタッフィング/アンスタッフィング用ルックアップテーブルを初期化する
 *
 * 起動時に 1 回だけ呼び出すこと。
 * stuff_byte_table[state][byte] および unstuff_byte_table[state][byte] を生成する。
 * state は直前の連続する 1 ビット数 (0〜5)、byte は処理対象の 1 バイト値 (0〜255)。
 */
void init_hdlc_tables(void)
{
    for (int state = 0; state < 6; state++) {
        for (int val = 0; val < 256; val++) {
            
            // [1] スタッフィング用テーブルの計算
            uint16_t tx_out_bits = 0;
            uint8_t tx_out_len = 0;
            uint8_t tx_ones = state;
            
            for (int b = 0; b < 8; b++) {
                uint8_t bit = (val >> b) & 1;
                if (tx_ones == 5) {
                    tx_out_bits |= (0 << tx_out_len++); // 強制スタッフビット(0)
                    tx_ones = 0;
                }
                tx_out_bits |= (bit << tx_out_len++);
                if (bit == 1) {
                    tx_ones++;
                } else {
                    tx_ones = 0;
                }
            }
            stuff_byte_table[state][val].out_bits = tx_out_bits;
            stuff_byte_table[state][val].out_len = tx_out_len;
            stuff_byte_table[state][val].next_state = tx_ones;

            // [2] アンスタッフィング用テーブルの計算
            uint8_t rx_out_bits = 0;
            uint8_t rx_out_len = 0;
            uint8_t rx_ones = state;
            bool rx_flag = false;
            bool rx_abort = false;
            
            for (int b = 0; b < 8; b++) {
                uint8_t bit = (val >> b) & 1;
                if (rx_ones == 5) {
                    if (bit == 0) {
                        rx_ones = 0; // スタッフビットなので破棄
                        continue;
                    } else {
                        rx_ones++; // 6連続目（フラグかアボートの一部）
                        continue; 
                    }
                } else if (rx_ones == 6) {
                    if (bit == 0) {
                        rx_flag = true;
                        rx_ones = 0;
                    } else {
                        rx_abort = true;
                        rx_ones++;
                    }
                    continue;
                }
                
                rx_out_bits |= (bit << rx_out_len++);
                if (bit == 1) rx_ones++;
                else rx_ones = 0;
            }
            unstuff_byte_table[state][val].out_bits = rx_out_bits;
            unstuff_byte_table[state][val].out_len = rx_out_len;
            unstuff_byte_table[state][val].next_state = rx_ones;
            unstuff_byte_table[state][val].has_flag = rx_flag;
            unstuff_byte_table[state][val].has_abort = rx_abort;
        }
    }
}


// --- スタッフィング (送信: フレームを一括処理) ---

/**
 * @brief フレームデータをビットスタッフィングして出力バッファに書き込む
 *
 * @param[in]  in      スタッフィング前のデータ
 * @param[in]  in_len  in のバイト数
 * @param[out] out     スタッフィング後のデータを格納するバッファ
 * @return int         スタッフィング後の正確なビット数
 *                     バイト数が必要な場合は (戻り値 + 7) / 8 で算出すること
 */
int hdlc_stuff_frame(const uint8_t *in, int in_len, uint8_t *out)
{
    uint8_t state = 0;
    uint32_t bit_buffer = 0;
    uint8_t bit_count = 0;
    int out_len = 0;
    int out_bits = 0;

    for (int i = 0; i < in_len; i++) {
        StuffEncodeResult_t res = stuff_byte_table[state][in[i]];
        state = res.next_state;

        bit_buffer |= ((uint32_t)res.out_bits << bit_count);
        bit_count += res.out_len;
        out_bits += res.out_len;

        while (bit_count >= 8) {
            out[out_len++] = (uint8_t)(bit_buffer & 0xFF);
            bit_buffer >>= 8;
            bit_count -= 8;
        }
    }

    // 端数ビットをフラッシュ (0パディング、ただし戻り値はパディング前のビット数)
    if (bit_count > 0) {
        out[out_len] = (uint8_t)(bit_buffer & 0xFF);
    }

    return out_bits;
}

// --- アンスタッフィング (受信: FX.25フレーム内AX.25を一括処理) ---

/**
 * @brief ビットスタッフィングされたフレームデータをアンスタッフして出力バッファに書き込む
 *
 * @param[in]  in      アンスタッフィング前のデータ
 * @param[in]  in_len  in のバイト数
 * @param[out] out     アンスタッフィング後のデータを格納するバッファ
 * @return int         out に書き込んだバイト数。アボート検出時は -1
 */
int hdlc_unstuff_frame(const uint8_t *in, int in_len, uint8_t *out)
{
    uint8_t state = 0;
    uint16_t bit_buffer = 0;
    uint8_t bit_count = 0;
    int out_len = 0;

    for (int i = 0; i < in_len; i++) {
        UnstuffDecodeResult_t res = unstuff_byte_table[state][in[i]];
        state = res.next_state;

        if (res.has_abort) return -1;

        bit_buffer |= ((uint16_t)res.out_bits << bit_count);
        bit_count += res.out_len;

        while (bit_count >= 8) {
            out[out_len++] = (uint8_t)(bit_buffer & 0xFF);
            bit_buffer >>= 8;
            bit_count -= 8;
        }
    }

    return out_len;
}