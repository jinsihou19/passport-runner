#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "passport_model.h"
#include "passport_network.h"

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

static void test_wifi_profiles(void)
{
    const passport_wifi_t home = {.ssid = "Home", .password = "example1"};
    const passport_wifi_t mobile = {.ssid = "Mobile", .password = "example2"};
    const passport_wifi_t office = {.ssid = "Office", .password = "example3"};
    const passport_wifi_t changed = {.ssid = "Mobile", .password = "example4"};
    passport_wifi_profiles_t profiles = {.slots = {home}}; // Existing single-network configuration.

    passport_wifi_remember(&profiles, &mobile);
    passport_wifi_remember(&profiles, &changed);
    assert(strcmp(profiles.slots[0].password, changed.password) == 0);
    assert(strcmp(profiles.slots[1].ssid, home.ssid) == 0);

    passport_wifi_remember(&profiles, &home);
    assert(strcmp(profiles.slots[0].ssid, home.ssid) == 0);
    assert(strcmp(profiles.slots[1].ssid, mobile.ssid) == 0);
    passport_wifi_remember(&profiles, &office);
    assert(strcmp(profiles.slots[1].ssid, home.ssid) == 0);

    unsigned attempted = 0;
    assert(passport_wifi_next(&profiles, NULL, &attempted) == &profiles.slots[0]);
    assert(passport_wifi_next(&profiles, &office, &attempted) == &profiles.slots[1]);
    assert(!passport_wifi_next(&profiles, &home, &attempted));

    attempted = 0;
    assert(passport_wifi_next(&profiles, &home, &attempted) == &profiles.slots[0]);
    assert(!passport_wifi_next(&profiles, &office, &attempted));

    attempted = 0;
    const passport_wifi_t wrong_password = {.ssid = "Office", .password = "incorrect"};
    assert(passport_wifi_next(&profiles, &wrong_password, &attempted) == &profiles.slots[0]);
    assert(passport_wifi_next(&profiles, &office, &attempted) == &profiles.slots[1]);
}

static void test_wifi_scan_results(void)
{
    passport_wifi_scan_t scan = {0};
    passport_wifi_scan_add(&scan, "", -30, true);
    passport_wifi_scan_add(&scan, "\xff", -30, true);
    passport_wifi_scan_add(&scan, "Hidden\nline", -30, true);
    assert(!scan.count);
    for (unsigned i = 0; i < PASSPORT_WIFI_SCAN_LIMIT; ++i) {
        char ssid[20];
        snprintf(ssid, sizeof(ssid), "Network %u", i);
        passport_wifi_scan_add(&scan, ssid, -80 + (int)i, false);
    }
    passport_wifi_scan_add(&scan, "Far away", -90, false);
    assert(strcmp(scan.points[PASSPORT_WIFI_SCAN_LIMIT - 1].ssid, "Network 0") == 0);
    passport_wifi_scan_add(&scan, "咖啡店", -32, true);
    assert(scan.count == PASSPORT_WIFI_SCAN_LIMIT);
    assert(strcmp(scan.points[0].ssid, "咖啡店") == 0 && scan.points[0].open);
    assert(strcmp(scan.points[PASSPORT_WIFI_SCAN_LIMIT - 1].ssid, "Network 1") == 0);
    passport_wifi_scan_add(&scan, "Network 5", -25, false);
    passport_wifi_scan_add(&scan, "Network 5", -28, true);
    assert(scan.count == PASSPORT_WIFI_SCAN_LIMIT);
    assert(strcmp(scan.points[0].ssid, "Network 5") == 0 && !scan.points[0].open);
    assert(scan.points[0].rssi == -25 && strcmp(scan.points[1].ssid, "咖啡店") == 0);
    for (size_t i = 1; i < scan.count; ++i) assert(strcmp(scan.points[i].ssid, "Network 5") != 0);
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
    test_wifi_profiles();
    test_wifi_scan_results();
    puts("Passport input tests passed");
    return 0;
}
