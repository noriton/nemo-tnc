#ifndef I2S_DEVICE_H
#define I2S_DEVICE_H

#include <stdint.h>

/*
 * V4220M コーデック接続 (ESP32-S3 側 GPIO)
 * ESP32-S3 = I2S マスター / V4220M = I2S スレーブ
 *
 *   IO47 -> RSTN  (コーデックリセット, active low)
 *   IO42 -> XTI   (コーデック外部クロック入力 = ESP32 I2S MCLK 出力)
 *   IO41 -> LRCK  (ワードセレクト,  ESP32 -> コーデック)
 *   IO40 -> SCLK  (ビットクロック, ESP32 -> コーデック)
 *   IO39 -> DOUT  (コーデックのシリアルデータ出力 -> ESP32 I2S din, RX方向)
 *   IO48 -> DIN   (コーデックのシリアルデータ入力 <- ESP32 I2S dout, TX方向)
 *
 * 注意: V4220M データシートより、DOUT ピンに 47kΩ プルダウン抵抗が
 * 実装されていると V4220M 自身が I2S マスターモードで起動してしまい、
 * SCLK/LRCK を V4220M 側が出力しようとして ESP32 側と衝突する。
 * このボードでは ESP32 がマスターである前提のため、DOUT に
 * 47kΩ プルダウンが実装されていないことを確認すること。
 *
 * DIN は当初 IO38 を想定していたが、ESP32-S3-DevKitC-1 v1.1 では
 * GPIO38 がオンボード RGB LED と共用のため IO48 に変更した。
 * GPIO48 は v1.0 でオンボード LED に使われていたが v1.1 では
 * GPIO38 に移動しており、v1.1 ボードでは空きピンとして使える。
 */
#define I2S_CODEC_RST_GPIO   47
#define I2S_CODEC_MCLK_GPIO  42
#define I2S_CODEC_LRCK_GPIO  41
#define I2S_CODEC_SCLK_GPIO  40
#define I2S_CODEC_DOUT_GPIO  39   /* コーデック DOUT -> ESP32 din */
#define I2S_CODEC_DIN_GPIO   48   /* コーデック DIN  <- ESP32 dout */

/* V4220M の DIF1/DIF0 ピンがボード上で 00 (I2S, up to 24bit) に
 * 固定配線されていることが前提。標準 I2S フォーマットで動作する。
 *
 * 48kHz 標準オーディオレートを採用（旧 ADC oneshot 方式の 9600Hz は
 * adc_oneshot_read の実行時間による制約であり、I2S/DMA 化で不要になった）。
 * afsk_demod.c 側の BPF 係数・DEMOD_SPB・ビットタイミングは
 * 48kHz 用に別途作り直す必要がある（現在は 9600Hz 前提のまま）。 */
#define I2S_SAMPLE_RATE  48000

typedef struct {
    int32_t left;
    int32_t right;
} i2s_sample_t;

/**
 * @brief V4220M コーデックをリセットし、I2S (std, マスター, 24bit stereo)
 *        チャンネルを初期化する
 */
void i2s_device_init(void);

/**
 * @brief 動作確認用: V4220M のアナログ側 (DAC出力 -> ADC入力) をジャンパ線で
 *        直結したループバック構成で疎通確認するテストタスクを起動する
 *
 * Mark(1200Hz)/Space(2200Hz) のサイン波を左右両チャンネルへ送信し、
 * 受信側の左右チャンネルそれぞれのピーク振幅と推定周波数を1秒ごとに
 * ログ出力する。外部入力回路 (マイク/ライン入力) がまだ無い段階で、
 * I2S 配線・V4220M の DAC/ADC が正しく動作しているかを確認するための
 * 暫定テスト。本番の AFSK モデム実装ができたら削除する。
 */
void i2s_device_start_analog_loopback_test(void);

#endif /* I2S_DEVICE_H */
