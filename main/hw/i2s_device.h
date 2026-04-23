#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define I2S_MCLK_PIN  4
#define I2S_BCLK_PIN  5
#define I2S_WS_PIN    6
#define I2S_DOUT_PIN  7
#define I2S_DIN_PIN   15
#define CODEC_RST_PIN 10
#define SAMPLE_RATE   9600

typedef struct I2sSample {
    int32_t left;
    int32_t right;
} I2sSample;

static const char *TAG = "NEMO_TNC";
static i2s_chan_handle_t tx_chan;
static i2s_chan_handle_t rx_chan;

