#ifndef AX25_H
#define AX25_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// アドレス情報のまとめ役
typedef struct ax25_address {
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


/**
 * @brief 送信先コールサインの検証とAX.25アドレスへのエンコード
 *
 * 特殊アドレス (CQ, BEACON, IDENT 等) はコールサイン検証を省略する。
 * 記号類は除去し、SSIDは省略時 0 とする。
 *
 * @param out_buf 7バイトの出力先
 * @param dest    送信先コールサイン文字列
 * @param is_last アドレスフィールドの最後かどうか
 * @return        1=有効(エンコード済み), 0=無効
 */
int ax25_encode_dest(uint8_t out_buf[7], const char *dest, bool is_last);

/**
 * @brief AX.25 UIフレームから情報フィールドを取り出す
 * @param frame 受信したAX.25フレーム全体（FCS込みでも可だが、使わない）
 * @param frame_len フレーム長
 * @param out_info 情報フィールドの格納先
 * @param max_info_len 格納先の最大サイズ
 * @return 取得した情報フィールドのバイト数（負の値はエラー）
 */
int ax25_decode_ui_info(const uint8_t *frame, size_t frame_len, char *out_info, size_t max_info_len);


#endif // AX25_H
