#include "passport_web.h"
#include "passport_web_internal.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "passport_apps.h"
#include "passport_network.h"
#include "passport_weather.h"
extern const char passport_html_start[] asm("_binary_passport_manager_html_start");
extern const char passport_html_end[] asm("_binary_passport_manager_html_end");
extern const char passport_import_start[] asm("_binary_passport_import_mjs_start");
extern const char passport_import_end[] asm("_binary_passport_import_mjs_end");
extern const char passport_hashes_start[] asm("_binary_passport_hashes_mjs_start");
extern const char passport_hashes_end[] asm("_binary_passport_hashes_mjs_end");

static esp_err_t target_page(httpd_req_t *req)
{
    return passport_web_page(req,
                "default-src 'none'; style-src 'unsafe-inline'; "
                "connect-src 'self' https://geocoding-api.open-meteo.com; script-src 'self' 'unsafe-inline'; "
                "frame-ancestors 'none'; form-action 'none'; base-uri 'none'",
                passport_html_start, passport_html_end);
}

static esp_err_t import_script(httpd_req_t *req)
{
    bool parser = strcmp(req->uri, "/passport_import.mjs") == 0;
    const char *start = parser ? passport_import_start : passport_hashes_start;
    const char *end = parser ? passport_import_end : passport_hashes_end;
    httpd_resp_set_type(req, "text/javascript; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, start, end - start - 1);
}

static size_t requested_offset(httpd_req_t *req)
{
    char query[32], value[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "offset", value, sizeof(value)) != ESP_OK || !value[0] ||
        strspn(value, "0123456789") != strlen(value)) return 0;
    return strtoul(value, NULL, 10);
}

static bool add_app_state(cJSON *json, size_t offset)
{
    enum { PAGE_SIZE = 8 };
    passport_apps_status_t apps;
    passport_app_t page[PAGE_SIZE];
    size_t count = passport_apps_page(offset, page, PAGE_SIZE, &apps);
    if (offset && offset >= apps.count) {
        offset = apps.count ? (apps.count - 1) / PAGE_SIZE * PAGE_SIZE : 0;
        count = passport_apps_page(offset, page, PAGE_SIZE, &apps);
    }
    cJSON *list = cJSON_AddArrayToObject(json, "apps");
    if (!list || !cJSON_AddBoolToObject(json, "manager_ready", apps.ready) ||
        !cJSON_AddBoolToObject(json, "busy", apps.busy) ||
        !cJSON_AddNumberToObject(json, "total_bytes", apps.total_bytes) ||
        !cJSON_AddNumberToObject(json, "free_bytes", apps.free_bytes) ||
        !cJSON_AddNumberToObject(json, "max_app_bytes", apps.max_app_bytes) ||
        !cJSON_AddNumberToObject(json, "app_count", apps.count) ||
        !cJSON_AddNumberToObject(json, "apps_offset", offset) ||
        !cJSON_AddNumberToObject(json, "page_size", PAGE_SIZE)) return false;
    for (size_t i = 0; i < count; ++i) {
        cJSON *entry = cJSON_CreateObject();
        if (!entry || !cJSON_AddItemToArray(list, entry)) {
            cJSON_Delete(entry);
            return false;
        }
        if (!cJSON_AddStringToObject(entry, "name", page[i].name) ||
            !cJSON_AddStringToObject(entry, "version", page[i].version) ||
            !cJSON_AddNumberToObject(entry, "size", page[i].size) ||
            !cJSON_AddBoolToObject(entry, "built_in", false)) return false;
    }
    return true;
}

static bool add_weather_state(cJSON *json)
{
    passport_weather_city_t city;
    passport_weather_city(&city);
    return cJSON_AddStringToObject(json, "weather_city", city.city) != NULL;
}

static bool weather_coordinate(cJSON *json, const char *key, int limit, int32_t *coordinate)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsNumber(item) || !(item->valuedouble >= -limit && item->valuedouble <= limit)) return false;
    int32_t value = (int32_t)item->valuedouble;
    if (value != item->valuedouble) return false;
    *coordinate = value;
    return true;
}

