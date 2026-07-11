/*
 * i2s_device.c  ―  V4220M コーデック + I2S ブリングアップ
 *
 * ESP32-S3 を I2S マスターとして動作させ、V4220M コーデックへ
 * MCLK(XTI)/BCLK(SCLK)/WS(LRCK) を供給する。データは 24bit stereo。
 *
 * 現時点ではハードウェア疎通確認用のループバックテストのみ実装。
 * AFSK 変調/復調本体（afsk_pwm_test.c / afsk_demod.c）を I2S 経由に
 * 置き換える作業は別途行う。
 */
#include "i2s_device.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "I2S_DEVICE";

static i2s_chan_handle_t s_tx_chan = NULL;
static i2s_chan_handle_t s_rx_chan = NULL;

void i2s_device_init(void)
{
    // 1. V4220M ハードウェアリセットシーケンス (RSTN: active low)
    gpio_reset_pin(I2S_CODEC_RST_GPIO);
    gpio_set_direction(I2S_CODEC_RST_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(I2S_CODEC_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(I2S_CODEC_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 2. I2S チャンネル確保 (全二重, マスターモード)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return;
    }

    // 3. I2S 標準フォーマット (24bit stereo) とピン割り当て
    //    V4220M 側 DIF1/DIF0 はボード上で 00 (I2S) に固定配線されている前提
    //
    //    slot_bit_width は AUTO(=24bit) にせず明示的に 32bit にする。
    //    ESP32-S3 では slot_bit_width=24 だと DMA バッファが 3byte 詰めに
    //    なってしまい int32_t 前提のバッファと合わなくなる。32bit にすれば
    //    BCLK=64Fs (V4220M マスターモード時の規定値と同じ) になり、
    //    サンプルは各32bitスロットの上位24bitに MSB 詰めされる。
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_24BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_CODEC_MCLK_GPIO,
            .bclk = I2S_CODEC_SCLK_GPIO,
            .ws   = I2S_CODEC_LRCK_GPIO,
            .dout = I2S_CODEC_DIN_GPIO,   /* ESP32 dout -> コーデック DIN */
            .din  = I2S_CODEC_DOUT_GPIO,  /* コーデック DOUT -> ESP32 din */
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    // 4. MCLK 逓倍率: 24bit データ幅では ESP-IDF 推奨の 384×Fs にする
    //    (256×Fs だとサンプルレートが不正確になる場合がある)。
    //    384×48000Hz = 18.432MHz は V4220M の XTI 許容値 (256/384/512×Fs) の一つ。
    std_cfg.clk_cfg.mclk_multiple  = I2S_MCLK_MULTIPLE_384;
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;

    err = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode(tx) failed: %s", esp_err_to_name(err));
        return;
    }
    err = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode(rx) failed: %s", esp_err_to_name(err));
        return;
    }

    err = i2s_channel_enable(s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable(tx) failed: %s", esp_err_to_name(err));
        return;
    }
    err = i2s_channel_enable(s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable(rx) failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "V4220M + I2S initialized (Fs=%dHz, MCLK=%luHz, 24bit stereo)",
             I2S_SAMPLE_RATE, (unsigned long)I2S_SAMPLE_RATE * 384);
}

/* ---------------------------------------------------------------------
 * アナログ側ループバックテスト
 *
 * 外部入力回路がまだ無いため、V4220M の DAC 出力 (LOUT/ROUT) から
 * ADC 入力 (LIN/RIN) へジャンパ線で直結し、実際にアナログを経由させて
 * 動作確認する。どちらのチャンネルをジャンパしたか分からなくても済む
 * ように、TX は左右両チャンネルに同じ信号を出力し、RX は左右を
 * 独立に解析する。
 *
 * 送信信号: Mark(1200Hz)/Space(2200Hz) を2秒おきに切り替えるサイン波。
 * 受信側はゼロクロス数からおおよその周波数を推定し、ピーク振幅と
 * あわせて1秒ごとにログ出力する。ジャンパした側のチャンネルだけ
 * peak が大きく freq が送信周波数に近い値になれば、DAC->ADC の
 * アナログパスが正常に動作していることが確認できる。
 * ------------------------------------------------------------------- */
#include <math.h>
#include <stdbool.h>

#define LOOPBACK_TONE_MARK_HZ    1200.0f
#define LOOPBACK_TONE_SPACE_HZ   2200.0f
#define LOOPBACK_TONE_SWITCH_SEC 2
#define LOOPBACK_TONE_AMPLITUDE  2000000   /* 24bit full scale (+-8388607) に対し余裕を持たせる */

typedef struct {
    float phase_acc;
    bool  is_mark;
    int   samples_left;
} tone_gen_t;

