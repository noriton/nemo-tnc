#include "tnc_settings.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "SETTINGS";

esp_err_t settings_save_mycall(const char* callsign) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(my_handle, KEY_MYCALL, callsign);
    if (err == ESP_OK) {
        err = nvs_commit(my_handle); // これを忘れると保存されない！
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t settings_load_mycall(char* buf, size_t max_len) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_get_str(my_handle, KEY_MYCALL, buf, &max_len);
    nvs_close(my_handle);
    return err;
}