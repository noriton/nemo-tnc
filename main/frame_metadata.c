#include <string.h>
#include "frame_metadata.h"
#include "tnc_pc_port.h"

size_t enqueue_packet_to_tx(uint8_t *buffer, uint8_t *payload, size_t payload_size) {
    // 1. ヘッダとペイロードを合わせた全体のサイズを計算
    size_t total_len = sizeof(tnc_meta_header_t) + payload_size;
    
    // 2. 送信用の一時バッファを用意 (スタックまたはmalloc)
    // uint8_t buffer[1024]; 
    
    if (total_len <= sizeof(buffer)) {
        tnc_meta_header_t *meta = (tnc_meta_header_t *)buffer;
        
        // ヘッダ情報の書き込み
        meta->version = TNC_META_VERSION_1;
        meta->type = META_TYPE_DATA_KISS;
        meta->header_len = sizeof(tnc_meta_header_t);
        meta->payload_len = payload_size;
        meta->port_id = 0;
        meta->reserved = 0;
        
        // ヘッダの後ろにペイロード本体をコピー
        // (buffer + meta->header_len) がペイロードの開始位置
        memcpy(buffer + meta->header_len, payload, payload_size);
        
        // リターンを元にリングバッファへ一括送信
        return total_len;
//        xRingbufferSend(port->send_tx, buffer, total_len, pdMS_TO_TICKS(10));
    }
}