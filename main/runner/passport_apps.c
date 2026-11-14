#include "passport_apps.h"
#include "passport_app_pool.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "passport_config.h"

#define META_PATH "/apps"
#define CATALOG_PATH META_PATH "/catalog"
#define METADATA_PATH META_PATH "/metadata"
#define TEMP_PATH META_PATH "/pending"

static const char TAG[] = "passport_apps";
static const char APP_PREFIX[] = "Passport-";
static const uint64_t MAX_PLAY_ID = UINT64_C(9007199254740991);
enum { CHUNK_BYTES = 4096, MAX_APPS = 96, CATALOG_MAGIC = 0x43415431 };

// The host migration tool preserves the on-flash records byte for byte.
_Static_assert(sizeof(passport_app_t) == 176 && offsetof(passport_app_t, size) == 164,
               "Unexpected app metadata layout");

typedef struct {
    esp_image_header_t image;
    esp_image_segment_header_t segment;
    esp_app_desc_t app;
} app_header_t;

_Static_assert(offsetof(app_header_t, app) == sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
               "ESP application descriptor must immediately follow the segment header");

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint32_t moving_id;
    uint32_t destination;
    uint32_t copied;
    passport_app_extent_t entries[MAX_APPS];
} app_catalog_t;

static SemaphoreHandle_t s_lock;
static passport_apps_status_t s_status;
static const esp_partition_t *s_pool;
static const esp_partition_t *s_boot;
static app_catalog_t s_catalog = {.magic = CATALOG_MAGIC};
// Only the owner of the busy flag edits this copy.
static app_catalog_t s_update;

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

bool passport_app_title_valid(const char *title)
{
    size_t size = strnlen(title, PASSPORT_APP_TITLE_BYTES);
    if (!size || size == PASSPORT_APP_TITLE_BYTES || passport_utf8_prefix(title, size) != size) return false;
    for (size_t i = 0; i < size; ++i) {
        unsigned char byte = title[i];
        if (byte < 0x20 || byte == 0x7f ||
            (byte == 0xc2 && (unsigned char)title[i + 1] <= 0x9f)) return false;
    }
    return true;
}

uint64_t passport_app_play_id(const char *text)
{
    size_t digits = strspn(text, "0123456789");
    if (!digits || digits >= PASSPORT_PLAY_ID_BYTES || text[digits] || text[0] == '0') return 0;
    uint64_t id = strtoull(text, NULL, 10);
    return id <= MAX_PLAY_ID ? id : 0;
}

static esp_err_t file_error(void)
{
    return errno == ENOSPC ? PASSPORT_ERR_APP_SPACE : ESP_FAIL;
}

