#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>

/**
 * @brief コールサインをAX.25形式（7バイト）に変換する
 * @param out_buf 7バイトの出力先
 * @param callsign 入力文字列 (例: "JH1XXX")
 * @param ssid SSID (0-15)
 * @param is_last アドレスフィールドの最後かどうか（最後なら1にする）
 */
void encode_callsign(uint8_t *out_buf, const char *callsign, uint8_t ssid, bool is_last) {
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