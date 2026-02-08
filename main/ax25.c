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

int ax25_decode_ui_info(const uint8_t *frame, size_t frame_len, char *out_info, size_t max_info_len) {
    // 最小サイズチェック (宛先7 + 送信元7 + Control1 + PID1 = 16バイト)
    if (frame_len < 16) return -1;

    size_t pos = 0;
    
    // 1. 宛先アドレススキップ (7バイト)
    pos += 7;

    // 2. 送信元アドレススキップ (7バイト)
    // 拡張ビット(LSB)を見て、リピータがある場合はスキップする処理が必要だが
    // 今回は簡易的に、SSIDのLSBが0 (拡張あり＝リピータあり) の場合はループで飛ばす
    while ((frame[pos - 1] & 0x01) == 0) { // 直前のアドレスのLSBが0なら、次のアドレスがある
        if (pos + 7 > frame_len) return -2; // 長さ不足
        pos += 7;
    }

    // 3. Controlフィールド (UIフレームなら 0x03)
    if (frame[pos] != 0x03) return -3; // Not UI frame
    pos++;

    // 4. PID (0xF0: No Layer 3)
    if (frame[pos] != 0xF0) return -4; // Unexpected PID
    pos++;

    // 5. 情報フィールドの抽出
    size_t info_len = frame_len - pos; // 残り全てを情報とする（FCSは外側で処理済みと仮定、あるいは含む場合は調整必要）
    
    // 注: ここでの frame_len は通常 FCS(2byte) を含むことが多いが、
    // 送信直後のバッファにはFCSが含まれていない場合や、受信側で除去済みの場合がある。
    // 今回の TESTTX では FCS 込みで渡すが、build 関数は FCS を付与している。
    // build 関数の戻り値は FCS 込みのサイズ。
    // したがって、最後の2バイトは FCS なので除外する。
    if (info_len < 2) return -5; // 情報なし、またはFCS分しかない
    info_len -= 2; // FCS除外

    if (info_len > max_info_len - 1) {
        info_len = max_info_len - 1;
    }

    memcpy(out_info, &frame[pos], info_len);
    out_info[info_len] = '\0'; // Null端子

    return (int)info_len;
}
