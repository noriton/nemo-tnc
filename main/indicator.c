#include "indicator.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define LED_STRIP_GPIO       38
#define LED_STRIP_BLINK_GPIO  38
#define LED_STRIP_COUNT      1
#define LED_STRIP_MAX_LEDS    1

static led_strip_handle_t led_strip;

void indicator_init(void) {
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
}

void indicator_set_color(uint8_t r, uint8_t g, uint8_t b) {
    if (led_strip) {
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
    }
}

void indicator_status_usb_conn(void) { indicator_set_color(0, 0, 255); } // Blue
void indicator_status_data_rx(void)  { indicator_set_color(0, 255, 0); } // Green
void indicator_status_error(void)    { indicator_set_color(255, 0, 0); } // Red
void indicator_status_off(void)      { indicator_set_color(0, 0, 0); } // Off