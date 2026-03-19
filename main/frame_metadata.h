#ifndef FRAME_METADATA_H
#define FRAME_METADATA_H

#include <stdint.h>
#include <stddef.h>

// --- Metadata System ---
#define TNC_META_VERSION_1  0x01
#define TNC_META_MAX_DIGI   8    // デジピータ最大数（AX.25仕様上限）

// パケットの種別
typedef enum tnc_meta_type {
    META_TYPE_DATA_KISS,     // KISS由来のデータ（AX.25フレーム）
    META_TYPE_DATA_UI,       // トランスペアレント由来のデータ（ペイロードのみ）
    META_TYPE_CTRL_CMD       // 制御コマンド（設定変更など）
} tnc_meta_type_t;

// デジピータ1局分の情報
typedef struct tnc_meta_digi {
    char    call[12];   // デジピータコールサイン（NULL終端、最大11文字）
    uint8_t has_been;   // H bit: 1=通過済み（中継済み）, 0=未通過
    uint8_t pad[3];     // アライメント予約
} tnc_meta_digi_t;      // 16 bytes

// システム内部で持ち回るメタデータヘッダ
// __attribute__((aligned(4))) を付与してアラインメント強要
// リングバッファに詰めても大丈夫なように、常に4バイト境界を保証する

typedef struct tnc_meta_header {
    uint8_t  version;         // ヘッダのバージョン (将来の互換性用)
    uint8_t  type;            // tnc_meta_type_t (データか制御か)
    uint16_t header_len;      // このヘッダ自身のサイズ (ペイロード開始位置の算出用)

    uint16_t payload_len;     // ペイロード（データ本体）のサイズ
    uint8_t  port_id;         // 入力元 / 送信先のポート番号 (0 or 1)
    uint8_t  reserved;        // アライメント調整＆将来用予約領域

    char     src_call[12];    // 送信元コールサイン (NULL終端、最大11文字 + 終端)
    char     dest_call[12];   // 宛先コールサイン (NULL終端、最大11文字 + 終端)

    uint8_t          digi_count;              // デジピータ数 (0〜TNC_META_MAX_DIGI)
    uint8_t          digi_pad[3];             // アライメント予約
    tnc_meta_digi_t  digi[TNC_META_MAX_DIGI]; // デジピータアドレス (0〜digi_count-1 が有効)

    // --- 以下、将来拡張していくパラメータ領域 ---
    uint16_t fcs;         // AX.25 FCS (CRC-16-CCITT) 計算値（rawpacket_build_ax25_ui が格納）
    uint8_t  fcs_pad[2];  // アライメント予約
    // uint8_t fx25_rs_mode; // FX.25のReed-Solomonモード指定など
    // uint8_t tx_delay;     // このパケット特有のTXDELAY指定など

} __attribute__((aligned(4))) tnc_meta_header_t;

#endif /* FRAME_METADATA_H */