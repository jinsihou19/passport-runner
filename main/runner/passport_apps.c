#include "passport_apps.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define STORE_LABEL "app_store"
#define STORE_PATH "/apps"
#define UPLOAD_PATH STORE_PATH "/.upload"

static const char TAG[] = "passport_apps";
static const char APP_PREFIX[] = "Passport-";
enum { UPLOAD_CHUNK = 4096, APP_PATH_BYTES = sizeof(STORE_PATH "/.bin") + PASSPORT_APP_NAME_BYTES };

typedef struct {
    esp_image_header_t image;
    esp_image_segment_header_t segment;
    esp_app_desc_t app;
} app_header_t;

_Static_assert(offsetof(app_header_t, app) == sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
               "ESP application descriptor must immediately follow the segment header");

static SemaphoreHandle_t s_lock;
static passport_apps_status_t s_status;
static const esp_partition_t *s_run;
static char s_staged[PASSPORT_APP_NAME_BYTES];

static bool valid_name(const char *name, size_t size)
{
    const size_t prefix = sizeof(APP_PREFIX) - 1;
    if (size <= prefix || size >= PASSPORT_APP_NAME_BYTES || memcmp(name, APP_PREFIX, prefix)) return false;
    for (size_t i = prefix; i < size; ++i) {
        unsigned char ch = name[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) return false;
    }
    return true;
}

static bool describe_app(const esp_app_desc_t *desc, passport_app_t *app)
{
    size_t name_size = strnlen(desc->project_name, sizeof(desc->project_name));
    size_t version_size = strnlen(desc->version, sizeof(desc->version));
    if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD || !valid_name(desc->project_name, name_size) ||
        !version_size || version_size >= sizeof(app->version)) {
        return false;
    }
    for (size_t i = 0; i < version_size; ++i) {
        unsigned char ch = desc->version[i];
        if (ch < 0x20 || ch > 0x7e) return false;
    }
    strlcpy(app->name, desc->project_name, sizeof(app->name));
    strlcpy(app->version, desc->version, sizeof(app->version));
    return true;
}

static void app_path(char *path, const char *name)
{
    snprintf(path, APP_PATH_BYTES, STORE_PATH "/%s.bin", name);
}

static bool file_name(const char *file, char *name)
{
    const size_t suffix = sizeof(".bin") - 1;
    size_t length = strlen(file);
    if (length <= suffix || strcmp(file + length - suffix, ".bin") || !valid_name(file, length - suffix)) {
        return false;
    }
    memcpy(name, file, length - suffix);
    name[length - suffix] = '\0';
    return true;
}

static void describe_file(passport_app_t *app)
{
    char path[APP_PATH_BYTES];
    app_path(path, app->name);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    struct stat info;
    app_header_t header;
    passport_app_t image = {0};
    if (fstat(fd, &info) == 0) app->size = info.st_size;
    if (read(fd, &header, sizeof(header)) == sizeof(header) && describe_app(&header.app, &image) &&
        strcmp(app->name, image.name) == 0) {
        strlcpy(app->version, image.version, sizeof(app->version));
    }
    close(fd);
}

static size_t scan_store(size_t offset, passport_app_t *apps, size_t limit)
{
    DIR *dir = opendir(STORE_PATH);
    if (!dir) return 0;
    size_t count = 0;
    struct dirent *entry;
    while ((!apps || count < limit) && (entry = readdir(dir))) {
        passport_app_t app = {0};
        if (entry->d_type != DT_REG || !file_name(entry->d_name, app.name)) continue;
        if (offset) {
            --offset;
            continue;
        }
        if (apps) {
            describe_file(&app);
            apps[count] = app;
        }
        ++count;
    }
    closedir(dir);
    return count;
}

static esp_err_t refresh_space(void)
{
    size_t total, used;
    esp_err_t err = esp_littlefs_info(STORE_LABEL, &total, &used);
    s_status.total_bytes = err == ESP_OK ? total : 0;
    s_status.free_bytes = err == ESP_OK && used < total ? total - used : 0;
    return err;
}

