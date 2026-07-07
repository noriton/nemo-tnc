/*
 * afsk_demod.c  ―  Bell 202 AFSK 復調 (暫定実装)
 *
 * 受信経路:
 *   GPIO3 → ADC1_CH2 → 9600 Hz サンプリング → IIR BPF → NRZI + HDLC → rx_ringbuf
 *
 * アルゴリズム:
 *   - 9600 Hz 周期タイマー (8× オーバーサンプリング / 1200 baud)
 *   - Mark 1200 Hz / Space 2200 Hz の 2 次 IIR BPF でエンベロープを比較してトーン判定
 *   - 遷移検出でビットクロックを再同期、NRZI デコード
 *   - インライン HDLC フラグ検出 + ビットデスタッフ + FCS 検証 → rx_ringbuf 投入
 *
 * TX (afsk_pwm_test) との共存:
 *   どちらも esp_timer タスク上で動作し、ADC/LEDC は独立したペリフェラルなので競合なし。
 */

#include "afsk_demod.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "ax25.h"
#include "ax25_hdlc.h"
#include "rawpacket.h"
#include "tnc_buffer.h"
#include "frame_metadata.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ======================================================================
 * サイン波ソフトウェアループバックテスト
 *
 * 有効時: ADC の代わりに内部生成のサイン波をデモジュレータに入力する。
 *   - BPF → エンベロープ → クロック回復 → HDLC の全経路をクリーン入力で検証
 *   - 矩形波エイリアスとは無関係にアルゴリズム単体の正しさを確認できる
 *
 * 実機 ADC に戻すには下の #define をコメントアウトするだけ。
 * ====================================================================== */
#define AFSK_DEMOD_SINE_TEST

#ifdef AFSK_DEMOD_SINE_TEST
#include <math.h>

static const char s_test_info[] = "NEMO-TNC TEST";

#define TEST_PREAMBLE   20
#define TEST_HDLC_BUFSZ 128
static uint8_t s_test_hdlc_buf[TEST_HDLC_BUFSZ];
static size_t  s_test_hdlc_bits = 0;

static size_t s_test_bit_idx   = 0;
static int    s_test_bit_phase = 0;   /* 0 .. DEMOD_SPB-1 */
static bool   s_test_nrzi      = true; /* true = Mark */
static float  s_test_phase_acc = 0.0f;

/* フレーム送出後に 2 秒間の無音を挟んでスケルチ動作も確認する */
/* DEMOD_SAMPLE_RATE / DEMOD_SPB はこのブロックより後で定義されるため直接値を使用 */
#define TEST_FS           9600
#define TEST_SPB          8       /* TEST_FS / 1200 baud */
#define TEST_SILENCE_SAMP (TEST_FS * 2)
static int s_test_silence_cnt = 0;

#define TEST_INC_MARK  (2.0f * (float)M_PI * 1200.0f / TEST_FS)
#define TEST_INC_SPACE (2.0f * (float)M_PI * 2200.0f / TEST_FS)
#define TEST_AMPLITUDE 512.0f

static void test_signal_init(void)
{
    /* ax25_build_ui_frame は [0x7E][addr+ctrl+pid+info+FCS][0x7E] を返す。
     * ax25_hdlc_frame は FCS 込みフレーム (フラグなし) を期待するので
     * ui_buf+1, ui_len-2 で先頭/末尾の 0x7E を除いて渡す。 */
    ax25_address_t addr = { "APTEST", 0, "JH1FBM", 0 };
    uint8_t ui_buf[64];
    size_t ui_len = ax25_build_ui_frame(
        &addr, (const uint8_t *)s_test_info, sizeof(s_test_info) - 1, ui_buf);

    size_t hdlc_bytes = ax25_hdlc_frame(
        ui_buf + 1, ui_len - 2, TEST_PREAMBLE,
        s_test_hdlc_buf, TEST_HDLC_BUFSZ,
        &s_test_hdlc_bits, NULL);

    ESP_LOGI("AFSK_RX", "SINE TEST: UI %zu bytes -> HDLC %zu bytes / %zu bits",
             ui_len - 2, hdlc_bytes, s_test_hdlc_bits);

    s_test_bit_idx    = 0;
    s_test_bit_phase  = 0;
    s_test_nrzi       = true;
    s_test_phase_acc  = 0.0f;
    s_test_silence_cnt = TEST_SILENCE_SAMP;
}

