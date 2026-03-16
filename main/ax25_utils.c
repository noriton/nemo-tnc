#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief コールサインをAX.25形式（7バイト）に変換する
 * @param out_buf 7バイトの出力先
 * @param callsign 入力文字列 (例: "JH1XXX")
 * @param ssid SSID (0-15)
 * @param is_last アドレスフィールドの最後かどうか（最後なら1にする）
 */
void encode_callsign(uint8_t *out_buf, const char *callsign, uint8_t ssid, bool is_last)
{
    int i;
    int len = strlen(callsign);

    // 1-6バイト目: コールサイン本体 (スペース埋め)
    for (i = 0; i < 6; i++) {
        if (i < len) {
            out_buf[i] = (uint8_t)toupper((int)callsign[i]) << 1;
        } else {
            out_buf[i] = ' ' << 1; // 0x20 << 1 = 0x40
        }
    }

    // 7バイト目: SSIDと各種フラグ
    // ビット構成: [Res][Res][Res][Res][SSID(4bit)][Extension(1bit)]
    // 通常、予約ビット(Res)は1にセットされる
    uint8_t ssid_byte = 0x60 | ((ssid & 0x0F) << 1);
    
    if (is_last) {
        ssid_byte |= 0x01; // Extension bit: 1 = アドレス終了
    } else {
        ssid_byte &= 0xFE; // Extension bit: 0 = 次のアドレスがある
    }
    
    out_buf[6] = ssid_byte;
}

/**
 * @brief AX.25形式（7バイト）のアドレスフィールドをコールサイン文字列にデコードする
 *
 * エンコード仕様（encode_callsign と対称）:
 *   bytes 0-5 : 各文字を 1-bit 左シフトした値、スペース (0x40) でパディング
 *   byte  6   : bit7=H-bit, bit6-5=res(1), bit4-1=SSID(4bit), bit0=is_last
 *
 * @param in_buf   7バイトのAX.25形式アドレス
 * @param callsign デコードしたコールサインの格納先（7バイト以上）
 * @param ssid     デコードしたSSIDの格納先（NULL可）
 * @return デコードしたコールサインの文字数（終端NULLを除く）
 */
size_t decode_callsign(const uint8_t *in_buf, char *callsign, uint8_t *ssid)
{
    size_t len = 0;

    // バイト 0-5: 1-bit 右シフトして元の文字に戻す、末尾のスペースは除外
    for (int i = 0; i < 6; i++) {
        char c = (char)(in_buf[i] >> 1);
        if (c == ' ') break; // スペース埋め = 終端
        if (callsign != NULL) {
            callsign[len] = c;
        }
        len++;
    }
    if (callsign != NULL) {
        callsign[len] = '\0';
    }

    // バイト 6: SSID = bits 4-1
    if (ssid != NULL) {
        *ssid = (in_buf[6] & 0x1E) >> 1;
    }

    return len;
}