esp_err_t passport_apps_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running || running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY ||
        !esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_run = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "app_run");
    if (!s_run || !esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS,
                                            STORE_LABEL)) {
        return ESP_ERR_NOT_FOUND;
    }
    s_status.max_app_bytes = s_run->size;
    const esp_vfs_littlefs_conf_t config = {.base_path = STORE_PATH, .partition_label = STORE_LABEL};
    esp_err_t err = esp_vfs_littlefs_register(&config);
    if (err != ESP_OK) return err;
    if (unlink(UPLOAD_PATH) != 0 && errno != ENOENT) return ESP_FAIL;
    err = refresh_space();
    s_status.ready = err == ESP_OK;
    if (s_status.ready) s_status.count = scan_store(0, NULL, 0);
    return err;
}

void passport_apps_status(passport_apps_status_t *status)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    if (s_lock) xSemaphoreGive(s_lock);
}

size_t passport_apps_page(size_t offset, passport_app_t *apps, size_t limit, passport_apps_status_t *status)
{
    if (!s_lock) {
        *status = s_status;
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    size_t count = s_status.ready && limit ? scan_store(offset, apps, limit) : 0;
    xSemaphoreGive(s_lock);
    return count;
}

static esp_err_t reserve(void)
{
    if (!s_lock) return ESP_FAIL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = s_status.busy ? ESP_ERR_INVALID_STATE : !s_status.ready ? ESP_FAIL : ESP_OK;
    if (err == ESP_OK) s_status.busy = true;
    xSemaphoreGive(s_lock);
    return err;
}

static esp_err_t read_exact(passport_app_read_t read, void *context, void *data, size_t size)
{
    char *out = data;
    while (size) {
        int count = read(context, out, size);
        if (count <= 0 || (size_t)count > size) return ESP_ERR_TIMEOUT;
        out += count;
        size -= (size_t)count;
    }
    return ESP_OK;
}

static esp_err_t file_error(void)
{
    return errno == ENOSPC ? PASSPORT_ERR_APP_SPACE : ESP_FAIL;
}

static esp_err_t write_file(int fd, const void *data, size_t size)
{
    ssize_t written = write(fd, data, size);
    if (written < 0) return file_error();
    return (size_t)written == size ? ESP_OK : PASSPORT_ERR_APP_SPACE;
}

static esp_err_t save_upload(size_t size, const app_header_t *header, passport_app_read_t receive, void *context)
{
    char *buffer = malloc(UPLOAD_CHUNK);
    if (!buffer) return ESP_ERR_NO_MEM;
    int fd = open(UPLOAD_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    esp_err_t err = fd < 0 ? file_error() : write_file(fd, header, sizeof(*header));
    for (size_t remaining = size - sizeof(*header); err == ESP_OK && remaining;) {
        size_t chunk = remaining < UPLOAD_CHUNK ? remaining : UPLOAD_CHUNK;
        err = read_exact(receive, context, buffer, chunk);
        if (err == ESP_OK) err = write_file(fd, buffer, chunk);
        remaining -= chunk;
    }
    if (fd >= 0 && close(fd) != 0 && err == ESP_OK) err = file_error();
    free(buffer);
    return err;
}

static esp_err_t stage_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    struct stat info;
    if (fstat(fd, &info) != 0 || info.st_size < (off_t)(sizeof(app_header_t) + 32) ||
        (size_t)info.st_size > s_run->size) {
        close(fd);
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    size_t size = info.st_size;
    char *buffer = malloc(UPLOAD_CHUNK);
    if (!buffer) {
        close(fd);
        return ESP_ERR_NO_MEM;
    }
    s_staged[0] = '\0';
    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(s_run, size, &handle);
    if (err == ESP_OK) {
        for (size_t remaining = size; err == ESP_OK && remaining;) {
            size_t chunk = remaining < UPLOAD_CHUNK ? remaining : UPLOAD_CHUNK;
            ssize_t count = read(fd, buffer, chunk);
            if (count <= 0) {
                err = ESP_FAIL;
                break;
            }
            err = esp_ota_write(handle, buffer, count);
            remaining -= (size_t)count;
        }
        if (err == ESP_OK) err = esp_ota_end(handle);
        else esp_ota_abort(handle);
    }
    free(buffer);
    close(fd);
    if (err != ESP_OK) return err;

    // A shorter upload may leave an earlier image's tail in Flash. Require the exact verified length.
    esp_image_metadata_t metadata;
    const esp_partition_pos_t bounds = {.offset = s_run->address, .size = s_run->size};
    if (esp_image_get_metadata(&bounds, &metadata) != ESP_OK || metadata.image_len != size) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    return ESP_OK;
}

static esp_err_t receive_app(size_t size, passport_app_read_t receive, void *context, char *name)
{
    app_header_t header;
    if (size < sizeof(header) + 32) return ESP_ERR_INVALID_SIZE;
    if (size > s_status.max_app_bytes) return PASSPORT_ERR_APP_TOO_LARGE;
    if (size > s_status.free_bytes) return PASSPORT_ERR_APP_SPACE;
    esp_err_t err = read_exact(receive, context, &header, sizeof(header));
    if (err != ESP_OK) return err;
    passport_app_t app = {0};
    if (header.image.magic != ESP_IMAGE_HEADER_MAGIC || header.image.chip_id != ESP_CHIP_ID_ESP32C3 ||
        header.image.hash_appended != 1 || !header.image.segment_count ||
        header.image.segment_count > ESP_IMAGE_MAX_SEGMENTS || header.segment.data_len < sizeof(header.app) ||
        header.segment.data_len > size - offsetof(app_header_t, app) || !describe_app(&header.app, &app)) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    char path[APP_PATH_BYTES];
    app_path(path, app.name);
    struct stat existing;
    if (stat(path, &existing) == 0) return ESP_ERR_NOT_ALLOWED;
    if (errno != ENOENT) return ESP_FAIL;
    strlcpy(name, app.name, PASSPORT_APP_NAME_BYTES);
    err = save_upload(size, &header, receive, context);
    return err == ESP_OK ? stage_file(UPLOAD_PATH) : err;
}

esp_err_t passport_apps_install(size_t size, passport_app_read_t receive, void *context)
{
    esp_err_t err = reserve();
    if (err != ESP_OK) return err;
    char name[PASSPORT_APP_NAME_BYTES] = {0};
    err = receive_app(size, receive, context, name);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (err == ESP_OK) {
        // Publish the verified app with a single filesystem commit.
        char path[APP_PATH_BYTES];
        app_path(path, name);
        if (rename(UPLOAD_PATH, path) != 0) err = file_error();
        else {
            strlcpy(s_staged, name, sizeof(s_staged));
            ++s_status.count;
            ++s_status.generation;
            ESP_LOGI(TAG, "Installed %s (%zu bytes)", name, size);
        }
    }
    if (name[0]) {
        if (err != ESP_OK && unlink(UPLOAD_PATH) != 0 && errno != ENOENT) {
            ESP_LOGW(TAG, "Could not discard interrupted upload: %d", errno);
        }
        refresh_space();
    }
    s_status.busy = false;
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t passport_apps_remove(const char *name, const size_t *generation)
{
    if (!name || !valid_name(name, strlen(name))) return ESP_ERR_INVALID_ARG;
    esp_err_t err = reserve();
    if (err != ESP_OK) return err;
    char path[APP_PATH_BYTES];
    app_path(path, name);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (generation && *generation != s_status.generation) err = ESP_ERR_NOT_FOUND;
    else if (unlink(path) != 0) err = errno == ENOENT ? ESP_ERR_NOT_FOUND : file_error();
    else {
        if (strcmp(s_staged, name) == 0) s_staged[0] = '\0';
        --s_status.count;
        ++s_status.generation;
        refresh_space();
        ESP_LOGI(TAG, "Removed %s", name);
    }
    s_status.busy = false;
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t passport_apps_launch(const char *name)
{
    if (!name || !valid_name(name, strlen(name))) return ESP_ERR_INVALID_ARG;
    esp_err_t err = reserve();
    if (err != ESP_OK) return err;
    if (strcmp(s_staged, name) != 0) {
        char path[APP_PATH_BYTES];
        app_path(path, name);
        ESP_LOGI(TAG, "Preparing %s", name);
        err = stage_file(path);
        if (err == ESP_OK) strlcpy(s_staged, name, sizeof(s_staged));
    }
    if (err == ESP_OK) err = esp_ota_set_boot_partition(s_run);
    if (err == ESP_OK) {
        // Apps leave the one-boot selection pending, so a reset returns to the factory manager.
        ESP_LOGI(TAG, "Launching %s from %s", name, s_run->label);
        esp_restart();
    }
    s_staged[0] = '\0';
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.busy = false;
    xSemaphoreGive(s_lock);
    return err;
}
