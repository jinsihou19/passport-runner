#include "passport_config.h"

#include <string.h>

#include "nvs.h"

esp_err_t passport_config_read(const char *key, void *data, size_t size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("passport_claw", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t actual = size;
        err = nvs_get_blob(handle, key, data, &actual);
        nvs_close(handle);
        if (err == ESP_OK && actual != size) err = ESP_ERR_INVALID_SIZE;
    }
    if (err != ESP_OK) memset(data, 0, size);
    return err;
}

esp_err_t passport_config_write(const char *key, const void *data, size_t size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("passport_claw", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = data ? nvs_set_blob(handle, key, data, size) : nvs_erase_key(handle, key);
    if (!data && err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