static esp_err_t replace_file(const char *path, const void *data, size_t size)
{
    int fd = open(TEMP_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return file_error();
    esp_err_t err = write(fd, data, size) == (ssize_t)size ? ESP_OK : file_error();
    if (close(fd) != 0 && err == ESP_OK) err = file_error();
    if (err == ESP_OK && rename(TEMP_PATH, path) != 0) err = file_error();
    if (err != ESP_OK) unlink(TEMP_PATH);
    return err;
}

static off_t metadata_offset(uint32_t id)
{
    return (id - 1) * sizeof(passport_app_t);
}

static bool read_app(const passport_app_extent_t *entry, passport_app_t *app)
{
    int fd = open(METADATA_PATH, O_RDONLY);
    if (fd < 0) return false;
    bool ok = pread(fd, app, sizeof(*app), metadata_offset(entry->id)) == sizeof(*app);
    close(fd);
    return ok && valid_name(app->name, strnlen(app->name, sizeof(app->name))) &&
           memchr(app->version, 0, sizeof(app->version)) &&
           (!app->title[0] || passport_app_title_valid(app->title)) && app->size == entry->size &&
           app->play_id <= MAX_PLAY_ID;
}

static esp_err_t write_app(uint32_t id, const passport_app_t *app)
{
    // LittleFS commits the record update on close. The catalog refers to it only after that succeeds.
    int fd = open(METADATA_PATH, O_WRONLY | O_CREAT, 0600);
    if (fd < 0) return file_error();
    esp_err_t err = pwrite(fd, app, sizeof(*app), metadata_offset(id)) == sizeof(*app) ? ESP_OK : file_error();
    if (close(fd) != 0 && err == ESP_OK) err = file_error();
    return err;
}

static size_t catalog_size(const app_catalog_t *catalog)
{
    return offsetof(app_catalog_t, entries) + catalog->count * sizeof(*catalog->entries);
}

static void refresh_space(void)
{
    uint32_t used = 0;
    for (size_t i = 0; i < s_catalog.count; ++i) used += passport_app_extent_bytes(s_catalog.entries[i].size);
    s_status.count = s_catalog.count;
    s_status.free_bytes = s_pool->size - used;
}

// LittleFS publishes the new catalog atomically. Readers see the old or new version under s_lock.
static esp_err_t commit_catalog(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = replace_file(CATALOG_PATH, &s_update, catalog_size(&s_update));
    if (err == ESP_OK) {
        s_catalog = s_update;
        refresh_space();
        ++s_status.generation;
    }
    xSemaphoreGive(s_lock);
    return err;
}

static bool catalog_valid(size_t bytes)
{
    if (s_catalog.magic != CATALOG_MAGIC || s_catalog.count > MAX_APPS || bytes != catalog_size(&s_catalog) ||
        !passport_app_pool_valid(s_catalog.entries, s_catalog.count, s_pool->size)) return false;
    bool ids[MAX_APPS + 1] = {0};
    bool move_valid = !s_catalog.moving_id && !s_catalog.destination && !s_catalog.copied;
    uint32_t end = 0;
    for (size_t i = 0; i < s_catalog.count; ++i) {
        const passport_app_extent_t *entry = &s_catalog.entries[i];
        if (!entry->id || entry->id > MAX_APPS || ids[entry->id]) return false;
        ids[entry->id] = true;
        passport_app_t app;
        if (!read_app(entry, &app)) return false;
        uint32_t span = passport_app_extent_bytes(entry->size);
        if (entry->id == s_catalog.moving_id) {
            move_valid = s_catalog.destination == end && end < entry->offset &&
                         s_catalog.copied <= span && s_catalog.copied % PASSPORT_APP_ALIGNMENT == 0;
        }
        end = entry->offset + span;
    }
    return move_valid;
}

static esp_err_t verify_image(const passport_app_extent_t *entry)
{
    esp_partition_pos_t bounds = {.offset = s_pool->address + entry->offset, .size = entry->size};
    esp_image_metadata_t metadata;
    if (esp_image_verify(ESP_IMAGE_VERIFY, &bounds, &metadata) != ESP_OK || metadata.image_len != entry->size) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    return ESP_OK;
}

// A downward move has at least one alignment unit between source and destination.
// Work on one unit at a time so the current source stays intact until its journal commit.
static esp_err_t finish_move(size_t index, char *buffers)
{
    passport_app_extent_t entry = s_update.entries[index];
    uint32_t span = passport_app_extent_bytes(entry.size);
    char *check = buffers + CHUNK_BYTES;
    while (s_update.copied < span) {
        uint32_t source = entry.offset + s_update.copied;
        uint32_t destination = s_update.destination + s_update.copied;
        esp_err_t err = esp_partition_erase_range(s_pool, destination, PASSPORT_APP_ALIGNMENT);
        for (size_t i = 0; err == ESP_OK && i < PASSPORT_APP_ALIGNMENT; i += CHUNK_BYTES) {
            err = esp_partition_read(s_pool, source + i, buffers, CHUNK_BYTES);
            if (err == ESP_OK) err = esp_partition_write(s_pool, destination + i, buffers, CHUNK_BYTES);
            if (err == ESP_OK) err = esp_partition_read(s_pool, destination + i, check, CHUNK_BYTES);
            if (err == ESP_OK && memcmp(buffers, check, CHUNK_BYTES)) err = ESP_FAIL;
        }
        if (err != ESP_OK) return err;
        s_update.copied += PASSPORT_APP_ALIGNMENT;
        err = commit_catalog();
        if (err != ESP_OK) return err;
        vTaskDelay(1);
    }
    entry.offset = s_update.destination;
    esp_err_t err = verify_image(&entry);
    if (err != ESP_OK) return err;
    s_update.entries[index] = entry;
    s_update.moving_id = s_update.destination = s_update.copied = 0;
    return commit_catalog();
}

static esp_err_t allocate_app(size_t size, uint32_t *offset, char *buffers)
{
    if (passport_app_extent_bytes(size) > s_status.free_bytes || s_catalog.count == MAX_APPS) {
        return PASSPORT_ERR_APP_SPACE;
    }
    if (passport_app_pool_find(s_catalog.entries, s_catalog.count, s_pool->size, size, offset)) return ESP_OK;
    uint32_t end = 0;
    for (size_t i = 0; i < s_catalog.count; ++i) {
        passport_app_extent_t entry = s_catalog.entries[i];
        if (entry.offset != end) {
            ESP_LOGI(TAG, "Moving app in pool from 0x%lx to 0x%lx", (unsigned long)entry.offset, (unsigned long)end);
            s_update = s_catalog;
            s_update.moving_id = entry.id;
            s_update.destination = end;
            esp_err_t err = commit_catalog();
            if (err == ESP_OK) err = finish_move(i, buffers);
            if (err != ESP_OK) return err;
            if (passport_app_pool_find(s_catalog.entries, s_catalog.count, s_pool->size, size, offset)) return ESP_OK;
        }
        end += passport_app_extent_bytes(entry.size);
    }
    return PASSPORT_ERR_APP_SPACE;
}

esp_err_t passport_apps_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    s_pool = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "app_pool");
    s_boot = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "app_boot");
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running || running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY || !s_pool ||
        !s_boot || s_boot->address != PASSPORT_BOOT_ADDRESS || s_boot->size != CHUNK_BYTES ||
        s_pool->address % PASSPORT_APP_ALIGNMENT || s_pool->size % PASSPORT_APP_ALIGNMENT ||
        s_pool->size / PASSPORT_APP_ALIGNMENT > MAX_APPS) return ESP_ERR_INVALID_STATE;
    const esp_vfs_littlefs_conf_t config = {.base_path = META_PATH, .partition_label = "app_meta"};
    esp_err_t err = esp_vfs_littlefs_register(&config);
    if (err != ESP_OK) return err;
    if (unlink(TEMP_PATH) != 0 && errno != ENOENT) return ESP_FAIL;
    int fd = open(CATALOG_PATH, O_RDONLY);
    if (fd >= 0) {
        ssize_t size = read(fd, &s_catalog, sizeof(s_catalog));
        char extra;
        bool exact = read(fd, &extra, 1) == 0;
        close(fd);
        if (size < 0 || !exact || !catalog_valid(size)) return ESP_ERR_INVALID_STATE;
    } else if (errno != ENOENT) return ESP_FAIL;
    if (s_catalog.moving_id) {
        char *buffers = malloc(2 * CHUNK_BYTES);
        if (!buffers) return ESP_ERR_NO_MEM;
        s_update = s_catalog;
        for (size_t i = 0; i < s_catalog.count; ++i) {
            if (s_catalog.entries[i].id == s_catalog.moving_id) {
                err = finish_move(i, buffers);
                break;
            }
        }
        free(buffers);
        if (err != ESP_OK) return err;
    }
    refresh_space();
    s_status.total_bytes = s_status.max_app_bytes = s_pool->size;
    s_status.ready = true;
    return ESP_OK;
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
    size_t count = 0;
    while (s_status.ready && offset < s_catalog.count && count < limit) {
        if (read_app(&s_catalog.entries[offset++], &apps[count])) ++count;
    }
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