static int test_signal_next_sample(void)
{
    if (s_test_silence_cnt > 0) {
        s_test_silence_cnt--;
        return 2048;
    }

    if (s_test_bit_phase == 0) {
        if (s_test_bit_idx < s_test_hdlc_bits) {
            uint8_t bit = (s_test_hdlc_buf[s_test_bit_idx >> 3]
                           >> (s_test_bit_idx & 7u)) & 1u;
            if (bit == 0) s_test_nrzi = !s_test_nrzi;
            s_test_bit_idx++;
        } else {
            s_test_bit_idx    = 0;
            s_test_nrzi       = true;
            s_test_bit_phase  = 0;
            s_test_silence_cnt = TEST_SILENCE_SAMP;
            return 2048;
        }
    }

    float inc = s_test_nrzi ? TEST_INC_MARK : TEST_INC_SPACE;
    s_test_phase_acc += inc;
    if (s_test_phase_acc >= 2.0f * (float)M_PI) {
        s_test_phase_acc -= 2.0f * (float)M_PI;
    }

    if (++s_test_bit_phase >= TEST_SPB) {
        s_test_bit_phase = 0;
    }

    return (int)(2048.0f + TEST_AMPLITUDE * sinf(s_test_phase_acc));
}

#endif /* AFSK_DEMOD_SINE_TEST */

static const char *TAG = "AFSK_RX";

/* --------------------------------------------------------------------------
 * ADC
 * -------------------------------------------------------------------------- */
#define DEMOD_ADC_UNIT     ADC_UNIT_1
#define DEMOD_ADC_CHANNEL  ADC_CHANNEL_2   /* GPIO3 = ADC1_CH2 */
#define DEMOD_ADC_ATTEN    ADC_ATTEN_DB_12 /* 0 - 3.9 V レンジ */

/* --------------------------------------------------------------------------
 * サンプリング  fs=9600 Hz (8 サンプル/bit)
 * adc_oneshot_read の実行時間 (>50µs) のため 19200 Hz (52µs周期) ではWDTが発生する。
 * Q=2.5 でビット末尾 (sample 7) 判定を行い群遅延問題を回避する。
 * -------------------------------------------------------------------------- */
#define DEMOD_SAMPLE_RATE  9600            /* Hz: 8 サンプル/bit (1200 baud) */
#define DEMOD_BAUD         1200
#define DEMOD_SPB          (DEMOD_SAMPLE_RATE / DEMOD_BAUD)  /* 8 */
#define DEMOD_SAMPLE_US    (1000000 / DEMOD_SAMPLE_RATE)     /* 104 µs */

/* --------------------------------------------------------------------------
 * Bell 202 BPF 係数  (2 次 RBJ BPF, Q = 2.5, fs = 9600 Hz)
 * フィルタ式: y[n] = b0*x[n] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
 *
 * Q=2.5 を選ぶ理由:
 *   Q=1 では 2200 Hz 5次高調波が 1400 Hz にエイリアスして Mark BPF に混入し
 *   envM≈envS となり判別不能。Q=2.5 (帯域 960-1440 Hz) では混入が 12.5% に抑えられる。
 *
 * ビット判定を末尾 (DEMOD_SPB-1=7) にする理由:
 *   群遅延 τ≈6.4 サンプル のため中点 (4) では収束 47% と不十分。
 *   末尾 (7) での収束率は 66% に改善される。
 * -------------------------------------------------------------------------- */

