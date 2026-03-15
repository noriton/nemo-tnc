#ifndef NVS_IF_H
#define NVS_IF_H

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "tnc_pc_port.h" // tnc_mode_table 参照用
#include "nvs_if.h"
#include "nvs_flash.h"


// 数値定数と識別文字列のペアを定義する構造体
typedef struct mode_mapping {
    int mode;
    const char *str;
} mode_mapping_t;

esp_err_t nvs_load_mycall(char* buf, size_t max_len);
esp_err_t nvs_save_mycall(const char* callsign);

// モード用
esp_err_t nvs_save_port_mode(int port_id, int portmode);
esp_err_t nvs_load_port_mode(int *portmode, int port_id, int default_mode);

// MYCALLリスト用
esp_err_t nvs_save_mycall_list_item(int index, const char* callsign);
esp_err_t nvs_load_mycall_list(char list[MAX_MYCALL_LIST][16]);

// ポートごとの使用MYCALLインデックス用
esp_err_t nvs_save_port_mycall_idx(int port_id, int idx);
esp_err_t nvs_load_port_mycall_idx(int port_id, int *idx, int default_idx);

/**
 * ポートごとのデフォルトモードを保存
 * @param port_id ポート番号
 * @param mode 保存するモード (tnc_mode_t型を想定)
 */
esp_err_t nvs_save_port_mode(int port_id, int mode);

/**
 * ポートごとのデフォルトモードを読み込み
 * @param portmode 読み込んだモードを格納するポインタ
 * @param port_id ポート番号
 * @param default_mode NVSにデータがない場合のデフォルト値
 * @return ESP_OK 成功, その他はエラーコード
 */
esp_err_t nvs_load_port_mode(int *portmode, int port_id, int default_mode);
#endif