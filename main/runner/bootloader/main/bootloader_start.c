#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_flash_priv.h"
#include "esp_log.h"
#include "passport_boot_request.h"

void __attribute__((noreturn)) call_start_cpu0(void)
{
    bootloader_state_t state = {0};
    if (bootloader_init() != ESP_OK || !bootloader_utility_load_partition_table(&state)) {
        bootloader_reset();
    }
    esp_partition_pos_t pool = state.ota[0];
    state.app_count = 0;
    passport_boot_request_t request;
    uint32_t consumed = 0;
    if (pool.offset && bootloader_flash_read(PASSPORT_BOOT_ADDRESS, &request, sizeof(request), false) == ESP_OK &&
        passport_boot_request_valid(&request, pool.size) &&
        bootloader_flash_write(PASSPORT_BOOT_ADDRESS, &consumed, sizeof(consumed), false) == ESP_OK) {
        state.ota[0] = (esp_partition_pos_t){.offset = pool.offset + request.offset, .size = request.size};
        state.app_count = 1;
        ESP_LOGI("passport_boot", "Starting app at 0x%lx", (unsigned long)state.ota[0].offset);
        bootloader_utility_load_boot_image(&state, 0);
    }
    ESP_LOGI("passport_boot", "Starting Runner system");
    bootloader_utility_load_boot_image(&state, FACTORY_INDEX);
}

#if CONFIG_LIBC_NEWLIB
#include <sys/reent.h>
struct _reent *__getreent(void)
{
    return _GLOBAL_REENT;
}
#endif
