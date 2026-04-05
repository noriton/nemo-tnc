#include "indicator.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h" // 時刻取得用
#include "nvs_if.h"

#define LED_STRIP_GPIO       4
#define LED_STRIP_BLINK_GPIO  4
#define LED_STRIP_COUNT      1
#define LED_STRIP_MAX_LEDS    1

static led_strip_handle_t led_strip;
static tnc_state_t current_state = TNC_ST_BOOT;
static tnc_state_t state_before_off = TNC_ST_IDLE; // 消灯前の状態を保存
static int led_forced_off = 0; // 1: 強制消灯中

static int64_t last_rx_time = 0; // 最後にRXステートになった時刻(マイクロ秒)

// タスクハンドル（念のため保持）
static TaskHandle_t indicator_task_handle = NULL;

// 状態を変更する関数
void indicator_set_state(tnc_state_t new_state)
{
    if (new_state == TNC_ST_RX) {
        last_rx_time = esp_timer_get_time(); // 現在時刻（μs）を記録
    }
    if (led_forced_off) {
        state_before_off = new_state; // 消灯中は復帰先だけ更新
        return;
    }
    current_state = new_state;
}

void indicator_set_forced_off(int off)
{
    if (off) {
        state_before_off = current_state;
        led_forced_off = 1;
        current_state = TNC_ST_OFF;
    } else {
        led_forced_off = 0;
        current_state = state_before_off;
    }
    nvs_save_led_forced_off(led_forced_off);
}

// LEDを制御する独立したタスク
static void indicator_task(void *pvParameters)
{
    for (;;) {
        switch (current_state) {
            case TNC_ST_BOOT:
                // 高速点滅で「準備中」感
                led_strip_set_pixel(led_strip, 0, 0, 0, 20);
                led_strip_refresh(led_strip);
                vTaskDelay(pdMS_TO_TICKS(100));
                led_strip_set_pixel(led_strip, 0, 0, 0, 0);
                led_strip_refresh(led_strip);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case TNC_ST_IDLE:
                // 呼吸（フェードイン・アウト）
                for (int i = 2; i < 11; i++) {
                    led_strip_set_pixel(led_strip, 0, i, 0, i);
                    led_strip_refresh(led_strip);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    if (current_state != TNC_ST_IDLE) break;
                }
                for (int i = 10; i >= 2; i--) {
                    led_strip_set_pixel(led_strip, 0, i, 0, i);
                    led_strip_refresh(led_strip);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    if (current_state != TNC_ST_IDLE) break;
                }
                break;

            case TNC_ST_RX:
                // 受信中はパチパチ光る
                led_strip_set_pixel(led_strip, 0, 0, 50, 0);
                led_strip_refresh(led_strip);
                vTaskDelay(pdMS_TO_TICKS(50));
                led_strip_set_pixel(led_strip, 0, 0, 0, 0);
                led_strip_refresh(led_strip);
                vTaskDelay(pdMS_TO_TICKS(50));
                
                int64_t now = esp_timer_get_time();
                // 200,000μs = 200ms 以上経過していたらIDLEへ
                if ((now - last_rx_time) > 200000) {
                    current_state = TNC_ST_IDLE;
                }
                break;

           case TNC_ST_OFF:
                // 消灯
                led_strip_set_pixel(led_strip, 0, 0, 0, 0);
                led_strip_refresh(led_strip);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            default:
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
}

void indicator_init(void)
{
    // NVSからLED強制消灯状態を復元
    int saved_off = 0;
    nvs_load_led_forced_off(&saved_off);
    if (saved_off) {
        led_forced_off = 1;
        current_state = TNC_ST_OFF;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_BLINK_GPIO,
        .max_leds = LED_STRIP_MAX_LEDS,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, 
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 5 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
    ESP_LOGI("LED", "Indicator (WS2812) initialized on GPIO %d", LED_STRIP_BLINK_GPIO);

    xTaskCreate(indicator_task, "indicator_task", 2048, NULL, 5, &indicator_task_handle);
}


static void indicator_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (led_strip) {
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
    }
}

#ifdef COLOR_LED_INDICATOR
void indicator_status_usb_conn(void) { indicator_set_color(0, 0, 255); } // Blue
void indicator_status_data_rx(void)  { indicator_set_color(0, 255, 0); } // Green
void indicator_status_error(void)    { indicator_set_color(255, 0, 0); } // Red
void indicator_status_off(void)      { indicator_set_color(0, 0, 0); } // Off
#else
void indicator_status_usb_conn(void) { indicator_set_state(TNC_ST_IDLE); }
void indicator_status_data_rx(void)  { indicator_set_state(TNC_ST_RX); }
void indicator_status_error(void)    { indicator_set_state(TNC_ST_ERROR); }
void indicator_status_off(void)      { indicator_set_state(TNC_ST_OFF); } 
#endif
