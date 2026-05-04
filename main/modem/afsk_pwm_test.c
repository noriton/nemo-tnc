#include "afsk_pwm_test.h"
#include "driver/ledc.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ax25_hdlc.h"
#include "rawpacket.h"
#include "tnc_buffer.h"
#include "bitstaff.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "AFSK_TEST";

// ---------------------------------------------------------------------------
// Bell 202 パラメータ
// ---------------------------------------------------------------------------
#define AFSK_MARK_HZ    1200            // 1ビット (NRZI: 変化なし)
#define AFSK_SPACE_HZ   2200            // 0ビット (NRZI: 周波数変化)
#define AFSK_BAUD       1200
#define AFSK_BIT_US     (1000000 / AFSK_BAUD)   // 833 µs/bit
#define AFSK_PREAMBLE   20              // プリアンブルフラグ(0x7E)の個数

// LEDC 設定
#define AFSK_LEDC_TIMER    LEDC_TIMER_0
#define AFSK_LEDC_CHANNEL  LEDC_CHANNEL_0
#define AFSK_LEDC_DUTY     128          // 8bit分解能で50%デューティ(矩形波)

// HDLC 出力バッファ（スタック節約のため static）
static uint8_t s_hdlc_buf[AX25_HDLC_OUT_MAX(RAW_PACKET_MAX_LEN_WITH_FCS, 20)];

// NRZI 状態: true=mark(1200Hz), false=space(2200Hz)
static bool s_nrzi_state;

// ---------------------------------------------------------------------------
// 内部ヘルパー
// ---------------------------------------------------------------------------

/**
 * @brief LEDC タイマー周波数を mark/space に切り替える
 *
 * @param is_mark true=1200Hz(mark), false=2200Hz(space)
 */
static inline void afsk_set_freq(bool is_mark)
{
    ledc_set_freq(LEDC_LOW_SPEED_MODE, AFSK_LEDC_TIMER,
                  is_mark ? AFSK_MARK_HZ : AFSK_SPACE_HZ);
}

/**
 * @brief デューティを50%にして送信開始する
 */
static void afsk_tx_on(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, AFSK_LEDC_CHANNEL, AFSK_LEDC_DUTY);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, AFSK_LEDC_CHANNEL);
}

/**
 * @brief デューティを0にして無音にする
 */
static void afsk_tx_off(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, AFSK_LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, AFSK_LEDC_CHANNEL);
}

/**
 * @brief ビット列を NRZI エンコードしながら 1 ビットずつ送出する
 *
 * AX.25 HDLC の出力は LSB ファースト（buf[0] の bit0 が最初に送信）。
 * NRZI 則: 0 → s_nrzi_state を反転（周波数切替）、1 → 維持。
 *
 * @param buf       HDLC フレームバッファ（LSB first）
 * @param bit_count 送出するビット数
 */
static void afsk_send_bits(const uint8_t *buf, size_t bit_count)
{
    for (size_t i = 0; i < bit_count; i++) {
        uint8_t bit = (buf[i >> 3] >> (i & 7u)) & 1u;
        if (bit == 0) {
            s_nrzi_state = !s_nrzi_state;
        }
        afsk_set_freq(s_nrzi_state);
        esp_rom_delay_us(AFSK_BIT_US);
    }
}

// ---------------------------------------------------------------------------
// 送信メイン処理
// ---------------------------------------------------------------------------

/**
 * @brief raw_tx_item_t（FCS付き AX.25 生フレーム）を Bell 202 AFSK で送出する
 *
 * 処理フロー:
 *   1. ax25_hdlc_frame()  → HDLC フレーミング（プリアンブル + ビットスタッフ）
 *   2. afsk_send_bits()   → NRZI + PWM 周波数切替で送出
 *
 * @param item raw_tx_buf / afsk_tx_buf から取得した raw_tx_item_t ポインタ
 */
static void afsk_transmit(const raw_tx_item_t *item)
{
    size_t raw_len = item->meta.payload_len;

    size_t hdlc_bits  = 0;
    size_t hdlc_bytes = ax25_hdlc_frame(
        item->data, raw_len, AFSK_PREAMBLE,
        s_hdlc_buf, sizeof(s_hdlc_buf), &hdlc_bits, NULL);
    if (hdlc_bytes == 0) {
        ESP_LOGW(TAG, "hdlc_frame failed (raw_len=%zu)", raw_len);
        return;
    }

    ESP_LOGI(TAG, "TX port%d %s>%s  FCS=0x%04X  raw=%zu  HDLC=%zu bytes / %zu bits",
             item->meta.port_id,
             item->meta.src_call, item->meta.dest_call,
             item->meta.fcs, raw_len, hdlc_bytes, hdlc_bits);

    s_nrzi_state = true;
    afsk_set_freq(true);
    afsk_tx_on();
    vTaskDelay(pdMS_TO_TICKS(10));  // PWM安定待ち

    afsk_send_bits(s_hdlc_buf, hdlc_bits);

    afsk_tx_off();
}

// ---------------------------------------------------------------------------
// タスク
// ---------------------------------------------------------------------------

/**
 * @brief AFSK 送信タスク
 *
 * afsk_tx_buf を監視し、パケットが到着したら AFSK で送出する。
 * rawpacket_task が UISEND / KISS 由来パケットを afsk_tx_buf に書き込む。
 *
 * @param pvParameters 未使用
 */
static void afsk_pwm_test_task(void *pvParameters)
{
    for (;;) {
        size_t item_size = 0;
        raw_tx_item_t *item = (raw_tx_item_t *)xRingbufferReceive(
            afsk_tx_buf, &item_size, portMAX_DELAY);

        if (item == NULL) continue;

        // HDLC フレーミングと送信（item->data を s_hdlc_buf にコピー後、アイテムを解放）
        afsk_transmit(item);
        vRingbufferReturnItem(afsk_tx_buf, (void *)item);
    }
}

// ---------------------------------------------------------------------------
// 初期化 API
// ---------------------------------------------------------------------------

void afsk_pwm_test_init(void)
{
    // HDLC ルックアップテーブル初期化（ax25_hdlc_frame が内部で使用）
    init_hdlc_tables();

    // LEDC タイマー初期化（8bit分解能、mark周波数で開始）
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = AFSK_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = AFSK_MARK_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    // LEDC チャンネル初期化（duty=0 で無音状態から開始）
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = AFSK_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = AFSK_LEDC_CHANNEL,
        .timer_sel  = AFSK_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    ESP_LOGI(TAG, "AFSK PWM init: GPIO%d  Mark=%dHz Space=%dHz %dbaud",
             AFSK_PWM_GPIO, AFSK_MARK_HZ, AFSK_SPACE_HZ, AFSK_BAUD);

    // Core 1 に固定して bit-bang タイミングの影響を最小化
    xTaskCreatePinnedToCore(afsk_pwm_test_task, "afsk_test",
                            4096, NULL, 5, NULL, 1);
}
