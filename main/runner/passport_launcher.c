#include "passport_launcher.h"

#include <stdio.h>
#include <string.h>

static const uint8_t brightness_steps[] = {30, 50, 70, 90, 100};

size_t passport_launcher_move(size_t selected, size_t count, bool forward)
{
    if (!count) return 0;
    if (forward) return (selected + 1) % count;
    return selected ? selected - 1 : count - 1;
}

size_t passport_launcher_first(size_t selected, size_t first, size_t count, size_t visible)
{
    if (selected < first) first = selected;
    else if (selected - first >= visible) first = selected - visible + 1;
    size_t last = count > visible ? count - visible : 0;
    return first > last ? last : first;
}

void passport_launcher_open(passport_launcher_t *launcher)
{
    static const passport_launcher_page_t home_pages[HOME_ITEMS] = {DEVELOP_PAGE, APPS_PAGE, SETTINGS_PAGE};
    if (launcher->page == HOME_PAGE) launcher->page = home_pages[launcher->home_item];
    else if (launcher->page == SETTINGS_PAGE && launcher->setting_item == SETTING_WIFI) launcher->page = WIFI_PAGE;
}

void passport_launcher_back(passport_launcher_t *launcher)
{
    if (launcher->page == WIFI_PAGE) launcher->page = SETTINGS_PAGE;
    else if (launcher->page == MANAGE_PAGE) launcher->page = APPS_PAGE;
    else launcher->page = HOME_PAGE;
}

bool passport_launcher_prefs_valid(const passport_launcher_prefs_t *prefs)
{
    return prefs->show_clock <= 1 && prefs->brightness_step < sizeof(brightness_steps) &&
           prefs->theme < PASSPORT_THEME_COUNT;
}

uint8_t passport_launcher_brightness(const passport_launcher_prefs_t *prefs)
{
    return brightness_steps[prefs->brightness_step];
}

void passport_launcher_change(passport_launcher_prefs_t *prefs, size_t item)
{
    if (item == SETTING_HOME) prefs->show_clock = !prefs->show_clock;
    if (item == SETTING_THEME) prefs->theme = (prefs->theme + 1) % PASSPORT_THEME_COUNT;
    if (item == SETTING_BRIGHTNESS) {
        prefs->brightness_step = (prefs->brightness_step + 1) % sizeof(brightness_steps);
    }
}

bool passport_launcher_clock(time_t utc, passport_launcher_clock_t *clock)
{
    struct tm parts;
    // The unsynchronized ESP-IDF wall clock starts at the Unix epoch.
    if (utc >= 1704067200 && utc < INT64_C(4102444800)) {
        const time_t local = utc + 8 * 60 * 60;
        if (gmtime_r(&local, &parts)) {
            strftime(clock->time, sizeof(clock->time), "%H:%M", &parts);
            strftime(clock->date, sizeof(clock->date), "%m/%d", &parts);
            return true;
        }
    }
    strcpy(clock->time, "--:--");
    clock->date[0] = '\0';
    return false;
}
