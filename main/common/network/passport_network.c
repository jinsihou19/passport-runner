#include "passport_network.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bootloader_random.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

static const char *TAG = "passport_net";
static const char WIFI_KEY[] = "wifi_v2";
static const char LEGACY_WIFI_KEY[] = "wifi_v1";
enum { SIGNAL_IP = BIT0, SIGNAL_DISCONNECTED = BIT1, SIGNAL_SCAN_DONE = BIT2, SIGNAL_SCAN_FAILED = BIT3 };
enum { SWITCH_DELAY_US = 800000, CONNECT_TIMEOUT_US = 16000000, RETRY_DELAY_US = 1200000 };
enum { SCAN_TIMEOUT_US = 5500000 };
typedef enum { SWITCH_NONE, SWITCH_STATION, SWITCH_SETUP } switch_t;

static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_events;
static esp_netif_t *s_ap;
static esp_netif_t *s_sta;
static char s_ap_name[20];
static char s_code[PASSPORT_CODE_CHARS + 1];
static passport_network_status_t s_status = {.state = PASSPORT_NET_STARTING, .error = ""};
static switch_t s_requested;
static passport_wifi_t s_candidate;
static int64_t s_switch_at;
static passport_wifi_profiles_t s_profiles;
static passport_wifi_scan_t s_scan;

// Radio ownership stays in the application worker.
static bool s_wifi_started;
static bool s_trying_candidate;
static bool s_sntp_started;
static int64_t s_connect_deadline;
static int64_t s_retry_at;
static int64_t s_scan_deadline;
static unsigned s_attempted;
static bool s_candidate_failed;
static passport_wifi_t s_active_wifi;

const char *passport_network_ap(void)
{
    return s_ap_name;
}

const char *passport_network_code(void)
{
    return s_code;
}

static void set_state(passport_network_state_t state, const char *ip, const char *error)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = state;
    strlcpy(s_status.ip, ip ? ip : "", sizeof(s_status.ip));
    s_status.error = error ? error : "";
    xSemaphoreGive(s_lock);
}

void passport_network_status(passport_network_status_t *status)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    if (s_lock) xSemaphoreGive(s_lock);
}

static void wifi_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)ctx;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) xEventGroupSetBits(s_events, SIGNAL_IP);
    if ((base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) ||
        (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) xEventGroupSetBits(s_events, SIGNAL_DISCONNECTED);
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        const wifi_event_sta_scan_done_t *event = data;
        xEventGroupSetBits(s_events, event && !event->status ? SIGNAL_SCAN_DONE : SIGNAL_SCAN_FAILED);
    }
}

static esp_netif_t *create_netif(bool station)
{
    esp_netif_config_t config = station ? (esp_netif_config_t)ESP_NETIF_DEFAULT_WIFI_STA()
                                       : (esp_netif_config_t)ESP_NETIF_DEFAULT_WIFI_AP();
    esp_netif_t *netif = esp_netif_new(&config);
    if (!netif) return NULL;
    esp_err_t err = station ? esp_netif_attach_wifi_station(netif) : esp_netif_attach_wifi_ap(netif);
    if (err == ESP_OK) err = station ? esp_wifi_set_default_wifi_sta_handlers() : esp_wifi_set_default_wifi_ap_handlers();
    if (err != ESP_OK) {
        esp_netif_destroy_default_wifi(netif);
        return NULL;
    }
    return netif;
}

static esp_err_t start_radio(wifi_mode_t mode, wifi_config_t *config)
{
    esp_err_t err = s_wifi_started ? esp_wifi_stop() : ESP_OK;
    if (err != ESP_OK) return err;
    s_wifi_started = false;
    xEventGroupClearBits(s_events, SIGNAL_IP | SIGNAL_DISCONNECTED | SIGNAL_SCAN_DONE | SIGNAL_SCAN_FAILED);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_scan.state = PASSPORT_SCAN_IDLE;
    s_scan.count = 0;
    xSemaphoreGive(s_lock);
    err = esp_wifi_set_mode(mode);
    if (err == ESP_OK) err = esp_wifi_set_config(mode == WIFI_MODE_STA ? WIFI_IF_STA : WIFI_IF_AP, config);
    if (err == ESP_OK) err = esp_wifi_start();
    s_wifi_started = err == ESP_OK;
    return err;
}

