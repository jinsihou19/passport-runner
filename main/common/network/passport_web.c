#include "passport_web_internal.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "passport_network.h"

esp_err_t passport_web_send_json(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_sendstr(req, body);
}

#define BROWSER_COOKIE "passport_browser"
#define BROWSER_COOKIE_ATTRS "; Path=/api/; HttpOnly; SameSite=Strict"
// Remembered browsers are shared between the manager and ESP-Claw via NVS.
enum { BROWSER_SLOTS = 8, TOKEN_BYTES = 24, TOKEN_CHARS = TOKEN_BYTES * 2, DIGEST_BYTES = 32 };
typedef struct {
    uint8_t digests[BROWSER_SLOTS][DIGEST_BYTES];
} trusted_browsers_t;
static const char BROWSER_KEY[] = "web_clients_v1";
static trusted_browsers_t s_browsers;
static bool s_browser_store_ready;

static bool browser_request(httpd_req_t *req)
{
    // Cross-origin forms cannot supply this header, and the API does not enable CORS.
    char flag[2];
    return httpd_req_get_hdr_value_str(req, "X-Passport-Request", flag, sizeof(flag)) == ESP_OK &&
           strcmp(flag, "1") == 0;
}

static int browser_slot(httpd_req_t *req)
{
    if (!browser_request(req)) return -1;
    char token[TOKEN_CHARS + 1];
    size_t length = sizeof(token);
    uint8_t digest[DIGEST_BYTES];
    if (httpd_req_get_cookie_val(req, BROWSER_COOKIE, token, &length) != ESP_OK || length != sizeof(token) ||
        mbedtls_sha256((const uint8_t *)token, TOKEN_CHARS, digest, 0) != 0) return -1;
    for (int i = 0; i < BROWSER_SLOTS; ++i) {
        if (mbedtls_ct_memcmp(digest, s_browsers.digests[i], sizeof(digest)) == 0) return i;
    }
    return -1;
}

static bool code_authorized(httpd_req_t *req)
{
    static unsigned failures;
    static int64_t retry_at;
    int64_t now = esp_timer_get_time();
    if (now < retry_at) {
        httpd_resp_set_hdr(req, "Retry-After", "5");
        passport_web_send_json(req, "429 Too Many Requests", "{\"error\":\"rate\"}");
        return false;
    }
    char code[PASSPORT_CODE_CHARS + 1];
    const char *expected = passport_network_code();
    if (expected[0] && httpd_req_get_hdr_value_len(req, "X-Passport-Code") == PASSPORT_CODE_CHARS &&
        httpd_req_get_hdr_value_str(req, "X-Passport-Code", code, sizeof(code)) == ESP_OK) {
        unsigned difference = 0;
        for (size_t i = 0; i < PASSPORT_CODE_CHARS; ++i) difference |= (unsigned)(code[i] ^ expected[i]);
        if (!difference) {
            failures = 0;
            return true;
        }
        if (++failures == 5) {
            retry_at = now + 5000000;
            failures = 0;
        }
    }
    passport_web_send_json(req, "401 Unauthorized", "{\"error\":\"code\"}");
    return false;
}

bool passport_web_authorized(httpd_req_t *req)
{
    if (httpd_req_get_hdr_value_len(req, "X-Passport-Code")) return code_authorized(req);
    if (browser_slot(req) >= 0) return true;
    passport_web_send_json(req, "401 Unauthorized", "{\"error\":\"code\"}");
    return false;
}

esp_err_t passport_web_page(httpd_req_t *req, const char *policy, const char *start, const char *end)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Content-Security-Policy", policy);
    return httpd_resp_send(req, start, end - start - 1);
}

cJSON *passport_web_read_json(httpd_req_t *req)
{
    enum { MAX_BODY = 4096 };
    if (!req->content_len || req->content_len > MAX_BODY) return NULL;
    char *body = malloc(req->content_len + 1);
    if (!body) return NULL;
    size_t offset = 0;
    while (offset < req->content_len) {
        int count = httpd_req_recv(req, body + offset, req->content_len - offset);
        if (count <= 0) break;
        offset += count;
    }
    body[offset] = '\0';
    cJSON *json = NULL;
    if (offset == req->content_len && !memchr(body, '\0', offset) && !strstr(body, "\\u0000")) {
        json = cJSON_ParseWithOpts(body, NULL, true);
    }
    free(body);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

esp_err_t passport_web_invalid_body(httpd_req_t *req)
{
    passport_web_send_json(req, "400 Bad Request", "{\"error\":\"invalid\"}");
    return ESP_FAIL;
}

bool passport_web_copy_field(cJSON *json, const char *name, char *buffer, size_t size)
{
    const char *text = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, name));
    return text && strlcpy(buffer, text, size) < size;
}

