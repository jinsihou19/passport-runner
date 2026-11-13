#include "passport_model.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool private_ipv4(const char *host, size_t len)
{
    if (!len || len > 15) return false;
    unsigned octet[4];
    size_t offset = 0;
    for (size_t i = 0; i < 4; ++i) {
        size_t start = offset;
        unsigned value = 0;
        while (offset < len && isdigit((unsigned char)host[offset])) {
            value = value * 10 + (host[offset++] - '0');
            if (value > 255) return false;
        }
        if (offset == start || (offset - start > 1 && host[start] == '0')) return false;
        octet[i] = value;
        if (i < 3 && (offset == len || host[offset++] != '.')) return false;
    }
    return offset == len && (octet[0] == 10 || (octet[0] == 192 && octet[1] == 168) ||
                             (octet[0] == 172 && octet[1] >= 16 && octet[1] <= 31));
}

bool passport_model_valid(const passport_model_t *model)
{
    size_t url_len = strnlen(model->base_url, sizeof(model->base_url));
    size_t model_len = strnlen(model->model, sizeof(model->model));
    size_t key_len = strnlen(model->api_key, sizeof(model->api_key));
    if (url_len == sizeof(model->base_url) || !model_len || model_len == sizeof(model->model) ||
        key_len == sizeof(model->api_key) || passport_utf8_prefix(model->model, model_len) != model_len) return false;

    for (const char *ch = model->api_key; *ch; ++ch) {
        if ((unsigned char)*ch <= 0x20 || (unsigned char)*ch >= 0x7f) return false;
    }
    bool plain = strncmp(model->base_url, "http://", 7) == 0;
    if (!plain && strncmp(model->base_url, "https://", 8) != 0) return false;
    const char *host = model->base_url + (plain ? 7 : 8);
    for (const char *ch = host; *ch; ++ch) {
        if ((unsigned char)*ch <= 0x20 || (unsigned char)*ch >= 0x7f || strchr("@?#\\", *ch)) return false;
    }
    size_t host_len = strcspn(host, ":/");
    if (!host_len || (plain && !private_ipv4(host, host_len))) return false;
    for (size_t i = 0; i < host_len; ++i) {
        unsigned char ch = host[i];
        if (!isalnum(ch) && ch != '-' && ch != '.') return false;
    }
    if (host[host_len] == ':') {
        const char *port = host + host_len + 1;
        char *end;
        if (!isdigit((unsigned char)*port)) return false;
        unsigned long number = strtoul(port, &end, 10);
        if (number == 0 || number > 65535 || (*end && *end != '/')) return false;
    }
    return true;
}
