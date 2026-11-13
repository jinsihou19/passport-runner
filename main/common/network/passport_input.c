#include "passport_config.h"

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