/* Mark 1200 Hz, Q=2.5, fs=9600 Hz
 *   w0=π/4, sin=cos=0.70711, alpha=0.14142
 *   b0=0.35355/1.14142=0.30970, a1=-1.41421/1.14142=-1.23934, a2=0.85858/1.14142=0.75220 */
#define MARK_B0   0.30970f
#define MARK_B2  (-0.30970f)
#define MARK_A1  (-1.23934f)
#define MARK_A2   0.75220f

/* Space 2200 Hz, Q=2.5, fs=9600 Hz
 *   w0=1.43990 rad, sin=0.99144, cos=0.13053, alpha=0.19829
 *   b0=0.49572/1.19829=0.41369, a1=-0.26106/1.19829=-0.21787, a2=0.80171/1.19829=0.66909 */
#define SPACE_B0   0.41369f
#define SPACE_B2  (-0.41369f)
#define SPACE_A1  (-0.21787f)
#define SPACE_A2   0.66909f

/* エンベロープ LP: α ≈ 0.6 (τ ≈ 1.7 サンプル ≈ 0.17 ms) */
#define ENV_ALPHA  0.6f

/* DC 追跡 LP: α ≈ 0.05 (τ ≈ 20 サンプル ≈ 2 ms)
 * TX 開始時の DC 過渡 (0→2048) が最初の 1〜2 フラグで収束する速度が必要。
 * α=0.005 は τ=200 サンプル=20ms でプリアンブル 133ms 中に収束しきれなかった */
#define DC_ALPHA   0.05f

/* スケルチ閾値: envM + envS の合計がこれ以下なら無信号とみなす
 * ループバック時の典型値は数十〜数百。ノイズのみ時は 1〜5 程度 */
#define SQUELCH_THRESHOLD  10.0f

/* トーン判定ヒステリシス: envM - envS の差がこの範囲内では前のトーンを維持 */
#define TONE_HYST  3.0f

/* --------------------------------------------------------------------------
 * HDLC 定数
 * -------------------------------------------------------------------------- */
#define HDLC_MAX_FRAME   RAW_PACKET_MAX_LEN_WITH_FCS  /* 514 bytes (FCS 込み) */
#define PORT_ID_DEFAULT  0                             /* デフォルト受信ポート */

typedef enum { HDLC_HUNT, HDLC_SYNC, HDLC_FRAME } hdlc_state_t;
static const char * const s_hdlc_state_str[] = { "HUNT", "SYNC", "FRAME" };

/* --------------------------------------------------------------------------
 * 静的変数 (ゼロ初期化 / afsk_demod_init で明示設定)
 * -------------------------------------------------------------------------- */

/* BPF 遅延ライン */
static float s_xm1, s_xm2;   /* Mark  BPF: x[n-1], x[n-2] */
static float s_ym1, s_ym2;   /* Mark  BPF: y[n-1], y[n-2] */
static float s_xs1, s_xs2;   /* Space BPF: x[n-1], x[n-2] */
static float s_ys1, s_ys2;   /* Space BPF: y[n-1], y[n-2] */

/* エンベロープ */
static float s_env_m;
static float s_env_s;

/* DC 追跡 */
static float s_dc;

/* ビットクロック */
static int   s_phase;          /* 0 .. DEMOD_SPB-1 */
static bool  s_tone;           /* ヒステリシス付きトーン (true = Mark) */
static bool  s_tone_prev;      /* 前サンプルのトーン (遷移検出用) */
static int   s_sync_lockout;   /* 再同期ロックアウトカウンタ */
static bool  s_nrzi_ref;       /* NRZI 参照: 最後のビット決定時のトーン */
static bool  s_squelch_active; /* true=無信号状態 (スケルチ解除検出用) */

/* HDLC デコーダ */
static hdlc_state_t s_hdlc_state;
static int          s_ones;             /* 直前の連続 1 ビット数 */
static uint8_t      s_frame_buf[HDLC_MAX_FRAME];
static size_t       s_frame_bit_pos;

