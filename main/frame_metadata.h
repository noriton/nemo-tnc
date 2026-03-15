#ifndef FRAME_METADATA_H
#define FRAME_METADATA_H

#include <stdint.h>
#include <stddef.h>

// --- Metadata System ---
#define TNC_META_VERSION_1 0x01

// パケットの種別
typedef enum tnc_meta_type {
    META_TYPE_DATA_KISS,     // KISS由来のデータ（AX.25フレーム）
    META_TYPE_DATA_UI,       // トランスペアレント由来のデータ（ペイロードのみ）
    META_TYPE_CTRL_CMD       // 制御コマンド（設定変更など）
} tnc_meta_type_t;

// システム内部で持ち回るメタデータヘッダ
// __attribute__((aligned(4))) を付与してアラインメント強要
// リングバッファに詰めても大丈夫なように、常に4バイト境界を保証する

typedef struct tnc_meta_header {
    uint8_t version;         // ヘッダのバージョン (将来の互換性用)
    uint8_t type;            // tnc_meta_type_t (データか制御か)
    uint16_t header_len;     // このヘッダ自身のサイズ (ペイロード開始位置の算出用)
    
    uint16_t payload_len;    // ペイロード（データ本体）のサイズ
    uint8_t port_id;         // 入力元 / 送信先のポート番号 (0 or 1)
    uint8_t reserved;        // アライメント調整＆将来用予約領域

    char src_call[12];       // 送信元コールサイン (NULL終端、最大11文字 + 終端)
    char dest_call[12];      // 宛先コールサイン (NULL終端、最大11文字 + 終端)

    // --- 以下、将来拡張していくパラメータ領域 ---
    // uint8_t fx25_rs_mode; // FX.25のReed-Solomonモード指定など
    // uint8_t tx_delay;     // このパケット特有のTXDELAY指定など
    
} __attribute__((aligned(4))) tnc_meta_header_t;

#endif /* FRAME_METADATA_H */