static void release(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.busy = false;
    // The source of a partly moved app may have been overwritten. Resume on restart.
    if (s_catalog.moving_id) s_status.ready = false;
    xSemaphoreGive(s_lock);
}

static size_t find_app(const char *name, passport_app_t *app)
{
    for (size_t i = 0; i < s_catalog.count; ++i) {
        if (read_app(&s_catalog.entries[i], app) && !strcmp(app->name, name)) return i;
    }
    return s_catalog.count;
}

static esp_err_t read_exact(passport_app_read_t receive, void *context, void *data, size_t size)
{
    char *out = data;
    while (size) {
        int count = receive(context, out, size);
        if (count <= 0 || (size_t)count > size) return ESP_ERR_TIMEOUT;
        out += count;
        size -= count;
    }
    return ESP_OK;
}

static esp_err_t match_upload(const passport_app_extent_t *entry, const app_header_t *header,
                              passport_app_read_t receive, void *context, char *buffers)
{
    char *check = buffers + CHUNK_BYTES;
    esp_err_t err = esp_partition_read(s_pool, entry->offset, check, sizeof(*header));
    if (err != ESP_OK) return err;
    if (memcmp(check, header, sizeof(*header))) return ESP_ERR_NOT_ALLOWED;
    for (size_t i = sizeof(*header); i < entry->size;) {
        size_t chunk = entry->size - i;
        if (chunk > CHUNK_BYTES) chunk = CHUNK_BYTES;
        err = read_exact(receive, context, buffers, chunk);
        if (err == ESP_OK) err = esp_partition_read(s_pool, entry->offset + i, check, chunk);
        if (err != ESP_OK) return err;
        if (memcmp(buffers, check, chunk)) return ESP_ERR_NOT_ALLOWED;
        i += chunk;
    }
    return ESP_OK;
}