/* ペリフェラルハンドル */
static adc_oneshot_unit_handle_t s_adc_handle   = NULL;
static esp_timer_handle_t        s_sample_timer = NULL;

/* 診断用統計カウンタ */
static uint32_t s_stat_flags  = 0;  /* フラグ検出数 (SYNC 遷移) */
static uint32_t s_stat_fcs_ok = 0;  /* FCS OK フレーム数 */
static uint32_t s_stat_fcs_ng = 0;  /* FCS NG フレーム数 */
static uint32_t s_sample_cnt  = 0;  /* 定期ログ用サンプルカウンタ */

/* 定期ログ間隔: 5 秒 (= DEMOD_SAMPLE_RATE × 5) */
#define LOG_INTERVAL_SAMP  (DEMOD_SAMPLE_RATE * 5)

/* ==========================================================================
 * HDLC フレーム完了処理
 * ========================================================================== */

static void hdlc_frame_done(size_t byte_len)
{
    if (!ax25_fcs_verify(s_frame_buf, byte_len)) {
        s_stat_fcs_ng++;
        ESP_LOGI(TAG, "FCS NG %zu bytes (ok=%lu ng=%lu)",
                 byte_len, (unsigned long)s_stat_fcs_ok, (unsigned long)s_stat_fcs_ng);
        return;
    }

    s_stat_fcs_ok++;
    ESP_LOGI(TAG, "RX OK %zu bytes (ok=%lu ng=%lu)",
             byte_len, (unsigned long)s_stat_fcs_ok, (unsigned long)s_stat_fcs_ng);

    tnc_meta_header_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.version     = TNC_META_VERSION_1;
    meta.type        = META_TYPE_RX_FRAME;
    meta.header_len  = (uint16_t)sizeof(tnc_meta_header_t);
    meta.payload_len = (uint16_t)byte_len;
    meta.port_id     = PORT_ID_DEFAULT;

    if (byte_len >= 14) {
        decode_callsign(s_frame_buf,     meta.dest_call, NULL);
        decode_callsign(s_frame_buf + 7, meta.src_call,  NULL);
    }

    /* static バッファ: esp_timer タスクは単一スレッドなので安全 */
    static uint8_t out_buf[sizeof(tnc_meta_header_t) + HDLC_MAX_FRAME];
    memcpy(out_buf, &meta, sizeof(meta));
    memcpy(out_buf + sizeof(meta), s_frame_buf, byte_len);
    xRingbufferSend(rx_ringbuf[PORT_ID_DEFAULT], out_buf,
                    sizeof(meta) + byte_len, 0);
}

/* ==========================================================================
 * HDLC ビット処理
 *
 * フラグ (0x7E = 0,1,1,1,1,1,1,0 LSB first) の検出方法:
 *   連続 1 ビットが 6 個になり、次の 0 が来たときがフラグ。
 *   その時点で蓄積済みの「開始 0 (1bit) + 連続 1 のうち最初の 5 個」= 計 6 ビットは
 *   フラグ自身のビットなので frame_bit_pos から 6 を引いて除去する。
 * ========================================================================== */

static inline void hdlc_append_bit(uint8_t bit)
{
    size_t byte_pos = s_frame_bit_pos >> 3;
    if (byte_pos >= HDLC_MAX_FRAME) {
        s_hdlc_state    = HDLC_HUNT;
        s_frame_bit_pos = 0;
        return;
    }
    if (bit) {
        s_frame_buf[byte_pos] |= (uint8_t)(1u << (s_frame_bit_pos & 7u));
    }
    /* 0 ビットはバッファ初期化済み (memset 0) なので書き込み不要 */
    s_frame_bit_pos++;
    s_hdlc_state = HDLC_FRAME;
}

