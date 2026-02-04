#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"

#define SETTINGS_NAMESPACE "tnc_config"
#define KEY_MYCALL "mycall"

esp_err_t settings_save_mycall(const char* callsign);
esp_err_t settings_load_mycall(char* buf, size_t max_len);

#endif