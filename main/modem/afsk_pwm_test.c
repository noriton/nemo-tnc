#include "afsk_pwm_test.h"
#include "driver/ledc.h"
#include "esp_timer.h"
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

// HDLC 出力バッファ（static: スタック節約）
static uint8_t s_hdlc_buf[AX25_HDLC_OUT_MAX(RAW_PACKET_MAX_LEN_WITH_FCS, 20)];

/* ======================================================================
 * ベンチテスト用自動送信 (TX→LPF→RX ハードウェアループバック確認用)
 *
 * 有効時: 起動 3 秒後から 5 秒間隔でテスト AX.25 UI フレームを afsk_tx_buf
 * へ自動投入する。KISS/USB 経由の実パケットが来なくても GPIO17 から
 * PWM が出るようになる。実運用時は下の #define をコメントアウトすること。
 * ====================================================================== */
#define AFSK_PWM_SELFTEST

#ifdef AFSK_PWM_SELFTEST
static const char s_selftest_info[] = "NEMO-TNC TEST";

static void afsk_pwm_selftest_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(3000));

    for (;;) {
        raw_tx_item_t item;
        memset(&item.meta, 0, sizeof(item.meta));
        item.meta.version    = TNC_META_VERSION_1;
        item.meta.type       = META_TYPE_DATA_UI;
        item.meta.header_len = sizeof(item.meta);
        item.meta.port_id    = 0;
        item.meta.payload_len = (uint16_t)(sizeof(s_selftest_info) - 1);
        strncpy(item.meta.src_call,  "JH1FBM", sizeof(item.meta.src_call) - 1);
        strncpy(item.meta.dest_call, "APTEST", sizeof(item.meta.dest_call) - 1);

        size_t raw_len = rawpacket_build_ax25_ui(
            &item.meta, (const uint8_t *)s_selftest_info,
            item.data, sizeof(item.data));
        if (raw_len > 0) {
            item.meta.payload_len = (uint16_t)raw_len;
            size_t item_sz = RAW_TX_ITEM_SIZE(raw_len);
            if (afsk_tx_buf != NULL) {
                BaseType_t res = xRingbufferSend(afsk_tx_buf, &item, item_sz, pdMS_TO_TICKS(10));
                if (res != pdTRUE) {
                    ESP_LOGW(TAG, "selftest: afsk_tx_buf full, frame dropped");
                }
            }
        } else {
            ESP_LOGW(TAG, "selftest: build_ax25_ui failed");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
#endif /* AFSK_PWM_SELFTEST */

// ---------------------------------------------------------------------------
// 送信ステート（esp_timer コールバックと AFSK タスクで共有）
// timer callback は esp_timer タスクから呼ばれ AFSK タスクは ulTaskNotifyTake
// でブロックしているため、送信中に両者が同時にステートを書き換えることはない。
// ---------------------------------------------------------------------------
static volatile size_t  s_bit_idx;      // 次に送出するビット位置
static volatile size_t  s_bit_total;    // 総ビット数
static volatile bool    s_nrzi_state;   // NRZI 現在状態: true=mark(1200Hz)

static esp_timer_handle_t s_bit_timer = NULL;
static TaskHandle_t       s_tx_task   = NULL;

// ---------------------------------------------------------------------------
// LEDC ヘルパー
// ---------------------------------------------------------------------------

static inline void afsk_set_freq(bool is_mark)
{
    ledc_set_freq(LEDC_LOW_SPEED_MODE, AFSK_LEDC_TIMER,
                  is_mark ? AFSK_MARK_HZ : AFSK_SPACE_HZ);
}

static void afsk_tx_on(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, AFSK_LEDC_CHANNEL, AFSK_LEDC_DUTY);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, AFSK_LEDC_CHANNEL);
}

static void afsk_tx_off(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, AFSK_LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, AFSK_LEDC_CHANNEL);
}

// ---------------------------------------------------------------------------
// esp_timer コールバック: 1ビット送出 → 次ビットのタイマーを再始動
//
// esp_timer は専用タスクから呼ばれる（ISR ではない）。
// AFSK タスクは ulTaskNotifyTake でブロック中なので競合しない。
// ---------------------------------------------------------------------------

/**
 * @brief 1ビット送出コールバック
 *
 * NRZI エンコードして LEDC 周波数を更新し、次のビットを AFSK_BIT_US 後に予約する。
 * 全ビット送出後は PWM を無音にして AFSK タスクへ通知する。
 */
static void afsk_bit_timer_cb(void *arg)
{
    size_t i = s_bit_idx;

    if (i >= s_bit_total) {
        // 全ビット送出完了: 無音にして AFSK タスクを起床
        afsk_tx_off();
        xTaskNotifyGive(s_tx_task);
        return;
    }

    // NRZI: 0 → 周波数反転, 1 → 維持
    uint8_t bit = (s_hdlc_buf[i >> 3] >> (i & 7u)) & 1u;
    if (bit == 0) s_nrzi_state = !s_nrzi_state;
    afsk_set_freq(s_nrzi_state);
    s_bit_idx = i + 1;

    // 次のビットを AFSK_BIT_US 後に予約（失敗時はTX強制終了）
    esp_err_t ret = esp_timer_start_once(s_bit_timer, AFSK_BIT_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_once: %s", esp_err_to_name(ret));
        afsk_tx_off();
        if (s_tx_task) xTaskNotifyGive(s_tx_task);
    }
}