static esp_err_t weather_city(httpd_req_t *req)
{
    if (!passport_web_authorized(req)) return ESP_OK;
    if (req->method == HTTP_DELETE) return passport_web_action_result(req, passport_weather_set_city(NULL));
    cJSON *json = passport_web_read_json(req);
    if (!json) return passport_web_invalid_body(req);
    passport_weather_city_t city = {0};
    bool valid = passport_web_copy_field(json, "city", city.city, sizeof(city.city)) &&
                 weather_coordinate(json, "latitude", 90 * PASSPORT_WEATHER_COORD_SCALE, &city.latitude) &&
                 weather_coordinate(json, "longitude", 180 * PASSPORT_WEATHER_COORD_SCALE, &city.longitude);
    cJSON_Delete(json);
    return passport_web_action_result(req, valid ? passport_weather_set_city(&city) : ESP_ERR_INVALID_ARG);
}

enum { COMMUNITY_KEY_BYTES = 80, COMMUNITY_CHUNK_BYTES = 2048 };
static const char COMMUNITY_HOST[] = "ai-passport.folotoy.cn";
static atomic_bool s_https_active;

bool passport_web_acquire_https(void)
{
    return !atomic_exchange(&s_https_active, true);
}

void passport_web_release_https(void)
{
    atomic_store(&s_https_active, false);
}

typedef struct {
    httpd_req_t *request;
    char path[sizeof("/api/download/community/") + COMMUNITY_KEY_BYTES];
    bool firmware;
} community_request_t;

static bool community_path(community_request_t *job, const char *query)
{
    job->firmware = strncmp(query, "firmware=", 9) == 0;
    if (!job->firmware && strncmp(query, "play=", 5) != 0) return false;
    const char *key = query + (job->firmware ? 9 : 5);
    if (!*key || strlen(key) > COMMUNITY_KEY_BYTES || *key == '-' || *key == '_') return false;
    bool numeric_id = *key >= '1' && *key <= '9';
    for (const char *p = key; *p; ++p) {
        bool digit = *p >= '0' && *p <= '9';
        if (!digit && !(*p >= 'a' && *p <= 'z') && !(*p >= 'A' && *p <= 'Z') &&
            *p != '-' && *p != '_') return false;
        numeric_id &= digit;
    }
    const char *prefix = job->firmware ? "/api/download/community/" :
                         numeric_id ? "/api/plays/id/" : "/api/plays/";
    snprintf(job->path, sizeof(job->path), "%s%s", prefix, key);
    return true;
}

static bool community_clock_ready(httpd_req_t *req)
{
    const time_t first_valid_time = 1704067200; // 2024-01-01 UTC
    if (time(NULL) >= first_valid_time) return true;
    // As in ESP-Claw, a paired browser can bootstrap the clock if NTP is blocked.
    char value[11];
    if (httpd_req_get_hdr_value_len(req, "X-Passport-Time") != 10 ||
        httpd_req_get_hdr_value_str(req, "X-Passport-Time", value, sizeof(value)) != ESP_OK ||
        strspn(value, "0123456789") != 10) return false;
    const long long seconds = strtoll(value, NULL, 10);
    const struct timeval clock = {.tv_sec = (time_t)seconds};
    return seconds >= first_valid_time && seconds < 4102444800LL && settimeofday(&clock, NULL) == 0;
}

