#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "passport_model.h"

static void test_model_addresses(void)
{
    passport_model_t model = {.base_url = "https://example.com:443/v1", .model = "模型"};
    assert(passport_model_valid(&model));
    strcpy(model.base_url, "http://192.168.1.30:11434/v1");
    assert(passport_model_valid(&model));
    strcpy(model.base_url, "http://172.31.0.4/v1");
    assert(passport_model_valid(&model));
    strcpy(model.base_url, "http://172.32.0.4/v1");
    assert(!passport_model_valid(&model));
    strcpy(model.base_url, "http://010.0.0.1/v1");
    assert(!passport_model_valid(&model));
    strcpy(model.base_url, "https://example.com:65536/v1");
    assert(!passport_model_valid(&model));
    strcpy(model.base_url, "https://localhost@outside.example/v1");
    assert(!passport_model_valid(&model));
    strcpy(model.base_url, "https://example.com/v1?key=secret");
    assert(!passport_model_valid(&model));
    strcpy(model.base_url, "https://example.com/v1");
    strcpy(model.api_key, "invalid\r\nheader");
    assert(!passport_model_valid(&model));
}

static void test_wifi_credentials(void)
{
    passport_wifi_t wifi = {.ssid = "2.4G 测试网络", .password = "abcdefgh"};
    assert(passport_wifi_valid(&wifi));
    wifi.password[7] = '\0';
    assert(!passport_wifi_valid(&wifi));
    wifi.password[0] = '\0';
    assert(passport_wifi_valid(&wifi));
    memset(wifi.password, 'A', 64);
    wifi.password[64] = '\0';
    assert(passport_wifi_valid(&wifi));
    wifi.password[63] = 'G';
    assert(!passport_wifi_valid(&wifi));
    memset(wifi.ssid, 'a', sizeof(wifi.ssid));
    assert(!passport_wifi_valid(&wifi));
}

int main(void)
{
    const char *sample = "A你🌱";
    assert(passport_utf8_prefix(sample, 3) == 1);
    assert(passport_utf8_prefix(sample, 4) == 4);
    assert(passport_utf8_prefix(sample, 7) == 4);
    assert(passport_utf8_prefix(sample, 20) == 8);
    assert(passport_utf8_prefix("\xed\xa0\x80", 3) == 0);
    assert(passport_utf8_prefix("\xf4\x90\x80\x80", 4) == 0);
    assert(passport_utf8_prefix("A\xe4", 20) == 1);
    test_model_addresses();
    test_wifi_credentials();
    puts("Passport input tests passed");
    return 0;
}
