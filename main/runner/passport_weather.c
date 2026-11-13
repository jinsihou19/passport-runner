#include "passport_weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "miniz.h"
#include "passport_network.h"
#include "passport_web.h"

enum { FORECAST_BYTES = 4096, WEATHER_STACK_BYTES = 6144 };
static const char WEATHER_KEY[] = "weather_city_v1";
static const char *TAG = "passport_weather";
static const char WEATHER_HOST[] = "api.met.no";
static const int64_t SECOND_US = INT64_C(1000000);
static const int64_t RETRY_US = INT64_C(15) * 60 * 1000000;
static const int64_t STALE_US = INT64_C(6) * 60 * 60 * 1000000;

typedef struct {
    int temperature;
    passport_weather_condition_t condition;
    char modified[40];
} forecast_t;

typedef struct {
    time_t expires;
    char modified[40];
    char redirect[256];
    char encoding[16];
} forecast_headers_t;

static SemaphoreHandle_t s_lock;
static passport_weather_city_t s_city;
static forecast_t s_forecast;
static unsigned s_city_revision;
static int64_t s_next;
static int64_t s_fetched_at;
static bool s_fetching;

static bool valid_city(const passport_weather_city_t *city)
{
    return city->city[0] && memchr(city->city, '\0', sizeof(city->city)) &&
           city->latitude >= -90 * PASSPORT_WEATHER_COORD_SCALE &&
           city->latitude <= 90 * PASSPORT_WEATHER_COORD_SCALE &&
           city->longitude >= -180 * PASSPORT_WEATHER_COORD_SCALE &&
           city->longitude <= 180 * PASSPORT_WEATHER_COORD_SCALE;
}

void passport_weather_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return;
    if (passport_config_read(WEATHER_KEY, &s_city, sizeof(s_city)) != ESP_OK || !valid_city(&s_city)) {
        memset(&s_city, 0, sizeof(s_city));
    }
}

