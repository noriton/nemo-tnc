#include "rawpacket.h"
#include "ax25.h"
#include "callsign.h"
#include "tnc_buffer.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

static const char *TAG = "RAWPKT";

// 生パケット出力用リングバッファ（NOSPLIT: 1アイテム = 1パケット）
RingbufHandle_t raw_tx_buf = NULL;


// ---------------------------------------------------------------------------
// AX.25アドレスフィールド解析（KISS由来パケット用）
// ---------------------------------------------------------------------------

/**
 * AX.25フレーム先頭のアドレスフィールドを解析し、meta の各フィールドに格納する。
 *
 * @param frame     AX.25フレーム先頭ポインタ（KISSペイロード）
 * @param frame_len フレーム長
 * @param meta      結果を格納するメタデータヘッダ（dest_call/src_call/digi_* を上書き）
 * @return アドレスフィールド終端位置（Control byte の直前）、解析失敗時は 0
 */
static size_t parse_ax25_addr_to_meta(const uint8_t *frame, size_t frame_len,
                                       tnc_meta_header_t *meta)
{
    // 最低: Dest(7) + Src(7) + Control(1) + PID(1) = 16 bytes
    if (frame == NULL || meta == NULL || frame_len < 16) {
        return 0;
    }

    size_t pos = 0;

    // 1. Destination Address
    {
        char base[7] = {0};
        uint8_t ssid = 0;
        decode_callsign(&frame[pos], base, &ssid);
        if (ssid > 0) {
            snprintf(meta->dest_call, sizeof(meta->dest_call), "%s-%d", base, ssid);
        } else {
            snprintf(meta->dest_call, sizeof(meta->dest_call), "%s", base);
        }
    }
    pos += AX25_ADDR_LEN;

    // 2. Source Address
    //    SSID バイトの bit0 = 0 のとき後続アドレスあり（デジピータ）
    bool src_is_last = (frame[pos + 6] & 0x01) != 0;
    {
        char base[7] = {0};
        uint8_t ssid = 0;
        decode_callsign(&frame[pos], base, &ssid);
        if (ssid > 0) {
            snprintf(meta->src_call, sizeof(meta->src_call), "%s-%d", base, ssid);
        } else {
            snprintf(meta->src_call, sizeof(meta->src_call), "%s", base);
        }
    }
    pos += AX25_ADDR_LEN;

    // 3. Digipeater Addresses（is_last bit が立つまで繰り返し）
    meta->digi_count = 0;
    memset(meta->digi, 0, sizeof(meta->digi));
    while (!src_is_last && (pos + AX25_ADDR_LEN) <= frame_len) {
        bool is_last  = (frame[pos + 6] & 0x01) != 0;
        bool has_been = (frame[pos + 6] & 0x80) != 0; // H-bit

        if (meta->digi_count < TNC_META_MAX_DIGI) {
            char base[7] = {0};
            uint8_t ssid = 0;
            decode_callsign(&frame[pos], base, &ssid);
            tnc_meta_digi_t *d = &meta->digi[meta->digi_count];
            if (ssid > 0) {
                snprintf(d->call, sizeof(d->call), "%s-%d", base, ssid);
            } else {
                snprintf(d->call, sizeof(d->call), "%s", base);
            }
            d->has_been = has_been ? 1 : 0;
            meta->digi_count++;
        }
        pos += AX25_ADDR_LEN;
        if (is_last) break;
    }

    return pos; // Control byte の直前位置
}


// ---------------------------------------------------------------------------
// パケット組み立て
// ---------------------------------------------------------------------------

