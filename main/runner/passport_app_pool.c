#include "passport_app_pool.h"

uint32_t passport_app_extent_bytes(uint32_t size)
{
    return (size + PASSPORT_APP_ALIGNMENT - 1) & ~(PASSPORT_APP_ALIGNMENT - 1);
}

bool passport_app_pool_find(const passport_app_extent_t *extents, size_t count,
                            uint32_t capacity, uint32_t size, uint32_t *offset)
{
    if (!size || size > capacity) return false;
    uint32_t span = passport_app_extent_bytes(size);
    uint32_t cursor = 0;
    for (size_t i = 0; i < count; ++i) {
        if (extents[i].offset - cursor >= span) break;
        cursor = extents[i].offset + passport_app_extent_bytes(extents[i].size);
    }
    if (capacity - cursor < span) return false;
    *offset = cursor;
    return true;
}

bool passport_app_pool_valid(const passport_app_extent_t *extents, size_t count, uint32_t capacity)
{
    uint32_t cursor = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t offset = extents[i].offset;
        uint32_t size = extents[i].size;
        if (!size || size > capacity || offset < cursor || offset >= capacity ||
            offset % PASSPORT_APP_ALIGNMENT || passport_app_extent_bytes(size) > capacity - offset) return false;
        cursor = offset + passport_app_extent_bytes(size);
    }
    return true;
}
