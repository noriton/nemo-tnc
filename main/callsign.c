#include "callsign.h"
#include <string.h>
#include <ctype.h>

void callsign_normalize(char *call)
{
    for (int i = 0; call[i]; i++) {
        call[i] = toupper((unsigned char)call[i]);
    }
}

/*
 * BASEのITU-R M.1033構造チェック
 *
 * BASE = PREFIX(1〜2文字の英数字) + AREA(1桁の数字) + SUFFIX(1〜5文字の英数字)
 * 総長: 3〜8文字
 *
 * 例: W1AW, JH1FBM, VK2ABC, 9A1AA, 7Z1HL, VK2FABC, 8J1RL100 (記念局)
 *
 * 解析方針:
 *   prefix_len=1 を先に試し、エリア数字が続かなければ prefix_len=2 を試みる。
 */
static int validate_base(const char *base, int base_len)
{
    if (base_len < 3 || base_len > 8) return 0;

    for (int prefix_len = 1; prefix_len <= 2; prefix_len++) {
        if (prefix_len >= base_len) break;

        /* prefix 部: 各文字は英数字 */
        int ok = 1;
        for (int i = 0; i < prefix_len; i++) {
            if (!isalnum((unsigned char)base[i])) { ok = 0; break; }
        }
        if (!ok) continue;

        /* area: 1桁の数字 */
        int area_pos = prefix_len;
        if (!isdigit((unsigned char)base[area_pos])) continue;

        /* suffix: 残り全て英数字, 1〜5文字（記念局は数字を含む場合がある） */
        int suf_start = area_pos + 1;
        int suf_len   = base_len - suf_start;
        if (suf_len < 1 || suf_len > 5) continue;

        ok = 1;
        for (int i = suf_start; i < base_len; i++) {
            if (!isalnum((unsigned char)base[i])) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

/*
 * コールサイン妥当性チェック
 *
 * 受け入れ文法:
 *   callsign  = BASE [ '/' secondary ] [ '-' ssid ]
 *   BASE      = PREFIX(1〜2英数字) + AREA(1桁数字) + SUFFIX(1〜5英字)  ← ITU構造
 *   secondary = 1〜3文字の英数字
 *   ssid      = 1〜2桁の数字、値 0〜15
 *   全体       = 3〜15文字
 *   先頭・末尾は英数字
 */
int callsign_validate(const char *call)
{
    if (call == NULL) return 0;

    int len = (int)strlen(call);
    if (len < 3 || len > 15) return 0;

    /* 先頭・末尾は英数字 */
    if (!isalnum((unsigned char)call[0])) return 0;
    if (!isalnum((unsigned char)call[len - 1])) return 0;

    const char *p = call;

    /* --- BASE: 英数字の連続部分を取り出しITU構造チェック --- */
    int base_len = 0;
    while (isalnum((unsigned char)*p)) {
        base_len++;
        p++;
    }
    if (!validate_base(call, base_len)) return 0;

    /* --- オプション: /secondary (英数字1〜3文字) --- */
    if (*p == '/') {
        p++;
        int suf_len = 0;
        while (isalnum((unsigned char)*p)) {
            suf_len++;
            p++;
        }
        if (suf_len < 1 || suf_len > 3) return 0;
    }

    /* --- オプション: -SSID (0〜15) --- */
    if (*p == '-') {
        p++;
        if (!isdigit((unsigned char)*p)) return 0;
        int ssid = 0, digits = 0;
        while (isdigit((unsigned char)*p)) {
            ssid = ssid * 10 + (*p - '0');
            digits++;
            p++;
        }
        if (digits > 2 || ssid > 15) return 0;
    }

    /* 末尾まで消費されていること */
    return (*p == '\0') ? 1 : 0;
}