static void tone_gen_reset(tone_gen_t *g)
{
    g->phase_acc    = 0.0f;
    g->is_mark      = true;
    g->samples_left = I2S_SAMPLE_RATE * LOOPBACK_TONE_SWITCH_SEC;
}

/* 24bit 符号付きサンプルを生成し、32bit スロットの上位24bitへ配置して返す */
static int32_t tone_gen_next_sample(tone_gen_t *g)
{
    float freq = g->is_mark ? LOOPBACK_TONE_MARK_HZ : LOOPBACK_TONE_SPACE_HZ;
    float inc  = 2.0f * (float)M_PI * freq / I2S_SAMPLE_RATE;
    g->phase_acc += inc;
    if (g->phase_acc >= 2.0f * (float)M_PI) {
        g->phase_acc -= 2.0f * (float)M_PI;
    }

    if (--g->samples_left <= 0) {
        g->is_mark      = !g->is_mark;
        g->samples_left = I2S_SAMPLE_RATE * LOOPBACK_TONE_SWITCH_SEC;
    }

    int32_t sample24 = (int32_t)(sinf(g->phase_acc) * LOOPBACK_TONE_AMPLITUDE);
    return sample24 << 8;
}

typedef struct {
    int32_t peak;
    int     zero_crossings;
    bool    prev_positive;
    bool    have_prev;
} chan_stats_t;

static void chan_stats_reset(chan_stats_t *s)
{
    s->peak           = 0;
    s->zero_crossings = 0;
    s->have_prev      = false;
}

static void chan_stats_update(chan_stats_t *s, int32_t sample24)
{
    int32_t a = (sample24 < 0) ? -sample24 : sample24;
    if (a > s->peak) s->peak = a;

    bool positive = (sample24 >= 0);
    if (s->have_prev && positive != s->prev_positive) {
        s->zero_crossings++;
    }
    s->prev_positive = positive;
    s->have_prev     = true;
}

static void i2s_analog_loopback_task(void *pvParameters)
{
    tone_gen_t tx_gen;
    tone_gen_reset(&tx_gen);

    chan_stats_t left_stats, right_stats;
    chan_stats_reset(&left_stats);
    chan_stats_reset(&right_stats);

    int log_countdown = I2S_SAMPLE_RATE;   /* 1秒ごとにログ */

    for (;;) {
        i2s_sample_t tx_sample;
        int32_t v = tone_gen_next_sample(&tx_gen);
        tx_sample.left  = v;
        tx_sample.right = v;

        size_t bytes_written = 0;
        i2s_channel_write(s_tx_chan, &tx_sample, sizeof(tx_sample),
                           &bytes_written, portMAX_DELAY);

        i2s_sample_t rx_sample;
        size_t bytes_read = 0;
        esp_err_t res = i2s_channel_read(s_rx_chan, &rx_sample, sizeof(rx_sample),
                                          &bytes_read, portMAX_DELAY);
        if (res != ESP_OK) {
            if (res != ESP_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "I2S read error: %s", esp_err_to_name(res));
            }
            continue;
        }

        /* 32bit スロットの上位24bitに詰めたサンプルを符号付き24bit値へ戻す */
        int32_t rx_left  = rx_sample.left  >> 8;
        int32_t rx_right = rx_sample.right >> 8;
        chan_stats_update(&left_stats, rx_left);
        chan_stats_update(&right_stats, rx_right);

        if (--log_countdown <= 0) {
            log_countdown = I2S_SAMPLE_RATE;
            /* ゼロクロス数の半分 = 周期数 ≒ 周波数(1秒間分) */
            float left_hz  = left_stats.zero_crossings  / 2.0f;
            float right_hz = right_stats.zero_crossings / 2.0f;
            ESP_LOGI(TAG, "LOOPBACK tx=%s(%.0fHz)  L: peak=%ld freq~=%.0fHz  R: peak=%ld freq~=%.0fHz",
                     tx_gen.is_mark ? "MARK" : "SPACE",
                     tx_gen.is_mark ? LOOPBACK_TONE_MARK_HZ : LOOPBACK_TONE_SPACE_HZ,
                     (long)left_stats.peak, left_hz,
                     (long)right_stats.peak, right_hz);
            chan_stats_reset(&left_stats);
            chan_stats_reset(&right_stats);
        }
    }
}

void i2s_device_start_analog_loopback_test(void)
{
    if (s_tx_chan == NULL || s_rx_chan == NULL) {
        ESP_LOGE(TAG, "i2s_device_start_analog_loopback_test: not initialized");
        return;
    }
    xTaskCreatePinnedToCore(i2s_analog_loopback_task, "i2s_analog_lb", 4096, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "I2S analog loopback test task started "
             "(jumper LOUT/ROUT -> LIN/RIN on the V4220M board)");
}