static esp_err_t start_setup(const char *reason)
{
    s_trying_candidate = false;
    s_candidate_failed = false;
    s_retry_at = 0;
    memset(&s_active_wifi, 0, sizeof(s_active_wifi));
    wifi_config_t config = {.ap = {.channel = 1, .max_connection = 1, .authmode = WIFI_AUTH_WPA2_PSK}};
    config.ap.ssid_len = strlen(s_ap_name);
    memcpy(config.ap.ssid, s_ap_name, config.ap.ssid_len);
    memcpy(config.ap.password, s_code, sizeof(s_code));
    // The station interface scans while the setup hotspot serves the browser.
    esp_err_t err = start_radio(WIFI_MODE_APSTA, &config);
    esp_netif_ip_info_t address;
    if (err == ESP_OK) err = esp_netif_get_ip_info(s_ap, &address);
    if (err == ESP_OK) {
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&address.ip));
        set_state(PASSPORT_NET_SETUP, ip, reason);
        ESP_LOGI(TAG, "Provisioning AP ready, free=%zu, largest=%zu",
                 heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    } else {
        set_state(PASSPORT_NET_FAILED, NULL, "wifi_start");
        ESP_LOGE(TAG, "Unable to start provisioning: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t start_station(const passport_wifi_t *wifi, bool candidate)
{
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, wifi->ssid, strlen(wifi->ssid));
    memcpy(config.sta.password, wifi->password, strlen(wifi->password));
    config.sta.threshold.authmode = wifi->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    set_state(PASSPORT_NET_CONNECTING, NULL, NULL);
    esp_err_t err = start_radio(WIFI_MODE_STA, &config);
    if (err != ESP_OK) return err;
    s_active_wifi = *wifi;
    s_trying_candidate = candidate;
    s_retry_at = esp_timer_get_time();
    s_connect_deadline = s_retry_at + CONNECT_TIMEOUT_US;
    return ESP_OK;
}

static esp_err_t try_saved(const passport_wifi_t *current, const char *reason)
{
    for (;;) {
        passport_wifi_t next;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const passport_wifi_t *saved = passport_wifi_next(&s_profiles, current, &s_attempted);
        if (saved) next = *saved;
        xSemaphoreGive(s_lock);
        if (!saved) return start_setup(reason);
        if (start_station(&next, false) == ESP_OK) return ESP_OK;
        reason = "wifi_start";
    }
}

static void load_profiles(void)
{
    if (passport_config_read(WIFI_KEY, &s_profiles, sizeof(s_profiles)) != ESP_OK) {
        passport_config_read(LEGACY_WIFI_KEY, &s_profiles.slots[0], sizeof(passport_wifi_t));
    }
    passport_wifi_t *primary = &s_profiles.slots[0];
    passport_wifi_t *backup = &s_profiles.slots[1];
    if (!passport_wifi_valid(backup)) memset(backup, 0, sizeof(*backup));
    if (!passport_wifi_valid(primary)) {
        *primary = *backup;
        memset(backup, 0, sizeof(*backup));
    }
    if (strcmp(primary->ssid, backup->ssid) == 0) memset(backup, 0, sizeof(*backup));
    s_status.saved_count = (primary->ssid[0] != 0) + (backup->ssid[0] != 0);
}

esp_err_t passport_network_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (!s_lock || !s_events) return ESP_ERR_NO_MEM;

    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) return err;
    snprintf(s_ap_name, sizeof(s_ap_name), "Passport-%02X%02X", mac[4], mac[5]);
    static const char code_alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    uint8_t random[PASSPORT_CODE_CHARS];
    // This runs before Wi-Fi and before the BSP owns the ADC ladder.
    bootloader_random_enable();
    esp_fill_random(random, sizeof(random));
    bootloader_random_disable();
    for (size_t i = 0; i < PASSPORT_CODE_CHARS; ++i) s_code[i] = code_alphabet[random[i] & 31];

    err = esp_netif_init();
    if (err == ESP_OK) err = esp_event_loop_create_default();
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    if (err == ESP_OK) err = esp_wifi_init(&wifi_config);
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) {
        s_sta = create_netif(true);
        s_ap = create_netif(false);
        if (!s_sta || !s_ap) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    if (err != ESP_OK) {
        set_state(PASSPORT_NET_FAILED, NULL, "wifi_init");
        return err;
    }

    load_profiles();
    return try_saved(NULL, NULL);
}

