#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct {
    char ssid[33];
    char password[65];
} passport_wifi_t;

bool passport_wifi_valid(const passport_wifi_t *wifi);
size_t passport_utf8_prefix(const char *text, size_t limit);

esp_err_t passport_config_read(const char *key, void *data, size_t size);
esp_err_t passport_config_write(const char *key, const void *data, size_t size);
