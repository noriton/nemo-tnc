#include <string.h>
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "kiss_parser.h"

/**
 * @brief KISS パーサーコンテキストを初期化する
 *
 * @param ctx 初期化対象のパーサーコンテキスト
 */
void kiss_init(kiss_context_t *ctx)
{
    // これでkiss_context_tのサイズ分のメモリが確保される
    ctx->idx = 0;
    ctx->escape_mode = 0;
    ctx->in_frame = 0;
    memset(ctx->buf, 0, sizeof(ctx->buf));
}

/**
 * @brief KISS ストリームを解析し、完成したフレームをリングバッファへ投入する
 *
 * @param ctx       パーサーコンテキスト（呼び出し間で状態を保持）
 * @param data      受信データバッファ
 * @param len       data のバイト数
 * @param port_id   メタデータに記録するポート番号
 * @param target_rb 完成フレームの投入先 No-Split リングバッファ
 */
void kiss_process_stream(kiss_context_t *ctx, const uint8_t *data, size_t len, int port_id, RingbufHandle_t target_rb)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        // 1. FEND (フレーム区切り)
        if (b == KISS_FEND) {
            // 有効なデータがバッファにある場合
            if (ctx->in_frame && ctx->idx > 0) {
                // 先頭のKISSコマンドを確認 (下位4bitが0x00ならデータフレーム)
                uint8_t cmd_type = ctx->buf[0] & 0x0F;
                
                if (cmd_type == 0x00 && ctx->idx > 1) {
                    size_t payload_len = ctx->idx - 1; // コマンドバイトを除く
                    size_t header_len = sizeof(tnc_meta_header_t);
                    size_t total_len = header_len + payload_len;

                    // スタック上に送信用の連結バッファを確保
                    uint8_t send_tmp[sizeof(tnc_meta_header_t) + KISS_MAX_RAW_LEN];

                    if (total_len <= sizeof(send_tmp)) {
                        tnc_meta_header_t *meta = (tnc_meta_header_t *)send_tmp;

                        // メタデータヘッダの構築（未使用フィールドを含め全てゼロ初期化）
                        memset(meta, 0, sizeof(tnc_meta_header_t));
                        meta->version     = TNC_META_VERSION_1;
                        meta->type        = META_TYPE_DATA_KISS;
                        meta->header_len  = (uint16_t)header_len;
                        meta->payload_len = (uint16_t)payload_len;
                        meta->port_id     = (uint8_t)port_id;

                        // ペイロード（AX.25パケット本体）をヘッダの直後にコピー
                        memcpy(send_tmp + header_len, &ctx->buf[1], payload_len);

                        // No-Splitリングバッファに「1アイテム」として投入
                        // タイムアウトを少し設けて、バッファフル時に即座に捨てないようにする
                        xRingbufferSend(target_rb, send_tmp, total_len, pdMS_TO_TICKS(10));
                    }
                }
            }
            // 次のフレームのために状態をリセット
            ctx->in_frame = 1;
            ctx->idx = 0;
            ctx->escape_mode = 0;
            continue;
        }

        // FENDを受信するまではデータを無視（同期待ち）
        if (!ctx->in_frame) {
            continue;
        }

        // 2. エスケープ処理 (バイトスタッフィング解除)
        if (ctx->escape_mode) {
            if (b == KISS_TFEND) {
                b = KISS_FEND;
            } else if (b == KISS_TFESC) {
                b = KISS_FESC;
            }
            // エスケープシーケンス終了
            ctx->escape_mode = 0;
        } else if (b == KISS_FESC) {
            ctx->escape_mode = 1;
            continue; // 次のバイトを待つ
        }

        // 3. データ格納
        if (ctx->idx < KISS_MAX_RAW_LEN) {
            ctx->buf[ctx->idx++] = b;
        } else {
            // バッファオーバーフロー時は同期を外してリセット
            ctx->in_frame = 0;
            ctx->idx = 0;
        }
    }
}