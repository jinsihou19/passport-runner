#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    HOME_PAGE,
    DEVELOP_PAGE,
    APPS_PAGE,
    SETTINGS_PAGE,
    WIFI_PAGE,
    MANAGE_PAGE,
    PAGE_COUNT,
} passport_launcher_page_t;

enum { HOME_DEVELOP, HOME_APPS, HOME_SETTINGS, HOME_ITEMS };
enum { SETTING_HOME, SETTING_THEME, SETTING_BRIGHTNESS, SETTING_WIFI, SETTING_ITEMS };
enum { PASSPORT_DEFAULT_BRIGHTNESS_STEP = 2 };
enum { PASSPORT_THEME_GREEN, PASSPORT_THEME_ORANGE, PASSPORT_THEME_COUNT };

typedef struct {
    passport_launcher_page_t page;
    size_t home_item;
    size_t setting_item;
} passport_launcher_t;

typedef struct {
    uint8_t show_clock;
    uint8_t brightness_step;
    uint8_t theme;
} passport_launcher_prefs_t;

typedef struct {
    char time[6];
    char date[6];
} passport_launcher_clock_t;

size_t passport_launcher_move(size_t selected, size_t count, bool forward);
size_t passport_launcher_first(size_t selected, size_t first, size_t count, size_t visible);
void passport_launcher_open(passport_launcher_t *launcher);
void passport_launcher_back(passport_launcher_t *launcher);
bool passport_launcher_prefs_valid(const passport_launcher_prefs_t *prefs);
uint8_t passport_launcher_brightness(const passport_launcher_prefs_t *prefs);
void passport_launcher_change(passport_launcher_prefs_t *prefs, size_t item);
bool passport_launcher_clock(time_t utc, passport_launcher_clock_t *clock);
