#include "nvs_if.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>
#include "tnc_pc_port.h" // tnc_mode_table 参照用

static const char *KEY_MYCALL = "mycall";   // コールサインの未定義値

static const char *TAG = "NVS_IF";
static const char *NVS_NAMESPACE = "tnc_settings";

esp_err_t nvs_save_mycall(const char* callsign) {
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

esp_err_t nvs_load_mycall(char* buf, size_t max_len) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_get_str(my_handle, KEY_MYCALL, buf, &max_len);
    nvs_close(my_handle);
    return err;
}


esp_err_t nvs_save_port_mode(int port_id, int portmode) {
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

esp_err_t nvs_load_port_mode(int *portmode, int port_id, int default_mode) {
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