static void hdlc_process_bit(uint8_t bit)
{
    if (bit) {
        s_ones++;
        if (s_ones < 6) {
            if (s_hdlc_state != HDLC_HUNT) {
                hdlc_append_bit(1);
            }
        } else if (s_ones == 6) {
            /* 6 個目の 1: 蓄積せず次ビットを待つ */
        } else {
            /* 7+ 連続 1 → アボート */
            ESP_LOGW(TAG, "ABORT (7+ ones) hdlc=%s frame_bit_pos=%zu",
                     s_hdlc_state_str[s_hdlc_state], s_frame_bit_pos);
            s_hdlc_state    = HDLC_HUNT;
            s_frame_bit_pos = 0;
            s_ones          = 0;
        }
    } else {
        if (s_ones == 5) {
            /* スタッフビット: 破棄 */
            s_ones = 0;
            return;
        }
        if (s_ones == 6) {
            /* フラグ (0x7E) 検出 */
            if (s_hdlc_state == HDLC_HUNT) {
                /* HUNT → SYNC: プリアンブルを初検出 */
                ESP_LOGI(TAG, "FLAG sync (DC=%.0f envM=%.1f envS=%.1f)",
                         s_dc, s_env_m, s_env_s);
            }
            s_stat_flags++;
            if (s_hdlc_state == HDLC_FRAME) {
                size_t valid_bits = (s_frame_bit_pos >= 6) ?
                                     s_frame_bit_pos - 6 : 0;
                /* バイト境界チェック + 最小フレーム長 (16 bytes = FCS含む最小) */
                if ((valid_bits & 7u) == 0 && valid_bits >= 16u * 8u) {
                    hdlc_frame_done(valid_bits >> 3);
                } else {
                    ESP_LOGW(TAG, "FRAME DROP (misaligned/short) "
                             "frame_bit_pos=%zu valid_bits=%zu (%%8=%zu)",
                             s_frame_bit_pos, valid_bits, valid_bits & 7u);
                }
            }
            memset(s_frame_buf, 0, sizeof(s_frame_buf));
            s_hdlc_state    = HDLC_SYNC;
            s_frame_bit_pos = 0;
            s_ones          = 0;
            return;
        }
        /* 通常の 0 ビット (ones = 0..4) */
        s_ones = 0;
        if (s_hdlc_state != HDLC_HUNT) {
            hdlc_append_bit(0);
        }
    }
}

/* ==========================================================================
 * サンプリングタイマーコールバック (9600 Hz = 104 µs ごと)
 * ========================================================================== */

