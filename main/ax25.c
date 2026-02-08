#include <string.h>
#include "ax25.h"

size_t ax25_build_ui_frame(const ax25_address_t *addr, const uint8_t *info, size_t info_len, uint8_t *out_buf) {
    size_t pos = 0;

    // 1. 開始フラグ (無線機に送る直前に付与する場合もあるが、ここでは含める)
    out_buf[pos++] = 0x7E;

    // --- ここからFCS計算対象 ---
    size_t fcs_start = pos;

    // 2. 宛先アドレス (is_last = false)
    encode_callsign(&out_buf[pos], addr->dest_call, addr->dest_ssid, false);
    pos += 7;

    // 3. 送信元アドレス (is_last = true、中継局がない場合)
    encode_callsign(&out_buf[pos], addr->src_call, addr->src_ssid, true);
    pos += 7;

    // 4. コントロールフィールド (UIフレーム = 0x03)
    out_buf[pos++] = 0x03;

    // 5. PID (Protocol Identifier: レイヤー3なし = 0xF0)
    out_buf[pos++] = 0xF0;

    // 6. 情報フィールド (Info)
    if (info != NULL && info_len > 0) {
        memcpy(&out_buf[pos], info, info_len);
        pos += info_len;
    }

    // 7. FCSの計算 (Flagを除く、AddressからInfoまで)
    uint16_t fcs = ax25_fcs_calculate(&out_buf[fcs_start], pos - fcs_start);
    
    // FCSをリトルエンディアンで格納
    out_buf[pos++] = fcs & 0xFF;        // 下位
    out_buf[pos++] = (fcs >> 8) & 0xFF; // 上位
    // --- ここまでFCS計算対象 ---

    // 8. 終了フラグ
    out_buf[pos++] = 0x7E;

    return pos; // 総パケットサイズ
}