static esp_err_t write_image(const passport_app_extent_t *entry, const app_header_t *header,
                             passport_app_read_t receive, void *context, char *buffer)
{
    esp_err_t err = esp_partition_erase_range(s_pool, entry->offset, passport_app_extent_bytes(entry->size));
    memcpy(buffer, header, sizeof(*header));
    size_t prefix = sizeof(*header);
    for (size_t i = 0; err == ESP_OK && i < entry->size;) {
        size_t chunk = entry->size - i;
        if (chunk > CHUNK_BYTES) chunk = CHUNK_BYTES;
        err = read_exact(receive, context, buffer + prefix, chunk - prefix);
        if (err == ESP_OK) err = esp_partition_write(s_pool, entry->offset + i, buffer, chunk);
        prefix = 0;
        i += chunk;
    }
    return err == ESP_OK ? verify_image(entry) : err;
}

static esp_err_t install_image(size_t size, passport_app_read_t receive, void *context,
                               const char *title, uint64_t play_id, char *buffers)
{
    app_header_t header;
    esp_err_t err = read_exact(receive, context, &header, sizeof(header));
    if (err != ESP_OK) return err;
    passport_app_t app = {.size = size};
    if (header.image.magic != ESP_IMAGE_HEADER_MAGIC || header.image.chip_id != ESP_CHIP_ID_ESP32C3 ||
        header.image.hash_appended != 1 || !header.image.segment_count ||
        header.image.segment_count > ESP_IMAGE_MAX_SEGMENTS || header.segment.data_len < sizeof(header.app) ||
        header.segment.data_len > size - offsetof(app_header_t, app) || !describe_app(&header.app, &app)) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    passport_app_t installed;
    size_t index = find_app(app.name, &installed);
    if (title) strlcpy(app.title, title, sizeof(app.title));
    app.play_id = play_id;
    if (index < s_catalog.count) {
        if (!title || installed.size != size) return ESP_ERR_NOT_ALLOWED;
        if (!play_id) app.play_id = installed.play_id;
        const passport_app_extent_t *extent = &s_catalog.entries[index];
        err = match_upload(extent, &header, receive, context, buffers);
        if (err == ESP_OK) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            err = write_app(extent->id, &app);
            if (err == ESP_OK) ++s_status.generation;
            xSemaphoreGive(s_lock);
        }
        return err;
    }

    passport_app_extent_t entry = {.size = size, .id = 1};
    err = allocate_app(size, &entry.offset, buffers);
    if (err != ESP_OK) return err;
    bool used[MAX_APPS + 1] = {0};
    for (size_t i = 0; i < s_catalog.count; ++i) used[s_catalog.entries[i].id] = true;
    while (used[entry.id]) ++entry.id;
    err = write_image(&entry, &header, receive, context, buffers);
    if (err == ESP_OK) err = write_app(entry.id, &app);
    if (err != ESP_OK) return err;
    s_update = s_catalog;
    index = s_update.count;
    while (index && s_update.entries[index - 1].offset > entry.offset) {
        s_update.entries[index] = s_update.entries[index - 1];
        --index;
    }
    s_update.entries[index] = entry;
    ++s_update.count;
    err = commit_catalog();
    if (err == ESP_OK) ESP_LOGI(TAG, "Installed %s at 0x%lx (%zu bytes)", app.name,
                                (unsigned long)(s_pool->address + entry.offset), size);
    return err;
}

