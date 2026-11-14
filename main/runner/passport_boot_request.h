#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    PASSPORT_APP_ALIGNMENT = 0x10000,
    PASSPORT_BOOT_ADDRESS = 0x12000,
    PASSPORT_BOOT_MAGIC = 0x52554e31,
};

typedef struct {
    uint32_t magic;
    uint32_t offset;
    uint32_t size;
    uint32_t check;
} passport_boot_request_t;

static inline uint32_t passport_boot_check(uint32_t offset, uint32_t size)
{
    return ~(offset ^ size ^ PASSPORT_BOOT_MAGIC);
}

static inline bool passport_boot_request_valid(const passport_boot_request_t *request, uint32_t pool_size)
{
    return request->magic == PASSPORT_BOOT_MAGIC &&
           request->check == passport_boot_check(request->offset, request->size) &&
           request->offset % PASSPORT_APP_ALIGNMENT == 0 && request->offset < pool_size &&
           request->size >= 32 && request->size <= pool_size - request->offset;
}
