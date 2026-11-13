#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "passport_launcher.h"

static void test_navigation(void)
{
    passport_launcher_t launcher = {.page = HOME_PAGE, .home_item = HOME_APPS};
    passport_launcher_open(&launcher);
    assert(launcher.page == APPS_PAGE);
    launcher.page = MANAGE_PAGE;
    passport_launcher_back(&launcher);
    assert(launcher.page == APPS_PAGE);
    passport_launcher_back(&launcher);
    assert(launcher.page == HOME_PAGE && launcher.home_item == HOME_APPS);

    launcher.home_item = passport_launcher_move(launcher.home_item, HOME_ITEMS, false);
    passport_launcher_open(&launcher);
    assert(launcher.page == DEVELOP_PAGE);
    passport_launcher_back(&launcher);
    launcher.home_item = passport_launcher_move(launcher.home_item, HOME_ITEMS, false);
    assert(launcher.home_item == HOME_SETTINGS);
    passport_launcher_open(&launcher);
    assert(launcher.page == SETTINGS_PAGE);
    launcher.setting_item = SETTING_WIFI;
    passport_launcher_open(&launcher);
    assert(launcher.page == WIFI_PAGE);
    passport_launcher_back(&launcher);
    assert(launcher.page == SETTINGS_PAGE && launcher.setting_item == SETTING_WIFI);

    assert(passport_launcher_move(0, 0, true) == 0);
    assert(passport_launcher_move(0, 4, false) == 3);
    assert(passport_launcher_move(3, 4, true) == 0);
    assert(passport_launcher_first(3, 0, 4, 3) == 1);
    assert(passport_launcher_first(0, 1, 4, 3) == 0);
    assert(passport_launcher_first(1, 2, 2, 3) == 0);
    assert(passport_launcher_first(0, 2, 0, 3) == 0);
}

static void test_preferences(void)
{
    passport_launcher_prefs_t prefs = {.brightness_step = PASSPORT_DEFAULT_BRIGHTNESS_STEP};
    assert(passport_launcher_prefs_valid(&prefs));
    assert(!prefs.show_clock && passport_launcher_brightness(&prefs) == 70);
    assert(prefs.theme == PASSPORT_THEME_GREEN);
    passport_launcher_change(&prefs, SETTING_THEME);
    assert(prefs.theme == PASSPORT_THEME_ORANGE && passport_launcher_prefs_valid(&prefs));
    passport_launcher_change(&prefs, SETTING_THEME);
    assert(prefs.theme == PASSPORT_THEME_GREEN);
    passport_launcher_change(&prefs, SETTING_HOME);
    assert(prefs.show_clock && passport_launcher_brightness(&prefs) == 70);
    passport_launcher_change(&prefs, SETTING_HOME);
    assert(!prefs.show_clock);
    const uint8_t expected[] = {90, 100, 30, 50, 70};
    for (size_t i = 0; i < sizeof(expected); ++i) {
        passport_launcher_change(&prefs, SETTING_BRIGHTNESS);
        assert(passport_launcher_brightness(&prefs) == expected[i]);
    }
    prefs.show_clock = 2;
    assert(!passport_launcher_prefs_valid(&prefs));
    prefs.show_clock = 0;
    prefs.theme = PASSPORT_THEME_COUNT;
    assert(!passport_launcher_prefs_valid(&prefs));
    prefs.theme = PASSPORT_THEME_GREEN;
    prefs.brightness_step = UINT8_MAX;
    assert(!passport_launcher_prefs_valid(&prefs));
}

static void test_clock(void)
{
    passport_launcher_clock_t clock;
    assert(!passport_launcher_clock(0, &clock));
    assert(strcmp(clock.time, "--:--") == 0 && clock.date[0] == '\0');
    assert(passport_launcher_clock(1735660740, &clock));
    assert(strcmp(clock.time, "23:59") == 0 && strcmp(clock.date, "12/31") == 0);
    assert(passport_launcher_clock(1735660800, &clock));
    assert(strcmp(clock.time, "00:00") == 0 && strcmp(clock.date, "01/01") == 0);
}

int main(void)
{
    test_navigation();
    test_preferences();
    test_clock();
    puts("Passport launcher tests: PASS");
}