void passport_weather_city(passport_weather_city_t *city)
{
    if (!s_lock) {
        *city = (passport_weather_city_t){0};
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *city = s_city;
    xSemaphoreGive(s_lock);
}

esp_err_t passport_weather_set_city(const passport_weather_city_t *city)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (city && !valid_city(city)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool same = city && s_city.latitude == city->latitude && s_city.longitude == city->longitude &&
                strcmp(s_city.city, city->city) == 0;
    esp_err_t err = ESP_OK;
    if (!same) {
        err = passport_config_write(WEATHER_KEY, city, sizeof(*city));
        if (err == ESP_OK) {
            s_city = city ? *city : (passport_weather_city_t){0};
            ++s_city_revision;
            s_next = 0;
            s_fetched_at = 0;
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

void passport_weather_status(passport_weather_status_t *status)
{
    *status = (passport_weather_status_t){0};
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    status->configured = s_city.city[0] != '\0';
    status->ready = s_fetched_at && esp_timer_get_time() - s_fetched_at <= STALE_US;
    status->pending = !s_next || s_fetching;
    status->temperature = s_forecast.temperature;
    status->condition = s_forecast.condition;
    xSemaphoreGive(s_lock);
}

static passport_weather_condition_t condition(const char *code)
{
    if (!code) return WEATHER_UNKNOWN;
    if (strstr(code, "thunder")) return WEATHER_THUNDER;
    if (strstr(code, "snow") || strstr(code, "sleet")) return WEATHER_SNOW;
    if (strstr(code, "rain")) return WEATHER_RAIN;
    if (strcmp(code, "fog") == 0) return WEATHER_FOG;
    if (strstr(code, "cloud")) return WEATHER_CLOUDY;
    if (strncmp(code, "clearsky", 8) == 0 || strncmp(code, "fair", 4) == 0) return WEATHER_CLEAR;
    return WEATHER_UNKNOWN;
}

static cJSON *field(cJSON *json, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(json, key);
}

static bool forecast_from_prefix(const char *body, forecast_t *forecast)
{
    const char *series = strstr(body, "\"timeseries\"");
    const char *first = series ? strchr(series, '[') : NULL;
    if (!first) return false;

    // Parse the first, current forecast only. The following days can be tens of KiB.
    cJSON *entry = cJSON_ParseWithOpts(first + 1, NULL, false);
    cJSON *data = field(entry, "data");
    cJSON *temp = field(field(field(data, "instant"), "details"), "air_temperature");
    bool valid = cJSON_IsNumber(temp) && temp->valuedouble >= -100 && temp->valuedouble <= 100;
    if (valid) {
        double value = temp->valuedouble;
        forecast->temperature = (int)(value < 0 ? value - .5 : value + .5);
        cJSON *summary = field(field(data, "next_1_hours"), "summary");
        if (!summary) summary = field(field(data, "next_6_hours"), "summary");
        forecast->condition = condition(cJSON_GetStringValue(field(summary, "symbol_code")));
    }
    cJSON_Delete(entry);
    return valid;
}

static esp_err_t capture_headers(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_HEADER) return ESP_OK;
    forecast_headers_t *headers = event->user_data;
    if (strcasecmp(event->header_key, "Expires") == 0) {
        struct tm utc = {0};
        // HTTP dates are always UTC. ESP-IDF starts with a UTC process timezone.
        const char *end = strptime(event->header_value, "%a, %d %b %Y %H:%M:%S GMT", &utc);
        if (end && !*end) headers->expires = mktime(&utc);
    } else if (strcasecmp(event->header_key, "Last-Modified") == 0 &&
               !strpbrk(event->header_value, "\r\n") && strlen(event->header_value) < sizeof(headers->modified)) {
        strlcpy(headers->modified, event->header_value, sizeof(headers->modified));
    } else if (strcasecmp(event->header_key, "Content-Encoding") == 0) {
        strlcpy(headers->encoding, event->header_value, sizeof(headers->encoding));
    } else if (strcasecmp(event->header_key, "Location") == 0 &&
               strlen(event->header_value) < sizeof(headers->redirect)) {
        strlcpy(headers->redirect, event->header_value, sizeof(headers->redirect));
    }
    return ESP_OK;
}

static int forecast_status(esp_http_client_handle_t client, forecast_headers_t *headers)
{
    for (unsigned redirects = 0; redirects <= 2; ++redirects) {
        *headers = (forecast_headers_t){0};
        if (esp_http_client_open(client, 0) != ESP_OK || esp_http_client_fetch_headers(client) < 0) return -1;
        int status = esp_http_client_get_status_code(client);
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) return status;
        const char *location = headers->redirect;
        bool same_host = strncmp(location, "https://api.met.no/", sizeof("https://api.met.no/") - 1) == 0;
        bool local_path = location[0] == '/' && location[1] != '/';
        if ((!same_host && !local_path) || esp_http_client_close(client) != ESP_OK ||
            esp_http_client_set_url(client, location) != ESP_OK) return status;
    }
    return -1;
}

static size_t gzip_payload(const uint8_t *data, size_t size)
{
    if (size < 10 || data[0] != 0x1f || data[1] != 0x8b || data[2] != 8 || (data[3] & 0xe0)) return 0;
    unsigned flags = data[3];
    size_t offset = 10;
    if (flags & 4) {
        if (size - offset < 2) return 0;
        offset += 2 + data[offset] + 256 * data[offset + 1];
        if (offset > size) return 0;
    }
    for (unsigned flag = 8; flag <= 16; flag *= 2) {
        if (flags & flag) {
            const uint8_t *end = memchr(data + offset, 0, size - offset);
            if (!end) return 0;
            offset = (size_t)(end - data) + 1;
        }
    }
    if (flags & 2) offset += 2;
    return offset < size ? offset : 0;
}

static bool decode_forecast(char *body, size_t size, const char *encoding, forecast_t *forecast)
{
    if (!*encoding || strcmp(encoding, "identity") == 0) {
        body[size] = '\0';
        return forecast_from_prefix(body, forecast);
    }
    const bool gzip = strcasecmp(encoding, "gzip") == 0;
    if (!gzip && strcasecmp(encoding, "deflate") != 0) return false;
    size_t offset = gzip ? gzip_payload((const uint8_t *)body, size) : 0;
    if (gzip && !offset) return false;
    tinfl_decompressor *inflater = malloc(sizeof(*inflater));
    uint8_t *plain = malloc(FORECAST_BYTES);
    bool ok = false;
    if (inflater && plain) {
        tinfl_init(inflater);
        size_t input = size - offset;
        size_t output = FORECAST_BYTES - 1;
        // Only the first complete forecast object is needed from the compressed stream.
        const unsigned flags = TINFL_FLAG_HAS_MORE_INPUT | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF |
                               (gzip ? 0 : TINFL_FLAG_PARSE_ZLIB_HEADER);
        tinfl_status result = tinfl_decompress(inflater, (const uint8_t *)body + offset, &input,
                                               plain, plain, &output, flags);
        if (result >= TINFL_STATUS_DONE) {
            plain[output] = '\0';
            ok = forecast_from_prefix((const char *)plain, forecast);
        }
    }
    free(plain);
    free(inflater);
    return ok;
}

static bool fetch(const passport_weather_city_t *city, forecast_t *forecast, int64_t *delay)
{
    char path[120];
    snprintf(path, sizeof(path), "/weatherapi/locationforecast/2.0/compact?lat=%.3f&lon=%.3f",
             city->latitude / (double)PASSPORT_WEATHER_COORD_SCALE,
             city->longitude / (double)PASSPORT_WEATHER_COORD_SCALE);
    forecast_headers_t headers = {0};
    const esp_http_client_config_t config = {
        .host = WEATHER_HOST,
        .path = path,
        .user_agent = "AI-Passport/1.0 (https://gitee.com/FoloToy/ai-passport)",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
        .timeout_ms = 5000,
        .buffer_size = 1024,
        .event_handler = capture_headers,
        .user_data = &headers,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    char *body = malloc(FORECAST_BYTES);
    int status = -1;
    bool valid = false;
    if (body && esp_http_client_set_header(client, "Range", "bytes=0-4094") == ESP_OK &&
        esp_http_client_set_header(client, "Accept-Encoding", "gzip, deflate") == ESP_OK &&
        (!forecast->modified[0] ||
         esp_http_client_set_header(client, "If-Modified-Since", forecast->modified) == ESP_OK)) {
        status = forecast_status(client, &headers);
        if (status == 304 && forecast->modified[0]) {
            valid = true;
        } else if (status == 200 || status == 206) {
            int size = esp_http_client_read_response(client, body, FORECAST_BYTES - 1);
            esp_http_client_close(client);
            if (size > 0) {
                valid = decode_forecast(body, (size_t)size, headers.encoding, forecast);
                if (valid) strlcpy(forecast->modified, headers.modified, sizeof(forecast->modified));
            }
        }
    }
    free(body);
    esp_http_client_cleanup(client);
    int64_t seconds = valid ? 3600 + esp_random() % 600 : RETRY_US / SECOND_US;
    int64_t until_expiry = (int64_t)headers.expires - time(NULL);
    if (until_expiry > seconds) seconds = until_expiry;
    *delay = seconds * SECOND_US;
    if (!valid) ESP_LOGW(TAG, "Forecast unavailable, HTTP %d", status);
    return valid;
}

static void weather_worker(void *context)
{
    (void)context;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const passport_weather_city_t city = s_city;
    const unsigned revision = s_city_revision;
    forecast_t forecast = s_fetched_at ? s_forecast : (forecast_t){0};
    xSemaphoreGive(s_lock);
    int64_t delay = RETRY_US;
    bool ready = city.city[0] && fetch(&city, &forecast, &delay);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (revision == s_city_revision) {
        int64_t now = esp_timer_get_time();
        s_next = now + delay;
        if (ready) {
            s_forecast = forecast;
            s_fetched_at = now;
        }
    }
    s_fetching = false;
    xSemaphoreGive(s_lock);
    passport_web_release_https();
    vTaskDelete(NULL);
}

void passport_weather_tick(bool home)
{
    if (!home || !s_lock || time(NULL) < 1704067200) return;
    passport_network_status_t network;
    passport_network_status(&network);
    if (network.state != PASSPORT_NET_ONLINE || network.changing) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_city.city[0] && !s_fetching && esp_timer_get_time() >= s_next && passport_web_acquire_https()) {
        s_fetching = xTaskCreate(weather_worker, "passport_weather", WEATHER_STACK_BYTES, NULL, 4, NULL) == pdPASS;
        if (!s_fetching) {
            s_next = esp_timer_get_time() + RETRY_US;
            passport_web_release_https();
        }
    }
    xSemaphoreGive(s_lock);
}
