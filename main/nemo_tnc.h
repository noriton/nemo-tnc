/*
 * nemo_tnc.h
 *
 * NEMO-TNC モジュール共通ヘッダファイル
 *
*/
#ifndef NEMO_TNC_H
#define NEMO_TNC_H

// USB 初期化関数のプロトタイプ宣言
void tnc_init(void);
void usb_init(void);

#ifdef TAGNAME
const char *TAG = TAGNAME;
#else
extern const char *TAG;
#endif // TAGNAME

#endif // NEMO_TNC_H