static bool can_switch(void)
{
    return !s_status.changing && s_status.state != PASSPORT_NET_STARTING &&
           s_status.state != PASSPORT_NET_CONNECTING && s_status.state != PASSPORT_NET_FAILED &&
           s_scan.state != PASSPORT_SCAN_RUNNING;
}

esp_err_t passport_network_scan(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool allowed = can_switch();
    if (allowed) {
        s_scan.state = PASSPORT_SCAN_RUNNING;
        s_scan.count = 0;
    }
    xSemaphoreGive(s_lock);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void passport_network_scan_status(passport_wifi_scan_t *scan)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    *scan = s_scan;
    if (s_lock) xSemaphoreGive(s_lock);
}

static void schedule_switch(switch_t request)
{
    s_requested = request;
    s_status.changing = true;
    s_switch_at = esp_timer_get_time() + SWITCH_DELAY_US;
}

esp_err_t passport_network_try(const passport_wifi_t *wifi)
{
    if (!passport_wifi_valid(wifi)) return ESP_ERR_INVALID_ARG;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool allowed = can_switch();
    if (allowed) {
        s_candidate = *wifi;
        schedule_switch(SWITCH_STATION);
    }
    xSemaphoreGive(s_lock);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t passport_network_setup(bool clear_saved)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = can_switch() ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && clear_saved) {
        err = passport_config_write(LEGACY_WIFI_KEY, NULL, 0);
        if (err == ESP_OK) err = passport_config_write(WIFI_KEY, NULL, 0);
        if (err == ESP_OK) {
            memset(&s_profiles, 0, sizeof(s_profiles));
            s_status.saved_count = 0;
        }
    }
    if (err == ESP_OK && s_status.state != PASSPORT_NET_SETUP) schedule_switch(SWITCH_SETUP);
    xSemaphoreGive(s_lock);
    return err;
}

static bool station_ip(char ip[16])
{
    wifi_ap_record_t access_point;
    esp_netif_ip_info_t address;
    if (esp_wifi_sta_get_ap_info(&access_point) != ESP_OK ||
        strcmp((const char *)access_point.ssid, s_active_wifi.ssid) != 0 ||
        esp_netif_get_ip_info(s_sta, &address) != ESP_OK || !address.ip.addr) return false;
    snprintf(ip, 16, IPSTR, IP2STR(&address.ip));
    return true;
}

