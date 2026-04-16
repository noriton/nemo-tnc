#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "fx25rscode.h"


#define RS_POLY 0x187
#define GF_LOG_ZERO 255          // 0の対数を表す例外値
#define MOD_NN(x) ((x) % 255)    // 255の剰余を計算するマクロ

// GF(256) 演算用テーブル
uint8_t exp_tbl[512];
// 0〜254の指数に対する値を格納（255以降は255で剰余した値を繰り返す）
// 本来必要なのは510エントリ。メモリパディングのため念のため512にしている。
uint8_t log_tbl[256];

// ====================================================================
// 初期化関係
// ====================================================================

void init_gf_tables(void)
{
    int val = 1;
    for (int i = 0; i < 255; i++) {
        exp_tbl[i] = val;
        exp_tbl[i + 255] = val;
        log_tbl[val] = i;
        val <<= 1;
        if (val & 0x100) val ^= RS_POLY;
    }
    log_tbl[0] = GF_LOG_ZERO; // 0の対数として例外値GF_LOG_ZERO(255)をセット
}

static inline uint8_t gf_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return exp_tbl[log_tbl[a] + log_tbl[b]];
}

void init_rs_context(RsContext_t *ctx, int parity_len)
{
    ctx->parity_len = parity_len;
    ctx->root0 = 128 - (parity_len / 2);
    
    // 一旦通常の係数で生成多項式を計算
    uint8_t normal_poly[65] = {0};
    normal_poly[0] = 1;
    
    for (int i = 0; i < parity_len; i++) {
        uint8_t root = exp_tbl[ctx->root0 + i];
        for (int j = i + 1; j > 0; j--) {
            normal_poly[j] = normal_poly[j - 1] ^ gf_mul(normal_poly[j], root);
        }
        normal_poly[0] = gf_mul(normal_poly[0], root);
    }
    
    // 計算した通常の係数をすべて対数(Log)に変換してコンテキストに保存
    for (int i = 0; i <= parity_len; i++) {
        ctx->gen_poly[i] = log_tbl[normal_poly[i]];
    }
}

// ====================================================================
// エンコーダ本体 (libfec互換アルゴリズム)
// ====================================================================
/**
 * @brief データ列からリードソロモンのパリティ（FEC）を生成する
 * * @details
 * 入力されたデータ配列に対して、初期化済みのコンテキストに基づいてパリティを計算します。
 * 出力されたパリティは、送信時にデータの「後ろ」に付加して送信します。
 * * @param[in]  ctx      初期化済みのRSコンテキスト構造体へのポインタ
 * @param[in]  data     エンコード対象のデータ本体（AX.25フレーム等）の配列ポインタ
 * @param[in]  data_len エンコード対象データのバイト数
 * @param[out] parity   計算されたパリティを格納するバッファへのポインタ
 * （呼び出し側で ctx->parity_len 分のメモリ確保が必要）
 * @retval 0  エラーなし（エンコードデータのバイト数は正常）
 * @retval -1  エラー（エンコードデータのバイト数が不正）

 */
int rs_encode_block(const RsContext_t *ctx, const uint8_t *data, int data_len, uint8_t *parity)
{
    if (data_len + ctx->parity_len > 255) return -1; // データ長が255バイトのRSコードの限界を超えている

    memset(parity, 0, ctx->parity_len);

    for (int i = 0; i < data_len; i++) {
        // この計算対数(Log)に変換された、update_valを共通係数として、パリティの各段を一斉に更新する
        uint8_t update_val = log_tbl[data[i] ^ parity[0]];

        if (update_val != GF_LOG_ZERO) {
            for (int j = 1; j < ctx->parity_len; j++) {
                parity[j] ^= exp_tbl[update_val + ctx->gen_poly[ctx->parity_len - j]];
            }
        }

        // memmoveでシフトレジスタを1バイト分一気にずらす
        memmove(&parity[0], &parity[1], ctx->parity_len - 1);

        if (update_val != GF_LOG_ZERO) {
            parity[ctx->parity_len - 1] = exp_tbl[update_val + ctx->gen_poly[0]];
        } else {
            parity[ctx->parity_len - 1] = 0;
        }
    }
    return 0;
}



// --- デコーダ (受信側) ---
// 戻り値: 訂正されたエラー数 (訂正不能な場合は -1)


