#ifndef RAWPACKET_H
#define RAWPACKET_H

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "frame_metadata.h"

// AX.25 L3生パケットの最大サイズ（Flags/FCS/ビットスタッフィングなし）
// AX.25標準: 宛先7 + 送信元7 + Digi×8×7=56 + Ctrl1 + PID1 + Info256 = 328
// FX.25 RS パリティ等を含む将来拡張を見越して 512 bytes に設定
#define RAW_PACKET_MAX_LEN          512
// FCS 2バイトを末尾に付加した後の最大サイズ
#define RAW_PACKET_MAX_LEN_WITH_FCS (RAW_PACKET_MAX_LEN + 2)
#define AX25_ADDR_LEN       7      // AX.25アドレスフィールド長（コールサイン6 + SSIDバイト1）
#define AX25_MAX_DIGI       8      // 中継局最大数
#define AX25_CTRL_UI        0x03   // UIフレーム制御フィールド
#define AX25_PID_NO_L3      0xF0   // PID: No Layer 3

// raw_tx_buf 出力アイテム構造体（NOSPLIT: 1アイテム = 1パケット）
// 先頭にメタデータヘッダを維持し、次段がポート番号・コールサイン等を参照できる
// meta.payload_len には生パケット（L3）のバイト数が格納される
typedef struct {
    tnc_meta_header_t meta;                // メタデータヘッダ（payload_len は生パケット長に更新）
    uint8_t           data[RAW_PACKET_MAX_LEN_WITH_FCS]; // L3生パケット本体（FCS 2バイト含む）
} raw_tx_item_t;

// 実際に送信するサイズ（有効データ分のみ、FCS込みの長さを渡すこと）
#define RAW_TX_ITEM_SIZE(raw_len) \
    (sizeof(tnc_meta_header_t) + (raw_len))

// 生パケット出力用リングバッファ（NOSPLIT: 1アイテム = 1パケット）
// 次段（FCS計算 / ビットスタッフィング）がここから読み出す
extern RingbufHandle_t raw_tx_buf;

/**
 * rawpacketモジュールを初期化し、ポートごとの処理タスクを起動する
 */
void rawpacket_init(void);

/**
 * メタデータヘッダとペイロードからAX.25 UIフレームL3生パケットを組み立てる
 *
 * 生成するフィールド（順序）:
 *   Destination Address  (7 bytes, 各文字を1-bit左シフト)
 *   Source Address       (7 bytes, 各文字を1-bit左シフト, is_last=true)
 *   Control Field        (1 byte, 0x03)
 *   PID                  (1 byte, 0xF0)
 *   Info Field           (payload_len bytes)
 *
 * FCS (CRC-16-CCITT) も末尾 2バイトに付加し、meta->fcs に格納する。
 * Flags・ビットスタッフィングはこの時点では付与しない。
 *
 * @param meta         メタデータヘッダ（src_call, dest_call, payload_len を参照、fcs を更新）
 * @param payload      情報フィールドのデータ
 * @param out_buf      出力バッファ（RAW_PACKET_MAX_LEN 以上を推奨）
 * @param out_buf_size 出力バッファサイズ
 * @return 生成したバイト数（0はエラー）
 */
size_t rawpacket_build_ax25_ui(tnc_meta_header_t *meta,
                                const uint8_t *payload,
                                uint8_t *out_buf,
                                size_t out_buf_size);

/**
 * AX.25 FCS (CRC-16-CCITT) を計算し、メタデータに格納するとともにデータ末尾に付加する
 *
 * FCS は LSB ファースト（2バイト）でデータ末尾に書き込む。
 * meta->fcs にも同じ値を格納する。
 * DATA_UI は rawpacket_build_ax25_ui() 内から呼ばれる。
 * DATA_KISS など他の経路からも直接呼び出し可能。
 *
 * @param meta      メタデータヘッダ（fcs フィールドを更新する）
 * @param data      生パケットバッファ（末尾 2バイトに FCS を書き込む）
 * @param data_len  FCS 付加前のデータ長
 * @param buf_size  data バッファの総容量（data_len + 2 以上必要）
 * @return FCS 付加後のデータ長（data_len + 2）、バッファ不足時は 0
 */
size_t rawpacket_append_fcs(tnc_meta_header_t *meta,
                             uint8_t *data, size_t data_len,
                             size_t buf_size);

#endif /* RAWPACKET_H */