static void station_online(const char *ip)
{
    esp_err_t err = ESP_OK;
    if (s_trying_candidate) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        passport_wifi_profiles_t next = s_profiles;
        passport_wifi_remember(&next, &s_active_wifi);
        unsigned count = next.slots[1].ssid[0] ? PASSPORT_WIFI_SLOTS : 1;
        err = passport_config_write(WIFI_KEY, &next, sizeof(next));
        if (err == ESP_OK) {
            s_profiles = next;
            s_status.saved_count = count;
            // Keep the preferred network readable by previously installed apps.
            esp_err_t legacy = passport_config_write(LEGACY_WIFI_KEY, &next.slots[0], sizeof(passport_wifi_t));
            if (legacy != ESP_OK) ESP_LOGW(TAG, "Unable to update legacy Wi-Fi profile: %s", esp_err_to_name(legacy));
        }
        xSemaphoreGive(s_lock);
        s_trying_candidate = false;
    }
    s_attempted = 0;
    s_retry_at = 0;
    set_state(PASSPORT_NET_ONLINE, ip, err != ESP_OK ? "wifi_save" : s_candidate_failed ? "wifi_fallback" : NULL);
    s_candidate_failed = false;
    if (!s_sntp_started) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        s_sntp_started = true;
    }
    ESP_LOGI(TAG, "Station ready, free=%zu, largest=%zu",
             heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static bool scan_tick(int64_t now, EventBits_t signals)
{
    if (!s_scan_deadline) {
        xEventGroupClearBits(s_events, SIGNAL_SCAN_DONE | SIGNAL_SCAN_FAILED);
        if (esp_wifi_scan_start(NULL, false) == ESP_OK) {
            s_scan_deadline = now + SCAN_TIMEOUT_US;
            return true;
        }
    } else if (!(signals & (SIGNAL_SCAN_DONE | SIGNAL_SCAN_FAILED)) && now < s_scan_deadline) {
        return true;
    }

    passport_wifi_scan_t result = {.state = PASSPORT_SCAN_FAILED};
    if (s_scan_deadline && (signals & SIGNAL_SCAN_DONE) && !(signals & SIGNAL_SCAN_FAILED)) {
        uint16_t count = 0;
        if (esp_wifi_scan_get_ap_num(&count) == ESP_OK) {
            result.state = PASSPORT_SCAN_DONE;
            for (uint16_t i = 0; i < count; ++i) {
                wifi_ap_record_t point;
                if (esp_wifi_scan_get_ap_record(&point) != ESP_OK) {
                    result.state = PASSPORT_SCAN_FAILED;
                    result.count = 0;
                    break;
                }
                switch (point.authmode) {
                case WIFI_AUTH_OPEN:
                case WIFI_AUTH_WPA2_PSK:
                case WIFI_AUTH_WPA_WPA2_PSK:
                case WIFI_AUTH_WPA3_PSK:
                case WIFI_AUTH_WPA2_WPA3_PSK:
                case WIFI_AUTH_WPA3_EXT_PSK:
                case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
                    passport_wifi_scan_add(&result, (const char *)point.ssid, point.rssi,
                                           point.authmode == WIFI_AUTH_OPEN);
                    break;
                default:
                    break;
                }
            }
        }
    } else if (s_scan_deadline && !(signals & (SIGNAL_SCAN_DONE | SIGNAL_SCAN_FAILED))) {
        esp_wifi_scan_stop();
    }
    esp_wifi_clear_ap_list();
    s_scan_deadline = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_scan = result;
    xSemaphoreGive(s_lock);
    return false;
}

void passport_network_tick(void)
{
    if (!s_lock || !s_events) return;
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    switch_t request = now >= s_switch_at ? s_requested : SWITCH_NONE;
    if (request != SWITCH_NONE) s_requested = SWITCH_NONE;
    passport_network_state_t state = s_status.state;
    bool scanning = s_scan.state == PASSPORT_SCAN_RUNNING;
    xSemaphoreGive(s_lock);
    if (request != SWITCH_NONE) {
        if (request == SWITCH_STATION) {
            s_attempted = 0;
            s_candidate_failed = false;
            if (start_station(&s_candidate, true) != ESP_OK) {
                s_candidate_failed = true;
                try_saved(NULL, "wifi_start");
            }
        } else start_setup(NULL);
        memset(&s_candidate, 0, sizeof(s_candidate));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.changing = false;
        xSemaphoreGive(s_lock);
        return;
    }

    EventBits_t signals = xEventGroupWaitBits(s_events, SIGNAL_IP | SIGNAL_DISCONNECTED |
                                             SIGNAL_SCAN_DONE | SIGNAL_SCAN_FAILED, pdTRUE, pdFALSE, 0);
    if (scanning) scanning = scan_tick(now, signals);
    if (state != PASSPORT_NET_CONNECTING && state != PASSPORT_NET_ONLINE) return;
    char ip[16];
    if ((signals & SIGNAL_IP) && station_ip(ip)) {
        station_online(ip);
        return;
    }
    if (signals & SIGNAL_DISCONNECTED) {
        if (state == PASSPORT_NET_ONLINE) {
            set_state(PASSPORT_NET_CONNECTING, NULL, NULL);
            s_connect_deadline = now + CONNECT_TIMEOUT_US;
        }
        s_retry_at = now + RETRY_DELAY_US;
    }
    if (state == PASSPORT_NET_CONNECTING && now >= s_connect_deadline) {
        if (s_trying_candidate) s_candidate_failed = true;
        try_saved(&s_active_wifi, "wifi_timeout");
    } else if (!scanning && s_retry_at && now >= s_retry_at) {
        esp_err_t err = esp_wifi_connect();
        s_retry_at = err == ESP_OK ? 0 : now + RETRY_DELAY_US;
    }
}