/**
 * @brief 受信データ（メッセージ＋パリティ）の誤りを検出し、可能であれば訂正する
 * * @details
 * 受信したデータ配列（本体＋パリティ）をそのまま渡し、シンドローム計算から
 * 誤り位置の特定、誤り値の算出（CCSDS補正含む）までを一貫して行います。
 * エラーが訂正可能範囲内であれば、引数 msg 内のエラー箇所が直接書き換え（修復）されます。
 * * @param[in]     ctx     初期化済みのRSコンテキスト構造体へのポインタ
 * @param[in,out] msg     受信したデータ配列（データ本体 ＋ 受信したパリティ）へのポインタ。
 * 訂正が成功した場合、この配列内の誤りビットが修復されます。
 * @param[in]     msg_len msg配列の全体のバイト数（データ本体の長さ ＋ パリティの長さ）
 * * @return int 検出・訂正されたエラー（シンボル/バイト）の数。
 * @retval 0  エラーなし（データは正常）
 * @retval >0 訂正に成功したエラーバイト数（msg配列は修復済み）
 * @retval -1 訂正不能（パリティの許容限界を超えたエラー、または偽の根を検出したため破棄すべき）
 */
int rs_decode_block(const RsContext_t *ctx, uint8_t *msg, int msg_len)
{
    if (msg_len > 255) return -1;

    uint8_t syn[64] = {0};
    bool has_error = false;
    
    // [1] シンドローム計算 (Horner法 + 対数乗算)
    for (int i = 0; i < ctx->parity_len; i++) {
        uint8_t s = 0;
        int root_log = MOD_NN(ctx->root0 + i);
        
        for (int j = 0; j < msg_len; j++) {
            if (s == 0) {
                s = msg[j];
            } else {
                s = exp_tbl[log_tbl[s] + root_log] ^ msg[j];
            }
        }
        syn[i] = s;
        if (s != 0) has_error = true;
    }
    
    if (!has_error) return 0; // エラーなし

    // [2] Berlekamp-Massey アルゴリズム (対数形式での実装)
    uint8_t lambda[65] = {1};
    uint8_t b[65] = {1};
    int L = 0;
    
    for (int r = 0; r < ctx->parity_len; r++) {
        uint8_t delta = syn[r];
        for (int j = 1; j <= L; j++) {
            if (lambda[j] != 0 && syn[r - j] != 0) {
                delta ^= exp_tbl[log_tbl[lambda[j]] + log_tbl[syn[r - j]]];
            }
        }
        
        uint8_t prev_b[65];
        memcpy(prev_b, b, sizeof(b));
        
        memmove(&b[1], &b[0], ctx->parity_len);
        b[0] = 0;
        
        if (delta != 0) {
            uint8_t t[65];
            uint8_t log_delta = log_tbl[delta];
            
            for (int j = 0; j <= ctx->parity_len; j++) {
                t[j] = lambda[j];
                if (b[j] != 0) {
                    t[j] ^= exp_tbl[log_delta + log_tbl[b[j]]];
                }
            }
            
            if (2 * L <= r) {
                uint8_t log_delta_inv = 255 - log_delta;
                for (int j = 0; j <= ctx->parity_len; j++) {
                    if (prev_b[j] != 0) {
                        b[j] = exp_tbl[log_tbl[prev_b[j]] + log_delta_inv];
                    } else {
                        b[j] = 0;
                    }
                }
                L = r + 1 - L;
            }
            memcpy(lambda, t, sizeof(lambda));
        }
    }
    
    if (L * 2 > ctx->parity_len) return -1;
    
    // [3] チェン探索 (Chien Search)  エラー位置の特定
    int err_locs[32];
    int err_count = 0;
    uint8_t reg[65];
    
    for (int j = 1; j <= L; j++) reg[j] = lambda[j];
    
    for (int i = 0; i < msg_len; i++) {
        uint8_t sum = 1; // lambda_0 は常に 1
        
        for (int j = 1; j <= L; j++) {
            if (reg[j] != 0) {
                sum ^= reg[j];
                // 1ステップ進むごとに、項に α^-j を掛ける（対数空間での引き算）
                reg[j] = exp_tbl[log_tbl[reg[j]] + 255 - j];
            }
        }
        if (sum == 0) err_locs[err_count++] = msg_len - 1 - i;
    }
    
    if (err_count != L) return -1;
    
    // [4] Forney アルゴリズム エラー値の算出と訂正（CCSDS補正含む）
    uint8_t omega[65] = {0};
    for (int i = 0; i < L; i++) {
        for (int j = 0; j <= i; j++) {
            if (lambda[j] != 0 && syn[i - j] != 0) {
                omega[i] ^= exp_tbl[log_tbl[lambda[j]] + log_tbl[syn[i - j]]];
            }
        }
    }
    
    for (int i = 0; i < err_count; i++) {
        int loc = err_locs[i];
        int loc_idx = msg_len - 1 - loc;
        int log_root_inv = 255 - loc_idx; // α^-loc_idx の対数
        
        uint8_t err_eval = 0;
        for (int j = 0; j < L; j++) {
            if (omega[j] != 0) {
                err_eval ^= exp_tbl[MOD_NN(log_tbl[omega[j]] + (j * log_root_inv))];
            }
        }
        
        uint8_t err_loc_deriv = 0;
        for (int j = 1; j <= L; j += 2) {
            if (lambda[j] != 0) {
                err_loc_deriv ^= exp_tbl[MOD_NN(log_tbl[lambda[j]] + ((j - 1) * log_root_inv))];
            }
        }
        if (err_loc_deriv == 0) return -1;
        
        int log_magnitude = MOD_NN(log_tbl[err_eval] + 255 - log_tbl[err_loc_deriv]);
        int log_ccsds = MOD_NN(loc_idx * ctx->root0);
        
        msg[loc] ^= exp_tbl[log_magnitude + log_ccsds];
    }
    
    return err_count;
}




