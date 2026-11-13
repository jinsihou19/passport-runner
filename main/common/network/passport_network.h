#pragma once

#include "passport_config.h"

enum { PASSPORT_CODE_CHARS = 8 };

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
    bool saved;
    bool changing;
} passport_network_status_t;

esp_err_t passport_network_init(void);
void passport_network_tick(void);
void passport_network_status(passport_network_status_t *status);
const char *passport_network_ap(void);
const char *passport_network_code(void);
esp_err_t passport_network_try(const passport_wifi_t *wifi);
esp_err_t passport_network_setup(bool clear_saved);
