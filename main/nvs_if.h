#ifndef NVS_IF_H
#define NVS_IF_H

#include "esp_err.h"

#define SETTINGS_NAMESPACE "tnc_config"
#define KEY_MYCALL "mycall"

esp_err_t nvs_save_mycall(const char* callsign);
esp_err_t nvs_load_mycall(char* buf, size_t max_len);

#endif