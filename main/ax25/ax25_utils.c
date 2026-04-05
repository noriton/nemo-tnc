#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "ax25.h"
#include "callsign.h"

// AX.25で使われる特殊送信先アドレス（コールサイン検証を省略する）
static const char * const AX25_SPECIAL_DEST[] = {
    "CQ", "BEACON", "IDENT", "QST", "NOCALL",
    "ALL", "NODE", "NODES", "ID",
    NULL
};

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

/**
 * @brief コールサイン文字列をクリーニングしてベース部とSSIDに分離する（内部ヘルパー）
 *
 * @param dest     入力文字列
 * @param out_base ベース部の格納先（7バイト以上）
 * @param out_ssid SSIDの格納先
 * @return 1=成功, 0=失敗
 */
static int parse_address(const char *dest, char out_base[7], int *out_ssid)
{
    if (dest == NULL) return 0;

    // 1. 記号類を除去して大文字化（英数字と'-'のみ残す）
    char cleaned[16] = {0};
    int ci = 0;
    for (int i = 0; dest[i] != '\0' && ci < 15; i++) {
        char c = (char)toupper((unsigned char)dest[i]);
        if (isalnum((unsigned char)c) || c == '-') {
            cleaned[ci++] = c;
        }
    }
    if (ci == 0) return 0;

    // 2. ベース部とSSIDを分離（SSIDデフォルト=0）
    int  ssid = 0;
    char *dash = strchr(cleaned, '-');

    if (dash != NULL) {
        int base_len = (int)(dash - cleaned);
        if (base_len == 0 || base_len > 6) return 0;
        memset(out_base, 0, 7);
        strncpy(out_base, cleaned, (size_t)base_len);
        // '-'以降を数値変換（1〜2桁、0〜15）
        int v = 0, digits = 0;
        for (char *p = dash + 1; *p != '\0'; p++) {
            if (!isdigit((unsigned char)*p)) { digits = 0; break; }
            v = v * 10 + (*p - '0');
            digits++;
        }
        if (digits > 0 && v <= 15) ssid = v;
    } else {
        if (ci > 6) return 0;
        memset(out_base, 0, 7);
        strncpy(out_base, cleaned, 6);
    }

    *out_ssid = ssid;
    return 1;
}

/**
 * @brief 送信先コールサインがAX.25で使用可能か検証する
 *
 * 特殊アドレス (CQ, BEACON, IDENT 等) は常に有効。
 * それ以外は callsign_validate() で検証する。
 *
 * @param dest 検証するコールサイン文字列
 * @return true=有効, false=無効
 */
bool ax25_validate_dest(const char *dest)
{
    char base[7];
    int  ssid;
    if (!parse_address(dest, base, &ssid)) return false;

    // 特殊アドレスは常に有効
    for (int i = 0; AX25_SPECIAL_DEST[i] != NULL; i++) {
        if (strcmp(base, AX25_SPECIAL_DEST[i]) == 0) return true;
    }

    // 通常コールサインのバリデーション
    char validate_buf[16];
    if (ssid > 0) {
        snprintf(validate_buf, sizeof(validate_buf), "%s-%d", base, ssid);
    } else {
        strncpy(validate_buf, base, sizeof(validate_buf) - 1);
        validate_buf[sizeof(validate_buf) - 1] = '\0';
    }
    return callsign_validate(validate_buf);
}

/**
 * @brief 送信先コールサインをAX.25アドレスにエンコードする
 *
 * バリデーションは行わない。呼び出し前に ax25_validate_dest() で検証すること。
 * 特殊アドレス (CQ, BEACON, IDENT 等) もそのままエンコードする。
 *
 * @param out_buf 7バイトの出力先
 * @param dest    送信先コールサイン文字列
 * @param is_last アドレスフィールドの最後かどうか
 * @return        1=成功, 0=パースエラー
 */
int ax25_encode_dest(uint8_t out_buf[7], const char *dest, bool is_last)
{
    if (out_buf == NULL) return 0;

    char base[7];
    int  ssid;
    if (!parse_address(dest, base, &ssid)) return 0;

    encode_callsign(out_buf, base, (uint8_t)ssid, is_last);
    return 1;
}