esp_err_t passport_web_action_result(httpd_req_t *req, esp_err_t err)
{
    const passport_web_error_t *specific = passport_web_target_error(err);
    if (specific) return passport_web_send_json(req, specific->status, specific->body);
    switch (err) {
    case ESP_OK:
        return passport_web_send_json(req, "200 OK", "{\"ok\":true}");
    case ESP_ERR_INVALID_ARG:
        return passport_web_send_json(req, "400 Bad Request", "{\"error\":\"invalid\"}");
    case ESP_ERR_INVALID_STATE:
        return passport_web_send_json(req, "409 Conflict", "{\"error\":\"busy\"}");
    default:
        ESP_LOGW("passport_web", "Action failed: %s", esp_err_to_name(err));
        return passport_web_send_json(req, "503 Service Unavailable", "{\"error\":\"unavailable\"}");
    }
}

static esp_err_t save_browsers(const trusted_browsers_t *next)
{
    if (!s_browser_store_ready) return ESP_FAIL;
    esp_err_t err = passport_config_write(BROWSER_KEY, next, sizeof(*next));
    if (err == ESP_OK) s_browsers = *next;
    return err;
}

static void forget_slot(trusted_browsers_t *browsers, size_t slot)
{
    for (size_t i = slot; i + 1 < BROWSER_SLOTS; ++i) {
        memcpy(browsers->digests[i], browsers->digests[i + 1], DIGEST_BYTES);
    }
    memset(browsers->digests[BROWSER_SLOTS - 1], 0, DIGEST_BYTES);
}

static esp_err_t forget_browser(httpd_req_t *req)
{
    int slot = browser_slot(req);
    if (slot >= 0) {
        trusted_browsers_t next = s_browsers;
        forget_slot(&next, (size_t)slot);
        esp_err_t err = save_browsers(&next);
        if (err != ESP_OK) return passport_web_action_result(req, err);
    }
    httpd_resp_set_hdr(req, "Set-Cookie", BROWSER_COOKIE "=; Max-Age=0" BROWSER_COOKIE_ATTRS);
    return passport_web_action_result(req, ESP_OK);
}

static esp_err_t browser_session(httpd_req_t *req)
{
    if (req->content_len) return passport_web_invalid_body(req);
    if (!browser_request(req)) return passport_web_action_result(req, ESP_ERR_INVALID_ARG);
    if (req->method == HTTP_DELETE) return forget_browser(req);
    if (!code_authorized(req)) return ESP_OK;
    // Wi-Fi supplies hardware entropy once the station or the setup hotspot is up.
    passport_network_status_t network;
    passport_network_status(&network);
    if (network.changing || (network.state != PASSPORT_NET_ONLINE && network.state != PASSPORT_NET_SETUP)) {
        return passport_web_action_result(req, ESP_ERR_INVALID_STATE);
    }
    uint8_t random[TOKEN_BYTES];
    char token[TOKEN_CHARS + 1];
    static const char hex[] = "0123456789abcdef";
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) {
        token[2 * i] = hex[random[i] >> 4];
        token[2 * i + 1] = hex[random[i] & 15];
    }
    token[TOKEN_CHARS] = '\0';
    trusted_browsers_t next = s_browsers;
    int slot = browser_slot(req);
    if (slot >= 0) forget_slot(&next, (size_t)slot);
    // A new pairing keeps the eight most recently remembered browser entries.
    memmove(next.digests + 1, next.digests, sizeof(next.digests) - DIGEST_BYTES);
    if (mbedtls_sha256((const uint8_t *)token, TOKEN_CHARS, next.digests[0], 0) != 0) {
        return passport_web_action_result(req, ESP_FAIL);
    }
    esp_err_t err = save_browsers(&next);
    if (err != ESP_OK) return passport_web_action_result(req, err);
    char cookie[sizeof(BROWSER_COOKIE "=" "; Max-Age=7776000" BROWSER_COOKIE_ATTRS) + TOKEN_CHARS];
    snprintf(cookie, sizeof(cookie), BROWSER_COOKIE "=%s; Max-Age=7776000" BROWSER_COOKIE_ATTRS, token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    return passport_web_action_result(req, ESP_OK);
}