static void community_worker(void *context)
{
    community_request_t *job = context;
    httpd_req_t *req = job->request;
    const esp_http_client_config_t config = {
        .host = COMMUNITY_HOST,
        .path = job->path,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
        .timeout_ms = 6000,
        .buffer_size = COMMUNITY_CHUNK_BYTES,
    };
    const int64_t deadline = esp_timer_get_time() + 90000000;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    int64_t length = -1;
    int status = -1;
    if (client && esp_http_client_set_header(client, "Accept-Encoding", "identity") == ESP_OK &&
        esp_http_client_open(client, 0) == ESP_OK) {
        length = esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
    }
    const int max_bytes = job->firmware ? 8 * 1024 * 1024 : 128 * 1024;
    const bool known_length = status == 200 && length > 0 && !esp_http_client_is_chunked_response(client);
    const bool streaming = known_length && length <= max_bytes;
    esp_err_t result = ESP_OK;
    if (streaming) {
        httpd_resp_set_type(req, job->firmware ? "application/octet-stream" : "application/json; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        char chunk[COMMUNITY_CHUNK_BYTES];
        int remaining = (int)length;
        while (remaining && esp_timer_get_time() < deadline) {
            int limit = remaining < COMMUNITY_CHUNK_BYTES ? remaining : COMMUNITY_CHUNK_BYTES;
            int count = esp_http_client_read(client, chunk, limit);
            if (count <= 0 || httpd_resp_send_chunk(req, chunk, count) != ESP_OK) {
                result = ESP_FAIL;
                break;
            }
            remaining -= count;
        }
        if (remaining || !esp_http_client_is_complete_data_received(client)) result = ESP_FAIL;
    }
    if (client) esp_http_client_cleanup(client);
    // Release TLS resources before the browser can finish and request the next file.
    passport_web_release_https();
    if (streaming) {
        // No final chunk on interruption: the browser must reject a partial file.
        if (result == ESP_OK) result = httpd_resp_send_chunk(req, NULL, 0);
    } else if (status == 404 || status == 410) {
        result = passport_web_send_json(req, "404 Not Found", "{\"error\":\"community_missing\"}");
    } else if (!known_length) {
        ESP_LOGW("passport_community", "Official request failed, status=%d", status);
        result = passport_web_send_json(req, "502 Bad Gateway", "{\"error\":\"community_fetch\"}");
    } else {
        result = passport_web_send_json(req, "413 Payload Too Large",
                           job->firmware ? "{\"error\":\"bundle_size\"}" : "{\"error\":\"community_metadata\"}");
    }
    if (result != ESP_OK) shutdown(httpd_req_to_sockfd(req), SHUT_RDWR);
    httpd_req_async_handler_complete(req);
    free(job);
    vTaskDelete(NULL);
}

static esp_err_t community_download(httpd_req_t *req)
{
    if (!passport_web_authorized(req)) return ESP_OK;
    char query[sizeof("firmware=") + COMMUNITY_KEY_BYTES];
    community_request_t request = {0};
    if (req->content_len || httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        !community_path(&request, query)) {
        return passport_web_send_json(req, "400 Bad Request", "{\"error\":\"community_link\"}");
    }
    passport_network_status_t network;
    passport_network_status(&network);
    if (network.state != PASSPORT_NET_ONLINE || network.changing) {
        return passport_web_send_json(req, "409 Conflict", "{\"error\":\"community_network\"}");
    }
    if (!community_clock_ready(req)) {
        return passport_web_send_json(req, "409 Conflict", "{\"error\":\"community_clock\"}");
    }
    if (!passport_web_acquire_https()) return passport_web_action_result(req, ESP_ERR_INVALID_STATE);
    community_request_t *job = malloc(sizeof(*job));
    if (job) {
        *job = request;
        if (httpd_req_async_handler_begin(req, &job->request) == ESP_OK) {
            if (xTaskCreate(community_worker, "passport_dl", 8192, job, 4, NULL) == pdPASS) return ESP_OK;
            passport_web_action_result(job->request, ESP_ERR_NO_MEM);
            httpd_req_async_handler_complete(job->request);
            free(job);
            passport_web_release_https();
            return ESP_OK;
        }
        free(job);
    }
    passport_web_release_https();
    return passport_web_action_result(req, ESP_ERR_NO_MEM);
}

typedef struct {
    httpd_req_t *request;
    int64_t deadline;
    size_t remaining;
} upload_t;

static int read_upload(void *context, char *buffer, size_t size)
{
    upload_t *upload = context;
    if (esp_timer_get_time() >= upload->deadline) return HTTPD_SOCK_ERR_TIMEOUT;
    int count = httpd_req_recv(upload->request, buffer, size);
    if (count > 0) upload->remaining -= (size_t)count;
    return count;
}

static esp_err_t discard_upload(upload_t *upload)
{
    passport_apps_status_t apps;
    passport_apps_status(&apps);
    if (upload->remaining > apps.total_bytes) return ESP_FAIL;
    upload->deadline = esp_timer_get_time() + 2000000;
    // Closing a socket with unread data can reset it before the browser gets the error.
    char buffer[1024];
    while (upload->remaining) {
        size_t size = upload->remaining < sizeof(buffer) ? upload->remaining : sizeof(buffer);
        if (read_upload(upload, buffer, size) <= 0) return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t install_app(httpd_req_t *req)
{
    upload_t upload = {.request = req, .deadline = esp_timer_get_time() + 120000000, .remaining = req->content_len};
    if (!passport_web_authorized(req)) return discard_upload(&upload);
    passport_network_status_t network;
    passport_network_status(&network);
    char type[32];
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (network.changing || network.state == PASSPORT_NET_CONNECTING) {
        err = ESP_ERR_INVALID_STATE;
    } else if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) == ESP_OK &&
               strcmp(type, "application/octet-stream") == 0) {
        err = passport_apps_install(req->content_len, read_upload, &upload);
    }
    esp_err_t result = passport_web_action_result(req, err);
    if (result != ESP_OK || err == ESP_ERR_TIMEOUT) return ESP_FAIL;
    return discard_upload(&upload);
}

static esp_err_t remove_app(httpd_req_t *req)
{
    if (!passport_web_authorized(req)) return ESP_OK;
    cJSON *json = passport_web_read_json(req);
    if (!json) return passport_web_invalid_body(req);
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "name"));
    esp_err_t err = passport_apps_remove(name, NULL);
    cJSON_Delete(json);
    return passport_web_action_result(req, err);
}

