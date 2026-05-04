#ifndef AFSK_PWM_TEST_H
#define AFSK_PWM_TEST_H

/**
 * @file afsk_pwm_test.h
 * @brief Bell 202 AFSK 変調テスト出力（LEDC PWM版）
 *
 * テスト構成:
 *   GPIO AFSK_PWM_GPIO ─── 10μF ─── 100Ω ─── スピーカ ─── GND
 *
 * I2S モデムが完成したら本モジュールは削除する予定。
 */

#define AFSK_PWM_GPIO  17   ///< PWM 出力 GPIO（空きピンから割り当て）

/**
 * @brief AFSK PWM テストモジュールを初期化し送信タスクを起動する
 *
 * 起動3秒後から5秒間隔でテスト AX.25 UI フレームを繰り返し送信する。
 * Bell 202: 1200 baud, Mark=1200Hz(1), Space=2200Hz(0), NRZI 符号化。
 */
void afsk_pwm_test_init(void);

#endif /* AFSK_PWM_TEST_H */
