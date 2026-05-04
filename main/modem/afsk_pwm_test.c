#include "afsk_pwm_test.h"
#include "driver/ledc.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ax25_hdlc.h"
#include "rawpacket.h"
#include "frame_metadata.h"
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

// テスト送信内容
#define AFSK_TEST_SRC   "JH1FBM"
#define AFSK_TEST_DST   "CQ"
#define AFSK_TEST_INFO  "TEST DE NEMO-TNC"

// 送信バッファ（スタック節約のためstatic）
static uint8_t s_raw_buf[RAW_PACKET_MAX_LEN_WITH_FCS];
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
 * @brief テスト用 AX.25 UI フレームを組み立て Bell 202 AFSK で送出する
 *
 * 処理フロー:
 *   1. rawpacket_build_ax25_ui()  → AX.25 L3 生フレーム（FCS付き）
 *   2. ax25_hdlc_frame()          → HDLC フレーミング（プリアンブル+ビットスタッフ）
 *   3. afsk_send_bits()           → NRZI + PWM 周波数切替で送出
 */
static void afsk_send_test_packet(void)
{
    // 1. AX.25 L3 生フレームを組み立てる
    tnc_meta_header_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.version     = TNC_META_VERSION_1;
    meta.type        = META_TYPE_DATA_UI;
    meta.header_len  = sizeof(meta);
    meta.payload_len = (uint16_t)strlen(AFSK_TEST_INFO);
    strncpy(meta.src_call,  AFSK_TEST_SRC, sizeof(meta.src_call)  - 1);
    strncpy(meta.dest_call, AFSK_TEST_DST, sizeof(meta.dest_call) - 1);

    size_t raw_len = rawpacket_build_ax25_ui(
        &meta, (const uint8_t *)AFSK_TEST_INFO, s_raw_buf, sizeof(s_raw_buf));
    if (raw_len == 0) {
        ESP_LOGW(TAG, "rawpacket_build_ax25_ui failed");
        return;
    }

    // 2. HDLC フレーミング（プリアンブル20フラグ + ビットスタッフィング）
    size_t hdlc_bits  = 0;
    size_t hdlc_bytes = ax25_hdlc_frame(
        s_raw_buf, raw_len, AFSK_PREAMBLE,
        s_hdlc_buf, sizeof(s_hdlc_buf), &hdlc_bits, NULL);
    if (hdlc_bytes == 0) {
        ESP_LOGW(TAG, "ax25_hdlc_frame failed");
        return;
    }

    ESP_LOGI(TAG, "TX %s>%s  FCS=0x%04X  raw=%zu bytes  HDLC=%zu bytes / %zu bits",
             meta.src_call, meta.dest_call, meta.fcs,
             raw_len, hdlc_bytes, hdlc_bits);

    // 3. 送信（NRZI 状態を mark からリセット）
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
 * @brief AFSK テスト送信タスク
 *
 * 起動3秒後から5秒間隔でテストパケットを繰り返し送信する。
 *
 * @param pvParameters 未使用
 */
static void afsk_pwm_test_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(3000));

    for (;;) {
        afsk_send_test_packet();
        vTaskDelay(pdMS_TO_TICKS(5000));
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

    ESP_LOGI(TAG, "AFSK PWM test init: GPIO%d  Mark=%dHz Space=%dHz %dbaud",
             AFSK_PWM_GPIO, AFSK_MARK_HZ, AFSK_SPACE_HZ, AFSK_BAUD);

    // Core 1 に固定して bit-bang タイミングの影響を最小化
    xTaskCreatePinnedToCore(afsk_pwm_test_task, "afsk_test",
                            4096, NULL, 5, NULL, 1);
}