// ---------------------------------------------------------------------------
// 送信メイン処理
// ---------------------------------------------------------------------------

/**
 * @brief raw_tx_item_t を Bell 202 AFSK で送出する（CPU をブロックしない）
 *
 * 処理フロー:
 *   1. ax25_hdlc_frame()       → HDLC フレーミング（プリアンブル + ビットスタッフ）
 *   2. esp_timer start_once()  → AFSK_BIT_US ごとにコールバックで 1 ビット送出
 *   3. ulTaskNotifyTake()      → 全ビット完了まで AFSK タスクをスリープ
 *
 * esp_timer コールバックが bit 送出を担うため、送信中に CPU は他タスクを実行できる。
 *
 * @param item afsk_tx_buf から受け取った raw_tx_item_t ポインタ
 */
static void afsk_transmit(const raw_tx_item_t *item)
{
    if (s_bit_timer == NULL) {
        ESP_LOGW(TAG, "timer not initialized, TX skipped");
        return;
    }

    size_t raw_len = item->meta.payload_len;

    // 不正な長さは弾く（s_hdlc_buf サイズ超え → hdlc_frame 内でも検出されるが二重チェック）
    if (raw_len == 0 || raw_len > RAW_PACKET_MAX_LEN_WITH_FCS) {
        ESP_LOGW(TAG, "invalid raw_len=%zu, skipped", raw_len);
        return;
    }

    size_t hdlc_bits  = 0;
    size_t hdlc_bytes = ax25_hdlc_frame(
        item->data, raw_len, AFSK_PREAMBLE,
        s_hdlc_buf, sizeof(s_hdlc_buf), &hdlc_bits, NULL);
    if (hdlc_bytes == 0) {
        ESP_LOGW(TAG, "hdlc_frame failed (raw_len=%zu)", raw_len);
        return;
    }

    ESP_LOGI(TAG, "TX port%d %s>%s FCS=0x%04X  raw=%zu  HDLC=%zu bytes / %zu bits",
             item->meta.port_id,
             item->meta.src_call, item->meta.dest_call,
             item->meta.fcs, raw_len, hdlc_bytes, hdlc_bits);

    // 送信ステート初期化
    s_bit_idx    = 0;
    s_bit_total  = hdlc_bits;
    s_nrzi_state = true;
    s_tx_task    = xTaskGetCurrentTaskHandle();

    // PWM 有効化 → 最初のビットをほぼ即時に送出（1µs 後に予約）
    afsk_tx_on();
    esp_err_t start_ret = esp_timer_start_once(s_bit_timer, 1);
    if (start_ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_once (initial): %s", esp_err_to_name(start_ret));
        afsk_tx_off();
        return;
    }

    // 全ビット送出完了（コールバックから通知）まで待機
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

// ---------------------------------------------------------------------------
// タスク
// ---------------------------------------------------------------------------

/**
 * @brief AFSK 送信タスク
 *
 * afsk_tx_buf を監視し、パケットが到着したら AFSK で送出する。
 * 送信中は esp_timer コールバックに制御を委譲し、このタスク自体はスリープする。
 *
 * @param pvParameters 未使用
 */
static void afsk_pwm_test_task(void *pvParameters)
{
    // afsk_tx_buf が NULL（作成失敗）なら即終了してタスクリソースを解放
    if (afsk_tx_buf == NULL) {
        ESP_LOGE(TAG, "afsk_tx_buf is NULL, task exiting");
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        size_t item_size = 0;
        raw_tx_item_t *item = (raw_tx_item_t *)xRingbufferReceive(
            afsk_tx_buf, &item_size, portMAX_DELAY);
        if (item == NULL) continue;

        afsk_transmit(item);
        vRingbufferReturnItem(afsk_tx_buf, (void *)item);
    }
}

// ---------------------------------------------------------------------------
// 初期化 API
// ---------------------------------------------------------------------------

void afsk_pwm_test_init(void)
{
    init_hdlc_tables();

    // LEDC タイマー初期化（8bit分解能、mark 周波数で開始）
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = AFSK_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = AFSK_MARK_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return;
    }

    // LEDC チャンネル初期化（duty=0 で無音状態から開始）
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = AFSK_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = AFSK_LEDC_CHANNEL,
        .timer_sel  = AFSK_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return;
    }

    // ビット送出用ワンショットタイマー作成
    // ESP_TIMER_TASK: コールバックは専用タスクから呼ばれる（ISR ではない）
    esp_timer_create_args_t targs = {0};
    targs.callback        = afsk_bit_timer_cb;
    targs.name            = "afsk_bit";
    targs.dispatch_method = ESP_TIMER_TASK;

    err = esp_timer_create(&targs, &s_bit_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "AFSK PWM init: GPIO%d  Mark=%dHz Space=%dHz %dbaud  (timer-driven)",
             AFSK_PWM_GPIO, AFSK_MARK_HZ, AFSK_SPACE_HZ, AFSK_BAUD);

    xTaskCreatePinnedToCore(afsk_pwm_test_task, "afsk_test",
                            4096, NULL, 5, NULL, 1);

#ifdef AFSK_PWM_SELFTEST
    xTaskCreatePinnedToCore(afsk_pwm_selftest_task, "afsk_selftest",
                            4096, NULL, 5, NULL, 1);
#endif
}
