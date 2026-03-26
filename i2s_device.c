#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2s_device.h"
#include "freertos/queue.h"


#define SAMPLE_RATE  9600

static const char *TAG = "NEMO_TNC_I2S";

i2s_chan_handle_t tx_chan;
i2s_chan_handle_t rx_chan;


void init_nemo_tnc_hardware(void)
{
    // 1. V4220Mのハードウェアリセットシーケンス
    gpio_set_direction(CODEC_RST_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(CODEC_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CODEC_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 2. I2Sチャンネルの割り当て（Masterモード）
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_chan, &rx_chan);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "I2S channels created successfully");
    } else {
        ESP_LOGE(TAG, "I2S channel creation failed");
        return;
    }

    // 3. I2S標準フォーマット（24bitステレオ）とピンマッピング
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_24BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_PIN,
            .bclk = I2S_BCLK_PIN,
            .ws   = I2S_WS_PIN,
            .dout = I2S_DOUT_PIN,
            .din  = I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    // 4. APLLを有効化し、V4220M用の正確なMCLK(2.4576MHz)を生成
    std_cfg.clk_cfg.use_apll = true;
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    // 5. ドライバの初期化と有効化
    i2s_channel_init_std_mode(tx_chan, &std_cfg);
    i2s_channel_init_std_mode(rx_chan, &std_cfg);
    i2s_channel_enable(tx_chan);
    i2s_channel_enable(rx_chan);

    ESP_LOGI(TAG, "V4220M initialized and I2S running.");
}

void tnc_processing_task(void *pvParameters)
{
    I2sSample rx_data;
    I2sSample tx_data;
    size_t bytes_read = 0;
    size_t bytes_written = 0;

    init_nemo_tnc_hardware();

    // DSP処理ループ
    for (;;) {
        esp_err_t res = i2s_channel_read(rx_chan, &rx_data, sizeof(I2sSample), &bytes_read, portMAX_DELAY);
        
        if (res == ESP_OK) {
            // ここにAFSKのゼロクロス判定や、送信時のサイン波生成（DSP処理）を実装します
            // テスト用：受信した音をそのまま送信側へループバック
            tx_data.left = rx_data.left;
            tx_data.right = rx_data.right;
        } else if (res == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2S Read Timeout");
        } else {
            ESP_LOGE(TAG, "I2S Read Error");
        }

        // 処理したデータをV4220MのDACへ書き込む
        i2s_channel_write(tx_chan, &tx_data, sizeof(I2sSample), &bytes_written, portMAX_DELAY);
    }
}


static const char *TAG = "NEMO_TNC_TASKS";

typedef struct I2sSample {
    int32_t left;
    int32_t right;
} I2sSample;

// ポートごとのデータ受け渡し用キュー
QueueHandle_t queue_rx_port0;
QueueHandle_t queue_rx_port1;

// I2Sドライバからデータを受け取り、各ポートへ分配するタスク
void task_i2s_interface(void *pvParameters)
{
    I2sSample rx_data;
    size_t bytes_read;

    for (;;) {
        esp_err_t res = i2s_channel_read(rx_chan, &rx_data, sizeof(I2sSample), &bytes_read, portMAX_DELAY);
        
        if (res == ESP_OK) {
            // Lch(Port0)とRch(Port1)のデータを分離して、それぞれのDSPキューへ送信（ブロック時間0）
            xQueueSend(queue_rx_port0, &rx_data.left, 0);
            xQueueSend(queue_rx_port1, &rx_data.right, 0);
        } else if (res == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2S Read Timeout");
        } else {
            ESP_LOGE(TAG, "I2S Read Error");
        }
    }
}

// Port 0 (Lch) のモデム処理タスク
void task_modem_port0(void *pvParameters)
{
    int32_t sample_data;

    for (;;) {
        // I2Sタスクからデータが送られてくるのを待つ
        if (xQueueReceive(queue_rx_port0, &sample_data, portMAX_DELAY) == pdTRUE) {
            // ここにPort 0のAFSKデコード処理（DSP）を実装
        }
    }
}

// Port 1 (Rch) のモデム処理タスク
void task_modem_port1(void *pvParameters)
{
    int32_t sample_data;

    for (;;) {
        // I2Sタスクからデータが送られてくるのを待つ
        if (xQueueReceive(queue_rx_port1, &sample_data, portMAX_DELAY) == pdTRUE) {
            // ここにPort 1のAFSKデコード処理（DSP）を実装
        }
    }
}

// メイン関数での起動例
void app_main(void)
{
    // キューの作成（バッファサイズはサンプリングレート等に応じて調整）
    queue_rx_port0 = xQueueCreate(256, sizeof(int32_t));
    queue_rx_port1 = xQueueCreate(256, sizeof(int32_t));

    // I2Sの初期化（前回の設定関数を呼ぶ）
    init_v4220_and_i2s();

    // タスクの起動（xTaskCreatePinnedToCore を使ってコアを割り当て）
    xTaskCreatePinnedToCore(task_i2s_interface, "I2S_Task", 4096, NULL, 5, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(task_modem_port0, "Modem0_Task", 8192, NULL, 4, NULL, 0); // Core 0に固定
    xTaskCreatePinnedToCore(task_modem_port1, "Modem1_Task", 8192, NULL, 4, NULL, 1); // Core 1に固定
}