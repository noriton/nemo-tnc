#ifndef AX25_H
#define AX25_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// アドレス情報のまとめ役
typedef struct {
    const char *dest_call; // 送信先 (例: "APRS  ")
    uint8_t dest_ssid;
    const char *src_call;  // 送信元 (例: 自分のコールサイン)
    uint8_t src_ssid;
} ax25_address_t;

/**
 * @brief UIフレームを組み立てる
 * @param addr アドレス情報
 * @param info 送信したいデータ本体
 * @param info_len データの長さ
 * @param out_buf 生成されたパケットの格納先
 * @return 生成されたパケットの総バイト数
 */
size_t ax25_build_ui_frame(const ax25_address_t *addr, const uint8_t *info, size_t info_len, uint8_t *out_buf);

/**
 * @brief コールサインをAX.25形式（7バイト）に変換する
 * @param out_buf 7バイトの出力先
 * @param callsign 入力文字列 (例: "JH1XXX")
 * @param ssid SSID (0-15)
 * @param is_last アドレスフィールドの最後かどうか（最後なら1にする）
 */
void encode_callsign(uint8_t *out_buf, const char *callsign, uint8_t ssid, bool is_last);

/**
 * @brief AX.25形式のコールサインを文字列にデコードする
 * @param in_buf 7バイトのAX.25形式コールサイン
 * @param callsign 出力先の文字列バッファ
 * @param ssid 出力先のSSIDポインタ
 * @return デコードしたコールサインの長さ
 */
size_t decode_callsign(const uint8_t *in_buf, char *callsign, uint8_t *ssid);

/**
 * @brief AX.25 FCS (CRC-16-CCITT) calculation
 * @param data FCS計算対象データ
 * @param len データ長
 * @return 計算されたFCS値
 */
uint16_t ax25_fcs_calculate(const uint8_t *data, size_t len);


#endif // AX25_H
