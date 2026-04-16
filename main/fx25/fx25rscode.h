#ifndef FX25_RSCODE_H
#define FX25_RSCODE_H

#include <stdint.h>

// GF(256) 演算用テーブル (実体は fx25rscode.c で定義)
extern uint8_t exp_tbl[512];
extern uint8_t log_tbl[256];

typedef struct RsContext {
    int parity_len;
    int root0;
    uint8_t gen_poly[65]; // 対数(Log)形式で保存された生成多項式
} RsContext_t;

// テーブル・コンテキスト初期化
void init_gf_tables(void);
void init_rs_context(RsContext_t *ctx, int parity_len);

// 単一ブロック エンコード / デコード
void rs_encode_block(const RsContext_t *ctx, const uint8_t *data, int data_len, uint8_t *parity);
int  rs_decode_block(const RsContext_t *ctx, uint8_t *msg, int msg_len);

// インターリーブ対応 エンコード / デコード
int  rs_encode_interleaved(const RsContext_t *ctx, const uint8_t *data, int data_len, uint8_t *parity_out, int N);
int  rs_decode_interleaved(const RsContext_t *ctx, uint8_t *msg_data, int data_len, uint8_t *parity_data, int N);

#endif /* FX25_RSCODE_H */