// ====================================================================
// N倍インターリーブ用 ラッパー関数（FX.25 独自拡張用）
// ====================================================================

/**
 * @brief インターリーブ対応 エンコーダ
 * @param N インターリーブ深さ（分割数）
 * @return 0:成功, -1:データ長が Nブロックの許容限界を超えている
 */
int rs_encode_interleaved(const RsContext_t *ctx, const uint8_t *data, int data_len, uint8_t *parity_out, int N)
{
    // 全体のデータ長が「N個のブロック」に収まるかバリデーション
    int max_data_per_block = 255 - ctx->parity_len;
    if (data_len > N * max_data_per_block)  return -1;

    uint8_t block_data[255];
    uint8_t block_parity[64]; // FX.25の最大パリティは64

    for (int b = 0; b < N; b++) {
        int block_data_len = 0;
        
        // 1. データ配列から Nオクテット飛ばしで抽出
        for (int i = b; i < data_len; i += N) {
            block_data[block_data_len++] = data[i];
        }

        // 2. 抽出したブロック単体で標準エンコード
        rs_encode_block(ctx, block_data, block_data_len, block_parity);

        // 3. 計算されたパリティを Nオクテット飛ばしで出力バッファに格納
        for (int j = 0; j < ctx->parity_len; j++) {
            parity_out[j * N + b] = block_parity[j];
        }
    }
    return 0;
}

/**
 * @brief インターリーブ対応 デコーダ
 * @param msg_data 受信したデータ本体へのポインタ（修復時はここが書き換えられる）
 * @param parity_data 受信したパリティ群へのポインタ
 * @param N インターリーブ深さ（分割数）
 * @return 訂正した合計エラー数。1ブロックでも訂正不能なら -1 を返す
 */
int rs_decode_interleaved(const RsContext_t *ctx, uint8_t *msg_data, int data_len, uint8_t *parity_data, int N)
{
    int max_data_per_block = 255 - ctx->parity_len;
    if (data_len > N * max_data_per_block) { return -1; }

    int total_errors = 0;
    uint8_t block_msg[255];

    for (int b = 0; b < N; b++) {
        int block_data_len = 0;
        
        // 1. データ部分を抽出
        for (int i = b; i < data_len; i += N) {
            block_msg[block_data_len++] = msg_data[i];
        }

        // 2. そのブロックに対応するパリティ部分を抽出して後ろに結合
        int p_offset = block_data_len;
        for (int j = 0; j < ctx->parity_len; j++) {
            block_msg[p_offset++] = parity_data[j * N + b];
        }

        // 3. 結合した 255バイト以下の配列を標準デコード
        int errs = rs_decode_block(ctx, block_msg, block_data_len + ctx->parity_len);
        if (errs < 0) { return -1; } // 訂正限界突破時は全体を破棄

        total_errors += errs;

        // 4. 訂正されたデータ部分のみを、元の msg_data 配列へ書き戻す（パリティの修復結果は不要）
        if (errs > 0) {
            int write_idx = 0;
            for (int i = b; i < data_len; i += N) {
                msg_data[i] = block_msg[write_idx++];
            }
        }
    }
    return total_errors;
}