static esp_err_t state(httpd_req_t *req)
{
    if (!passport_web_authorized(req)) return ESP_OK;
    static const char *const states[] = {"starting", "setup", "connecting", "online", "failed"};
    passport_network_status_t network;
    passport_network_status(&network);
    cJSON *json = cJSON_CreateObject();
    bool ok = json && cJSON_AddStringToObject(json, "network", states[network.state]) &&
        cJSON_AddStringToObject(json, "ip", network.ip) &&
        cJSON_AddStringToObject(json, "wifi_error", network.error) &&
        cJSON_AddBoolToObject(json, "wifi_saved", network.saved_count > 0) &&
        cJSON_AddNumberToObject(json, "wifi_saved_count", network.saved_count) &&
        cJSON_AddBoolToObject(json, "changing", network.changing) &&
        passport_web_add_state(req, json);
    char *body = ok ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    if (!body) return passport_web_action_result(req, ESP_ERR_NO_MEM);
    esp_err_t err = passport_web_send_json(req, "200 OK", body);
    free(body);
    return err;
}

static esp_err_t wifi(httpd_req_t *req)
{
    if (!passport_web_authorized(req)) return ESP_OK;
    if (passport_web_wifi_busy()) return passport_web_action_result(req, ESP_ERR_INVALID_STATE);
    if (req->method == HTTP_DELETE) return passport_web_action_result(req, passport_network_setup(true));

    cJSON *json = passport_web_read_json(req);
    if (!json) return passport_web_invalid_body(req);
    passport_wifi_t next = {0};
    bool valid = passport_web_copy_field(json, "ssid", next.ssid, sizeof(next.ssid)) &&
                 passport_web_copy_field(json, "password", next.password, sizeof(next.password));
    cJSON_Delete(json);
    return passport_web_action_result(req, valid ? passport_network_try(&next) : ESP_ERR_INVALID_ARG);
}

static esp_err_t wifi_scan(httpd_req_t *req)
{
    if (!passport_web_authorized(req)) return ESP_OK;
    if (req->method == HTTP_POST) {
        if (passport_web_wifi_busy()) return passport_web_action_result(req, ESP_ERR_INVALID_STATE);
        return passport_web_action_result(req, passport_network_scan());
    }

    static const char *const states[] = {"idle", "scanning", "done", "failed"};
    passport_wifi_scan_t scan;
    passport_network_scan_status(&scan);
    cJSON *json = cJSON_CreateObject();
    cJSON *networks = json && cJSON_AddStringToObject(json, "status", states[scan.state])
        ? cJSON_AddArrayToObject(json, "networks") : NULL;
    bool ok = networks != NULL;
    for (size_t i = 0; ok && i < scan.count; ++i) {
        const passport_wifi_access_point_t *point = &scan.points[i];
        cJSON *item = cJSON_CreateObject();
        if (!item || !cJSON_AddItemToArray(networks, item)) {
            cJSON_Delete(item);
            ok = false;
            break;
        }
        ok = cJSON_AddStringToObject(item, "ssid", point->ssid) &&
             cJSON_AddNumberToObject(item, "rssi", point->rssi) &&
             cJSON_AddBoolToObject(item, "open", point->open);
    }
    char *body = ok ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    if (!body) return passport_web_action_result(req, ESP_ERR_NO_MEM);
    esp_err_t err = passport_web_send_json(req, "200 OK", body);
    free(body);
    return err;
}

esp_err_t passport_web_start_server(esp_err_t (*page_handler)(httpd_req_t *), const httpd_uri_t *extra_routes, size_t extra_count)
{
    esp_err_t stored = passport_config_read(BROWSER_KEY, &s_browsers, sizeof(s_browsers));
    s_browser_store_ready = stored == ESP_OK || stored == ESP_ERR_NVS_NOT_FOUND;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page_handler},
        {.uri = "/api/state", .method = HTTP_GET, .handler = state},
        {.uri = "/api/session", .method = HTTP_POST, .handler = browser_session},
        {.uri = "/api/session", .method = HTTP_DELETE, .handler = browser_session},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi},
        {.uri = "/api/wifi", .method = HTTP_DELETE, .handler = wifi},
        {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = wifi_scan},
        {.uri = "/api/wifi/scan", .method = HTTP_POST, .handler = wifi_scan},
    };
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    size_t common_count = sizeof(routes) / sizeof(routes[0]);
    config.max_uri_handlers = common_count + extra_count;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.stack_size = 6144;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    httpd_handle_t server;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;
    for (size_t i = 0; i < common_count + extra_count; ++i) {
        const httpd_uri_t *route = i < common_count ? &routes[i] : &extra_routes[i - common_count];
        err = httpd_register_uri_handler(server, route);
        if (err != ESP_OK) {
            httpd_stop(server);
            return err;
        }
    }
    return ESP_OK;
}
