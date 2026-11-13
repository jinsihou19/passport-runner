#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

enum {
    PASSPORT_APP_NAME_BYTES = 32,
    PASSPORT_ERR_APP_SPACE = 0x7601,
    PASSPORT_ERR_APP_TOO_LARGE = 0x7602,
};

typedef struct {
    char name[PASSPORT_APP_NAME_BYTES];
    char version[32];
    size_t size;
} passport_app_t;

typedef struct {
    size_t count;
    size_t generation;
    size_t total_bytes;
    size_t free_bytes;
    size_t max_app_bytes;
    bool ready;
    bool busy;
} passport_apps_status_t;

typedef int (*passport_app_read_t)(void *context, char *buffer, size_t size);

esp_err_t passport_apps_init(void);
void passport_apps_status(passport_apps_status_t *status);
size_t passport_apps_page(size_t offset, passport_app_t *apps, size_t limit, passport_apps_status_t *status);
esp_err_t passport_apps_install(size_t size, passport_app_read_t read, void *context);
// Pass the generation the user confirmed, or NULL to remove by name regardless of generation.
esp_err_t passport_apps_remove(const char *name, const size_t *generation);
esp_err_t passport_apps_launch(const char *name);