static void afsk_sample_cb(void *arg)
{
#ifdef AFSK_DEMOD_SINE_TEST
    int raw = test_signal_next_sample();
#else
    /* 1. ADC 読み取り (12-bit: 0..4095, 中点 ≈ 2048) */
    int raw = 2048;
    adc_oneshot_read(s_adc_handle, DEMOD_ADC_CHANNEL, &raw);
#endif

    /* 2. DC 除去 (1 次 IIR LP) */
    s_dc += DC_ALPHA * ((float)raw - s_dc);
    float ac = (float)raw - s_dc;

    /* 3. Mark BPF (y[n] = b0*x[n] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]) */
    float ym = MARK_B0  * ac   + MARK_B2  * s_xm2
             - MARK_A1  * s_ym1 - MARK_A2  * s_ym2;
    s_xm2 = s_xm1;  s_xm1 = ac;
    s_ym2 = s_ym1;  s_ym1 = ym;

    /* 4. Space BPF */
    float ys = SPACE_B0 * ac   + SPACE_B2 * s_xs2
             - SPACE_A1 * s_ys1 - SPACE_A2 * s_ys2;
    s_xs2 = s_xs1;  s_xs1 = ac;
    s_ys2 = s_ys1;  s_ys1 = ys;

    /* 5. エンベロープ検出 (整流 + 1 次 IIR LP) */
    float abs_m = (ym < 0.0f) ? -ym : ym;
    float abs_s = (ys < 0.0f) ? -ys : ys;
    s_env_m = ENV_ALPHA * abs_m + (1.0f - ENV_ALPHA) * s_env_m;
    s_env_s = ENV_ALPHA * abs_s + (1.0f - ENV_ALPHA) * s_env_s;

    /* 6. スケルチ: 信号合計が閾値未満 → HDLC リセットして早期リターン */
    if (s_env_m + s_env_s < SQUELCH_THRESHOLD) {
        if (s_hdlc_state != HDLC_HUNT) {
            s_hdlc_state    = HDLC_HUNT;
            s_frame_bit_pos = 0;
        }
        /* s_ones は HUNT 状態でも常にリセットする。
         * リセットしないと次の TX 開始時に残留値から始まり、
         * 最初のフラグが 7 連続 1 = アボートに化けてプリアンブル全体を見逃す */
        s_ones          = 0;
        s_squelch_active = true;
        if (++s_phase >= DEMOD_SPB) s_phase = 0;
        if (++s_sample_cnt >= LOG_INTERVAL_SAMP) {
            s_sample_cnt = 0;
            ESP_LOGI(TAG, "STAT DC=%.0f envM=%.1f envS=%.1f [NO SIGNAL] "
                     "flags=%lu ok=%lu ng=%lu",
                     s_dc, s_env_m, s_env_s,
                     (unsigned long)s_stat_flags,
                     (unsigned long)s_stat_fcs_ok,
                     (unsigned long)s_stat_fcs_ng);
        }
        return;
    }

    /* スケルチ解除直後: ビットクロックを安全な位相に初期化
     * phase = DEMOD_SPB-1 にすると次サンプルで phase==0 となり
     * (判定タイミングと合わせて) 最初の判定が発生する */
    if (s_squelch_active) {
        s_squelch_active = false;
        s_phase          = DEMOD_SPB - 1;  /* = 7: 次の判定まで最低 5 サンプル */
        s_sync_lockout   = 0;
        s_nrzi_ref       = true;           /* AX.25 NRZI 初期トーン = Mark */
        s_ones           = 0;
    }

    /* 7. トーン判定 (ヒステリシス付き)
     *    差が TONE_HYST 未満の近傍では前のトーンを維持 → 境界での高速振動を防ぐ */
    float diff = s_env_m - s_env_s;
    if (s_tone) {                          /* 現在 Mark */
        if (diff < -TONE_HYST) s_tone = false;
    } else {                               /* 現在 Space */
        if (diff >  TONE_HYST) s_tone = true;
    }

    /* 8. ビット決定 (phase == 0: ビット境界の 1 サンプル後)
     *    遷移検出より先に判定する。
     *    判定を境界ちょうどではなく 1 サンプル後にすることで、resync が
     *    ちょうど判定と同じサンプルで発火して位相が二重に進んでしまう
     *    競合 (旧: phase==DEMOD_SPB-1 での重複判定バグ) を避ける。 */
    bool decision_this_sample = (s_phase == 0);
    if (decision_this_sample) {
        /* NRZI デコード: 前回と同じトーン → 1, 異なる → 0 */
        uint8_t bit = (s_tone == s_nrzi_ref) ? 1u : 0u;
        s_nrzi_ref  = s_tone;
        hdlc_process_bit(bit);
        /* 判定直後は resync を数サンプルロックアウトする (resync 後だけでなく
         * 通常判定の後も)。BPF のリンギング (本物の遷移直後に起きる 1〜2
         * サンプルのトーンの跳ね返り) を新たな遷移と誤検出し、余分な
         * resync とビット判定を引き起こすのを防ぐため。 */
        s_sync_lockout = DEMOD_SPB / 2;
    }

    /* 9. 遷移検出 → ビットクロック再同期 (判定の後に実行) */
    bool transition = (s_tone != s_tone_prev);
    s_tone_prev = s_tone;
    if (decision_this_sample) {
        /* 判定と同じサンプルで遷移が来た場合はクロックは既に同期している
         * ので resync しない (位相を余分に進めると次サンプルで重複判定になる) */
    } else if (s_sync_lockout > 0) {
        s_sync_lockout--;
    } else if (transition) {
        /* 群遅延補正: 次の判定点 (phase==0) が遷移の 1 サンプル後になるよう
         * phase = DEMOD_SPB-1 にセットする (この直後の位相カウンタ更新で 0 へ) */
        s_phase        = DEMOD_SPB - 1;
        s_sync_lockout = DEMOD_SPB / 2;   /* 4 サンプル間ロック */
    }

    /* 10. 位相カウンタ更新 */
    if (++s_phase >= DEMOD_SPB) {
        s_phase = 0;
    }

    /* 11. 定期診断ログ (5 秒ごと) */
    if (++s_sample_cnt >= LOG_INTERVAL_SAMP) {
        s_sample_cnt = 0;
        ESP_LOGI(TAG,
                 "STAT DC=%.0f envM=%.1f envS=%.1f diff=%.1f tone=%s hdlc=%s "
                 "flags=%lu ok=%lu ng=%lu",
                 s_dc, s_env_m, s_env_s, diff,
                 s_tone ? "MARK" : "SPACE",
                 s_hdlc_state_str[s_hdlc_state],
                 (unsigned long)s_stat_flags,
                 (unsigned long)s_stat_fcs_ok,
                 (unsigned long)s_stat_fcs_ng);
    }
}

