#ifndef PACKET_MONITOR_H
#define PACKET_MONITOR_H

/**
 * @brief パケットモニタモジュールを初期化し、監視タスクを起動する
 *
 * raw_tx_buf を監視し、送信済み AX.25 フレームを
 * META_TYPE_TX_MON として rx_ringbuf へ折り返すタスクを生成する。
 */
void packet_monitor_init(void);

#endif // PACKET_MONITOR_H
