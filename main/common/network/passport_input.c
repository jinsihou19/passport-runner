#include "passport_network.h"

#include <ctype.h>
#include <string.h>

size_t passport_utf8_prefix(const char *text, size_t limit)
{
    const unsigned char *bytes = (const unsigned char *)text;
    size_t pos = 0;
    while (pos < limit && bytes[pos]) {
        unsigned char lead = bytes[pos];
        size_t width;
        if (lead <= 0x7f) width = 1;
        else if (lead >= 0xc2 && lead <= 0xdf) width = 2;
        else if (lead >= 0xe0 && lead <= 0xef) width = 3;
        else if (lead >= 0xf0 && lead <= 0xf4) width = 4;
        else break;
        if (width > limit - pos) break;

        for (size_t i = 1; i < width; ++i) {
            if ((bytes[pos + i] & 0xc0) != 0x80) return pos;
        }
        if (width > 2) {
            unsigned char second = bytes[pos + 1];
            if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
                (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second >= 0x90)) break;
        }
        pos += width;
    }
    return pos;
}

bool passport_wifi_valid(const passport_wifi_t *wifi)
{
    size_t ssid = strnlen(wifi->ssid, sizeof(wifi->ssid));
    size_t password = strnlen(wifi->password, sizeof(wifi->password));
    if (!ssid || ssid == sizeof(wifi->ssid) || passport_utf8_prefix(wifi->ssid, ssid) != ssid) return false;
    if (!password) return true;
    if (password < 8 || password >= sizeof(wifi->password)) return false;
    for (size_t i = 0; i < password; ++i) {
        unsigned char ch = wifi->password[i];
        if (password == 64 ? !isxdigit(ch) : (ch < 0x20 || ch > 0x7e)) return false;
    }
    return true;
}

void passport_wifi_remember(passport_wifi_profiles_t *profiles, const passport_wifi_t *wifi)
{
    if (strcmp(profiles->slots[0].ssid, wifi->ssid) != 0) profiles->slots[1] = profiles->slots[0];
    profiles->slots[0] = *wifi;
}

const passport_wifi_t *passport_wifi_next(const passport_wifi_profiles_t *profiles,
                                         const passport_wifi_t *current, unsigned *attempted)
{
    if (current) {
        for (unsigned i = 0; i < PASSPORT_WIFI_SLOTS; ++i) {
            const passport_wifi_t *wifi = &profiles->slots[i];
            if (strcmp(current->ssid, wifi->ssid) == 0 && strcmp(current->password, wifi->password) == 0) {
                *attempted |= 1U << i;
                break;
            }
        }
    }
    for (unsigned i = 0; i < PASSPORT_WIFI_SLOTS; ++i) {
        unsigned bit = 1U << i;
        if (!(*attempted & bit) && profiles->slots[i].ssid[0]) {
            *attempted |= bit;
            return &profiles->slots[i];
        }
    }
    return NULL;
}

void passport_wifi_scan_add(passport_wifi_scan_t *scan, const char *ssid, int8_t rssi, bool open)
{
    size_t length = strnlen(ssid, sizeof(scan->points[0].ssid));
    if (!length || length >= sizeof(scan->points[0].ssid) || passport_utf8_prefix(ssid, length) != length ||
        strpbrk(ssid, "\r\n")) return;

    size_t index = 0;
    while (index < scan->count && strcmp(scan->points[index].ssid, ssid) != 0) ++index;
    if (index < scan->count && scan->points[index].rssi >= rssi) return;
    if (index == scan->count && scan->count < PASSPORT_WIFI_SCAN_LIMIT) ++scan->count;

    while (index && scan->points[index - 1].rssi < rssi) {
        if (index < PASSPORT_WIFI_SCAN_LIMIT) scan->points[index] = scan->points[index - 1];
        --index;
    }
    if (index < PASSPORT_WIFI_SCAN_LIMIT) {
        memcpy(scan->points[index].ssid, ssid, length + 1);
        scan->points[index].rssi = rssi;
        scan->points[index].open = open;
    }
}