size_t rawpacket_build_ax25_ui(const tnc_meta_header_t *meta,
                                const uint8_t *payload,
                                uint8_t *out_buf,
                                size_t out_buf_size)
{
    if (meta == NULL || payload == NULL || out_buf == NULL) {
        return 0;
    }

    // 必要最小サイズチェック（宛先 + 送信元 + Digi×N + Control + PID + Info）
    uint8_t digi_count = (meta->digi_count <= TNC_META_MAX_DIGI)
                         ? meta->digi_count : TNC_META_MAX_DIGI;
    size_t required = AX25_ADDR_LEN * (2 + digi_count) + 2 + meta->payload_len;
    if (required > out_buf_size) {
        ESP_LOGW(TAG, "Output buffer too small: need %d, have %d", required, out_buf_size);
        return 0;
    }

    size_t pos = 0;

    // 1. 宛先アドレス（Destination Address, is_last=false）
    char dest_base[7] = {0};
    int  dest_ssid    = 0;
    callsign_to_ax25(meta->dest_call, dest_base, &dest_ssid);
    encode_callsign(&out_buf[pos], dest_base, (uint8_t)dest_ssid, false);
    pos += AX25_ADDR_LEN;

    // 2. 送信元アドレス（Source Address）
    //    デジピータがある場合は is_last=false、なければ is_last=true
    char src_base[7] = {0};
    int  src_ssid    = 0;
    callsign_to_ax25(meta->src_call, src_base, &src_ssid);
    encode_callsign(&out_buf[pos], src_base, (uint8_t)src_ssid, (digi_count == 0));
    pos += AX25_ADDR_LEN;

    // 3. デジピータアドレス（Digipeater Addresses）
    for (uint8_t i = 0; i < digi_count; i++) {
        bool is_last_addr = (i == digi_count - 1);
        char digi_base[7] = {0};
        int  digi_ssid    = 0;
        callsign_to_ax25(meta->digi[i].call, digi_base, &digi_ssid);
        encode_callsign(&out_buf[pos], digi_base, (uint8_t)digi_ssid, is_last_addr);
        // H bit（通過済みフラグ）: AX.25アドレスSSIDバイト(pos+6)のbit7
        if (meta->digi[i].has_been) {
            out_buf[pos + 6] |= 0x80;
        }
        pos += AX25_ADDR_LEN;
    }

    // 4. Control Field（UIフレーム = 0x03）
    out_buf[pos++] = AX25_CTRL_UI;

    // 5. PID（No Layer 3 = 0xF0）
    out_buf[pos++] = AX25_PID_NO_L3;

    // 6. 情報フィールド（Info）
    if (meta->payload_len > 0) {
        memcpy(&out_buf[pos], payload, meta->payload_len);
        pos += meta->payload_len;
    }

    return pos;
}


// ---------------------------------------------------------------------------
// 受信タスク（ポートごとに1つ起動）
// ---------------------------------------------------------------------------

