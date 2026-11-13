#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct {
    int (*battery)(void);
    esp_err_t (*backlight)(int percent);
} passport_lua_device_t;

esp_err_t passport_lua_run(const char *script, bool compile_only, const passport_lua_device_t *device,
                          char *error, size_t error_size);
