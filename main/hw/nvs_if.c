#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "tnc_pc_port.h" // tnc_mode_table 参照用
#include "nvs_if.h"
#include "nvs_flash.h"


static const char *KEY_MYCALL = "mycall";   // コールサインの未定義値

static const char *TAG = "NVS_IF";
static const char *NVS_NAMESPACE = "tnc_settings";


// TODO(未使用): tnc_init.c の mycall グローバル変数廃止に伴い呼び出し元なし。
//   mycall_list / mycall_idx の per-port 管理に統合済み。
//   将来的に単一コールサイン設定 API として再利用する場合に備え残存。
esp_err_t nvs_save_mycall(const char* callsign)
{
    // port0 リストの先頭（インデックス0）に保存する
    return nvs_save_mycall_list_item(0, 0, callsign);
}

#define UNUSED
#ifndef UNUSED
esp_err_t nvs_save_mycall(const char* callsign)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(my_handle, KEY_MYCALL, callsign);
    if (err == ESP_OK) {
        err = nvs_commit(my_handle); // これを忘れると保存されない！
    }
    nvs_close(my_handle);
    return err;
}
#endif

// TODO(未使用): tnc_init.c の mycall グローバル変数廃止に伴い呼び出し元なし。
//   起動ログ用途は mycall_list[0] (tnc_pc_ports_init 後) で代替済み。
//   将来的な利用に備え残存。
esp_err_t nvs_load_mycall(char* buf, size_t max_len)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("tnc_settings", NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;

    // インデックス0のキーを直接読み出す
    err = nvs_get_str(my_handle, "mycall_0", buf, &max_len);
    nvs_close(my_handle);

    return err;
}

#define UNUSED
#ifndef UNUSED
esp_err_t nvs_load_mycall(char* buf, size_t max_len)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_get_str(my_handle, KEY_MYCALL, buf, &max_len);
    nvs_close(my_handle);
    return err;
}
#endif

// NVSキー形式: "p{port_id}_mc_{index}"  例: "p0_mc_0", "p1_mc_7" (最大9文字)
esp_err_t nvs_save_mycall_list_item(int port_id, int index, const char* callsign)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("tnc_settings", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    snprintf(key, sizeof(key), "p%d_mc_%d", port_id, index);
    err = nvs_set_str(my_handle, key, callsign);

    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t nvs_load_mycall_list(int port_id, char list[MAX_MYCALL_LIST][16])
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("tnc_settings", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < MAX_MYCALL_LIST; i++) {
        char key[16];
        snprintf(key, sizeof(key), "p%d_mc_%d", port_id, i);
        size_t len = 16;
        // 取得できなくてもエラーにせず、初期値を維持する
        nvs_get_str(my_handle, key, list[i], &len);
    }
    nvs_close(my_handle);
    return ESP_OK;
}

esp_err_t nvs_save_port_mycall_idx(int port_id, int idx)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("tnc_settings", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    snprintf(key, sizeof(key), "p%d_myidx", port_id);
    err = nvs_set_i32(my_handle, key, (int32_t)idx);

    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t nvs_load_port_mycall_idx(int port_id, int *idx, int default_idx)
{
    *idx = default_idx;
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("tnc_settings", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    snprintf(key, sizeof(key), "p%d_myidx", port_id);
    int32_t val = default_idx;
    err = nvs_get_i32(my_handle, key, &val);

    if (err == ESP_OK) {
        *idx = (int)val;
    }
    nvs_close(my_handle);
    return ESP_OK;
}

esp_err_t nvs_save_port_mode(int port_id, int portmode)
{
    const char *s = NULL;

    // 数値定数に対応する識別文字列をテーブルから探す
    for (int i = 0; i < PORT_MODE_MAX; i++) {
        if (tnc_mode_table[i].mode == portmode) {
            s = tnc_mode_table[i].nvs_str;
            break;
        }
    }

    if (s == NULL) {
        ESP_LOGE(TAG, "Port %d: Attempted to save unknown mode %d", port_id, portmode);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    snprintf(key, sizeof(key), "port%d_mode", port_id);

    err = nvs_set_str(my_handle, key, s);
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }

    nvs_close(my_handle);
    return err;
}

esp_err_t nvs_load_port_mode(int *portmode, int port_id, int default_mode)
{
    *portmode = default_mode;
    nvs_handle_t my_handle;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    snprintf(key, sizeof(key), "port%d_mode", port_id);

    char mode_str[8];
    size_t str_len = sizeof(mode_str);
    err = nvs_get_str(my_handle, key, mode_str, &str_len);
    nvs_close(my_handle);

    if (err == ESP_OK) {
        // tnc_pc_port.c で定義されているテーブルを走査
        for (int i = 0; i < PORT_MODE_MAX; i++) {
            if (strcmp(mode_str, tnc_mode_table[i].nvs_str) == 0) {
                *portmode = (int)tnc_mode_table[i].mode;
                break;
            }
        }
    }

    return ESP_OK;
}

esp_err_t nvs_save_led_forced_off(int off)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i8(h, "led_off", (int8_t)off);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t nvs_load_led_forced_off(int *off)
{
    *off = 0; // デフォルト: 通常点灯
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    int8_t val = 0;
    err = nvs_get_i8(h, "led_off", &val);
    if (err == ESP_OK) *off = (int)val;
    nvs_close(h);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// コマンドヒストリ
// NVSキー: "p{port}_hcnt"（件数）, "p{port}_h{i}"（エントリ、i=0が最古）
// ---------------------------------------------------------------------------

esp_err_t nvs_save_history(int port_id, uint8_t *pool,
                            uint8_t hist_head, int hist_count)
{
    if (hist_count == 0) return ESP_OK;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    // 件数を保存
    char key[16];
    snprintf(key, sizeof(key), "p%d_hcnt", port_id);
    nvs_set_i8(h, key, (int8_t)hist_count);

    // oldest(= pool[hist_head+1]) から newer 方向に順番に保存
    uint8_t p = pool[hist_head + 1]; // H_NEXT = oldest
    for (int i = 0; i < hist_count; i++) {
        snprintf(key, sizeof(key), "p%d_h%d", port_id, i);
        nvs_set_str(h, key, (const char *)&pool[p + 2]);
        p = pool[p + 1]; // H_NEXT = go newer
    }

    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_load_history(int port_id, char out_cmds[][256], int *out_count)
{
    *out_count = 0;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    // 件数を取得
    char key[16];
    snprintf(key, sizeof(key), "p%d_hcnt", port_id);
    int8_t cnt = 0;
    err = nvs_get_i8(h, key, &cnt);
    if (err != ESP_OK || cnt <= 0) { nvs_close(h); return ESP_OK; }
    if (cnt > 32) cnt = 32;

    // エントリを古い順（i=0: 最古）に文字列配列へ復元
    int loaded = 0;
    for (int i = 0; i < cnt; i++) {
        snprintf(key, sizeof(key), "p%d_h%d", port_id, i);
        size_t len = 256;
        if (nvs_get_str(h, key, out_cmds[loaded], &len) == ESP_OK)
            loaded++;
    }

    *out_count = loaded;
    nvs_close(h);
    return ESP_OK;
}