/* ==========================================================================
 * 初期化 API
 * ========================================================================== */

void afsk_demod_init(void)
{
    /* 初期値設定 */
    s_dc             = 2048.0f;
    s_tone           = true;
    s_tone_prev      = true;
    s_nrzi_ref       = true;
    s_sync_lockout   = 0;
    s_squelch_active = true;
    s_hdlc_state     = HDLC_HUNT;
    memset(s_frame_buf, 0, sizeof(s_frame_buf));

#ifdef AFSK_DEMOD_SINE_TEST
    test_signal_init();
    ESP_LOGI(TAG, "SINE TEST MODE: ADC をスキップし内部サイン波を入力");
    esp_err_t err = ESP_OK;
#else
    /* ADC 初期化 (ESP-IDF v5 oneshot API) */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = DEMOD_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(err));
        return;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = DEMOD_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_oneshot_config_channel(s_adc_handle, DEMOD_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return;
    }
#endif

    /* サンプリング周期タイマー */
    esp_timer_create_args_t targs = {
        .callback        = afsk_sample_cb,
        .name            = "afsk_rx",
        .dispatch_method = ESP_TIMER_TASK,
    };
    err = esp_timer_create(&targs, &s_sample_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create: %s", esp_err_to_name(err));
#ifndef AFSK_DEMOD_SINE_TEST
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
#endif
        return;
    }

    err = esp_timer_start_periodic(s_sample_timer, DEMOD_SAMPLE_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic: %s", esp_err_to_name(err));
        esp_timer_delete(s_sample_timer);
        s_sample_timer = NULL;
#ifndef AFSK_DEMOD_SINE_TEST
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
#endif
        return;
    }

#ifdef AFSK_DEMOD_SINE_TEST
    ESP_LOGI(TAG, "AFSK demod: SINE TEST  %dHz  %dbaud  %dsamp/bit",
             DEMOD_SAMPLE_RATE, DEMOD_BAUD, DEMOD_SPB);
#else
    ESP_LOGI(TAG, "AFSK demod: GPIO%d (ADC1_CH%d)  %dHz  %dbaud  %dsamp/bit",
             AFSK_DEMOD_GPIO, (int)DEMOD_ADC_CHANNEL,
             DEMOD_SAMPLE_RATE, DEMOD_BAUD, DEMOD_SPB);
#endif
}
