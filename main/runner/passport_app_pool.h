#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "passport_boot_request.h"

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint32_t id;
} passport_app_extent_t;

uint32_t passport_app_extent_bytes(uint32_t size);
// Both functions take extents sorted by offset with no overlaps.
bool passport_app_pool_find(const passport_app_extent_t *extents, size_t count,
                            uint32_t capacity, uint32_t size, uint32_t *offset);
bool passport_app_pool_valid(const passport_app_extent_t *extents, size_t count, uint32_t capacity);
