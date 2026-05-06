#ifndef AFSK_DEMOD_H
#define AFSK_DEMOD_H

/** ADC 入力 GPIO (暫定: GPIO3 = ADC1_CH2) */
#define AFSK_DEMOD_GPIO  3

/**
 * @brief Bell 202 AFSK 復調モジュールを初期化する
 *
 * ADC サンプリングタイマー (9600 Hz) を起動し、
 * 受信した AX.25 フレームを rx_ringbuf[0] へ投入する。
 * 送信側 (afsk_pwm_test) と同時動作可能。
 */
void afsk_demod_init(void);

#endif /* AFSK_DEMOD_H */
