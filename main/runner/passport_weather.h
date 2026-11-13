#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

enum { PASSPORT_WEATHER_COORD_SCALE = 1000 };

typedef struct {
    int32_t latitude;
    int32_t longitude;
    char city[64];
} passport_weather_city_t;

typedef enum {
    WEATHER_UNKNOWN, WEATHER_CLEAR, WEATHER_CLOUDY, WEATHER_FOG, WEATHER_RAIN, WEATHER_SNOW, WEATHER_THUNDER,
} passport_weather_condition_t;

typedef struct {
    bool configured;
    bool ready;
    bool pending;
    int temperature;
    passport_weather_condition_t condition;
} passport_weather_status_t;

void passport_weather_init(void);
void passport_weather_tick(bool home);
void passport_weather_status(passport_weather_status_t *status);
void passport_weather_city(passport_weather_city_t *city);
esp_err_t passport_weather_set_city(const passport_weather_city_t *city);
