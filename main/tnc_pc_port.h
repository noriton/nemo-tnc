#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"

typedef enum {
    PORT_MODE_COMMAND, // コマンドモード
    PORT_TRANCEPORT,   // トランスペアレントモード  
    PORT_MODE_KISS,    // KISSバイナリモード
    PORT_MODE_BRIDGE   // テスト用：反対側にスルー
} tnc_port_mode_t;

typedef struct {
    int id;                    // ポート番号 (0 or 1)
    tnc_port_mode_t mode;      // 現在のモード
    RingbufHandle_t rx_rb;     // PC -> TNC (受信)
    RingbufHandle_t tx_rb;     // TNC -> PC (送信)
    char line_buf[128];        // コマンド用の一行バッファ
    int line_pos;
} tnc_pc_port_t;

// 共通のMutex（command_parser.c などで定義）
extern SemaphoreHandle_t console_mutex;