static void rawpacket_task(void *pvParameters)
{
    int port_id = (int)(intptr_t)pvParameters;

    for (;;) {
        size_t size = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceive(
            tx_ringbuf[port_id], &size, pdMS_TO_TICKS(100));

        if (item == NULL) {
            continue;
        }

        // ヘッダサイズの最低限チェック
        if (size < sizeof(tnc_meta_header_t)) {
            ESP_LOGW(TAG, "Port%d: short read (%d bytes, need %d)",
                     port_id, size, sizeof(tnc_meta_header_t));
            vRingbufferReturnItem(tx_ringbuf[port_id], item);
            continue;
        }

        tnc_meta_header_t *meta = (tnc_meta_header_t *)item;

        // バージョンチェック
        if (meta->version != TNC_META_VERSION_1) {
            ESP_LOGW(TAG, "Port%d: unknown meta version 0x%02X", port_id, meta->version);
            vRingbufferReturnItem(tx_ringbuf[port_id], item);
            continue;
        }

        // 完全パケットサイズチェック
        size_t expected = (size_t)meta->header_len + meta->payload_len;
        if (size < expected) {
            ESP_LOGW(TAG, "Port%d: incomplete packet (got %d, expected %d)",
                     port_id, size, expected);
            vRingbufferReturnItem(tx_ringbuf[port_id], item);
            continue;
        }

        uint8_t *payload = item + meta->header_len;

        switch (meta->type) {

        case META_TYPE_DATA_UI: {
            // AX.25 UIフレームとして生パケットを組み立て
            uint8_t raw_buf[RAW_PACKET_MAX_LEN];
            size_t raw_len = rawpacket_build_ax25_ui(
                meta, payload, raw_buf, sizeof(raw_buf));
            if (raw_len == 0) {
                ESP_LOGW(TAG, "Port%d: build_ax25_ui failed", port_id);
                break;
            }

            // raw_tx_buf へ出力（メタデータヘッダ先頭 + L3生パケット）
            if (raw_tx_buf != NULL) {
                raw_tx_item_t out;
                // メタデータをそのまま引き継ぎ、payload_len だけ生パケット長に更新
                out.meta              = *meta;
                out.meta.payload_len  = (uint16_t)raw_len;
                memcpy(out.data, raw_buf, raw_len);
                BaseType_t res = xRingbufferSend(
                    raw_tx_buf, &out, RAW_TX_ITEM_SIZE(raw_len), pdMS_TO_TICKS(10));
                if (res != pdTRUE) {
                    ESP_LOGW(TAG, "Port%d: raw_tx_buf full, packet dropped", port_id);
                }
            }
            break;
        }

        case META_TYPE_DATA_KISS: {
            // KISSペイロード = PC から受け取った AX.25 フレーム（FCSなし）
            // アドレスフィールドを解析してメタに展開し、raw_tx_buf へ転送する
            size_t ax25_len = (size_t)meta->payload_len;
            if (ax25_len == 0 || ax25_len > RAW_PACKET_MAX_LEN) {
                ESP_LOGW(TAG, "Port%d: KISS invalid payload len %u",
                         port_id, (unsigned)ax25_len);
                break;
            }

            tnc_meta_header_t updated_meta = *meta;
            size_t ctrl_pos = parse_ax25_addr_to_meta(payload, ax25_len, &updated_meta);
            if (ctrl_pos == 0) {
                ESP_LOGW(TAG, "Port%d: KISS AX.25 addr parse failed", port_id);
                break;
            }

            ESP_LOGD(TAG, "Port%d: KISS src=%s dst=%s digi=%d",
                     port_id, updated_meta.src_call, updated_meta.dest_call,
                     updated_meta.digi_count);

            if (raw_tx_buf != NULL) {
                raw_tx_item_t out;
                out.meta             = updated_meta;
                out.meta.payload_len = (uint16_t)ax25_len;
                memcpy(out.data, payload, ax25_len);
                BaseType_t res = xRingbufferSend(
                    raw_tx_buf, &out, RAW_TX_ITEM_SIZE(ax25_len), pdMS_TO_TICKS(10));
                if (res != pdTRUE) {
                    ESP_LOGW(TAG, "Port%d: raw_tx_buf full, KISS packet dropped", port_id);
                }
            }
            break;
        }

        default:
            ESP_LOGW(TAG, "Port%d: unknown meta type %d", port_id, meta->type);
            break;
        }

        vRingbufferReturnItem(tx_ringbuf[port_id], item);
    }
}


// ---------------------------------------------------------------------------
// 初期化
// ---------------------------------------------------------------------------

void rawpacket_init(void)
{
    // 生パケット出力バッファ（NOSPLIT: 1アイテム = 1完全パケット）
    raw_tx_buf = xRingbufferCreate(4096, RINGBUF_TYPE_NOSPLIT);
    if (raw_tx_buf == NULL) {
        ESP_LOGE(TAG, "Failed to create raw_tx_buf");
        return;
    }

    // ポートごとにタスクを起動
    for (int i = 0; i < 2; i++) {
        char task_name[16];
        snprintf(task_name, sizeof(task_name), "rawpkt_%d", i);
        xTaskCreate(rawpacket_task, task_name, 2048, (void *)(intptr_t)i, 5, NULL);
    }

    ESP_LOGI(TAG, "rawpacket_init done");
}
