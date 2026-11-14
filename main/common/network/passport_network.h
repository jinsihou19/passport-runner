#pragma once

#include <stdint.h>

#include "passport_config.h"

enum { PASSPORT_CODE_CHARS = 8, PASSPORT_WIFI_SLOTS = 2, PASSPORT_WIFI_SCAN_LIMIT = 16 };

typedef struct {
    passport_wifi_t slots[PASSPORT_WIFI_SLOTS];
} passport_wifi_profiles_t;

typedef enum {
    PASSPORT_SCAN_IDLE,
    PASSPORT_SCAN_RUNNING,
    PASSPORT_SCAN_DONE,
    PASSPORT_SCAN_FAILED,
} passport_scan_state_t;

typedef struct {
    char ssid[sizeof(((passport_wifi_t *)0)->ssid)];
    int8_t rssi;
    bool open;
} passport_wifi_access_point_t;

typedef struct {
    passport_scan_state_t state;
    size_t count;
    passport_wifi_access_point_t points[PASSPORT_WIFI_SCAN_LIMIT];
} passport_wifi_scan_t;

typedef enum {
    PASSPORT_NET_STARTING,
    PASSPORT_NET_SETUP,
    PASSPORT_NET_CONNECTING,
    PASSPORT_NET_ONLINE,
    PASSPORT_NET_FAILED,
} passport_network_state_t;

typedef struct {
    passport_network_state_t state;
    char ip[16];
    const char *error;
    unsigned saved_count;
    bool changing;
} passport_network_status_t;

esp_err_t passport_network_init(void);
void passport_network_tick(void);
void passport_network_status(passport_network_status_t *status);
const char *passport_network_ap(void);
const char *passport_network_code(void);
esp_err_t passport_network_try(const passport_wifi_t *wifi);
esp_err_t passport_network_setup(bool clear_saved);
esp_err_t passport_network_scan(void);
void passport_network_scan_status(passport_wifi_scan_t *scan);

void passport_wifi_remember(passport_wifi_profiles_t *profiles, const passport_wifi_t *wifi);
const passport_wifi_t *passport_wifi_next(const passport_wifi_profiles_t *profiles,
                                         const passport_wifi_t *current, unsigned *attempted);
void passport_wifi_scan_add(passport_wifi_scan_t *scan, const char *ssid, int8_t rssi, bool open);