const passport_web_error_t *passport_web_target_error(esp_err_t err)
{
    static const passport_web_error_t errors[] = {
        {ESP_ERR_INVALID_SIZE, "413 Content Too Large", "{\"error\":\"size\"}"},
        {PASSPORT_ERR_APP_TOO_LARGE, "413 Content Too Large", "{\"error\":\"app_too_large\"}"},
        {ESP_ERR_OTA_VALIDATE_FAILED, "422 Unprocessable Content", "{\"error\":\"image\"}"},
        {ESP_ERR_TIMEOUT, "408 Request Timeout", "{\"error\":\"upload\"}"},
        {ESP_ERR_NOT_ALLOWED, "409 Conflict", "{\"error\":\"duplicate\"}"},
        {PASSPORT_ERR_APP_SPACE, "507 Insufficient Storage", "{\"error\":\"space\"}"},
        {ESP_ERR_NOT_FOUND, "404 Not Found", "{\"error\":\"missing\"}"},
    };
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        if (errors[i].code == err) return &errors[i];
    }
    return NULL;
}

bool passport_web_add_state(httpd_req_t *req, cJSON *json)
{
    return cJSON_AddStringToObject(json, "web_app", "manager") &&
           add_app_state(json, requested_offset(req)) && add_weather_state(json);
}

bool passport_web_wifi_busy(void)
{
    return false;
}

esp_err_t passport_web_start(void)
{
    static const httpd_uri_t routes[] = {
        {.uri = "/passport_import.mjs", .method = HTTP_GET, .handler = import_script},
        {.uri = "/passport_hashes.mjs", .method = HTTP_GET, .handler = import_script},
        {.uri = "/api/community", .method = HTTP_GET, .handler = community_download},
        {.uri = "/api/weather/city", .method = HTTP_POST, .handler = weather_city},
        {.uri = "/api/weather/city", .method = HTTP_DELETE, .handler = weather_city},
        {.uri = "/api/apps", .method = HTTP_POST, .handler = install_app},
        {.uri = "/api/apps", .method = HTTP_DELETE, .handler = remove_app},
    };
    return passport_web_start_server(target_page, routes, sizeof(routes) / sizeof(routes[0]));
}