esp_err_t passport_apps_install(size_t size, passport_app_read_t receive, void *context,
                               const char *title, uint64_t play_id)
{
    if (size < sizeof(app_header_t) + 32) return ESP_ERR_INVALID_SIZE;
    if ((title && !passport_app_title_valid(title)) || (play_id && (!title || play_id > MAX_PLAY_ID))) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = reserve();
    if (err != ESP_OK) return err;
    if (size > s_status.max_app_bytes) err = PASSPORT_ERR_APP_TOO_LARGE;
    else {
        char *buffers = malloc(2 * CHUNK_BYTES);
        err = buffers ? install_image(size, receive, context, title, play_id, buffers) : ESP_ERR_NO_MEM;
        free(buffers);
    }
    release();
    return err;
}

esp_err_t passport_apps_remove(const char *name, const size_t *generation)
{
    if (!name || !valid_name(name, strlen(name))) return ESP_ERR_INVALID_ARG;
    esp_err_t err = reserve();
    if (err != ESP_OK) return err;
    passport_app_t app;
    size_t index = find_app(name, &app);
    if (index == s_catalog.count || (generation && *generation != s_status.generation)) err = ESP_ERR_NOT_FOUND;
    else {
        s_update = s_catalog;
        --s_update.count;
        memmove(&s_update.entries[index], &s_update.entries[index + 1],
                (s_update.count - index) * sizeof(*s_update.entries));
        err = commit_catalog();
    }
    release();
    return err;
}

esp_err_t passport_apps_launch(const char *name)
{
    if (!name || !valid_name(name, strlen(name))) return ESP_ERR_INVALID_ARG;
    esp_err_t err = reserve();
    if (err != ESP_OK) return err;
    passport_app_t app;
    size_t index = find_app(name, &app);
    if (index == s_catalog.count) err = ESP_ERR_NOT_FOUND;
    else {
        passport_app_extent_t entry = s_catalog.entries[index];
        err = verify_image(&entry);
        if (err == ESP_OK) {
            passport_boot_request_t request = {.magic = PASSPORT_BOOT_MAGIC, .offset = entry.offset,
                .size = entry.size, .check = passport_boot_check(entry.offset, entry.size)};
            err = esp_partition_erase_range(s_boot, 0, s_boot->size);
            if (err == ESP_OK) err = esp_partition_write(s_boot, 0, &request, sizeof(request));
        }
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Launching %s at 0x%lx", name, (unsigned long)(s_pool->address + entry.offset));
            esp_restart();
        }
    }
    release();
    return err;
}
