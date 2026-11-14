#include <assert.h>
#include <stdio.h>

#include "passport_app_pool.h"

enum { UNIT = PASSPORT_APP_ALIGNMENT, CAPACITY = 93 * UNIT };

static void test_capacity_and_alignment(void)
{
    uint32_t offset = UINT32_MAX;
    assert(passport_app_extent_bytes(1) == UNIT);
    assert(passport_app_extent_bytes(UNIT) == UNIT);
    assert(passport_app_extent_bytes(UNIT + 1) == 2 * UNIT);
    assert(passport_app_pool_find(NULL, 0, CAPACITY, 6000000, &offset));
    assert(offset == 0);
    assert(passport_app_pool_find(NULL, 0, CAPACITY, CAPACITY, &offset));
    assert(!passport_app_pool_find(NULL, 0, CAPACITY, CAPACITY + 1, &offset));
}

static void test_fragmented_pool(void)
{
    passport_app_extent_t apps[] = {
        {.offset = UNIT, .size = 2 * UNIT, .id = 1},
        {.offset = 5 * UNIT, .size = 2 * UNIT - 1, .id = 2},
    };
    uint32_t offset;
    assert(passport_app_pool_valid(apps, 2, 8 * UNIT));
    assert(passport_app_pool_find(apps, 2, 8 * UNIT, UNIT, &offset) && offset == 0);
    assert(passport_app_pool_find(apps, 2, 8 * UNIT, 2 * UNIT, &offset) && offset == 3 * UNIT);
    assert(!passport_app_pool_find(apps, 2, 8 * UNIT, 3 * UNIT, &offset));
    apps[0].offset = 0;
    assert(passport_app_pool_find(apps, 2, 8 * UNIT, 3 * UNIT, &offset) && offset == 2 * UNIT);
    apps[1].offset = UNIT;
    assert(!passport_app_pool_valid(apps, 2, 8 * UNIT));
    apps[1].offset = 2 * UNIT + 1;
    assert(!passport_app_pool_valid(apps, 2, 8 * UNIT));
    apps[1].offset = 7 * UNIT;
    assert(!passport_app_pool_valid(apps, 2, 8 * UNIT));
}

static void test_boot_request(void)
{
    passport_boot_request_t request = {.magic = PASSPORT_BOOT_MAGIC, .offset = 2 * UNIT, .size = 217000};
    request.check = passport_boot_check(request.offset, request.size);
    assert(passport_boot_request_valid(&request, CAPACITY));
    request.magic = 0;
    assert(!passport_boot_request_valid(&request, CAPACITY));
    request.magic = PASSPORT_BOOT_MAGIC;
    request.check ^= 1;
    assert(!passport_boot_request_valid(&request, CAPACITY));
    request.offset = CAPACITY - UNIT;
    request.check = passport_boot_check(request.offset, request.size);
    assert(!passport_boot_request_valid(&request, CAPACITY));
    request.offset = 1;
    request.check = passport_boot_check(request.offset, request.size);
    assert(!passport_boot_request_valid(&request, CAPACITY));
}

int main(void)
{
    test_capacity_and_alignment();
    test_fragmented_pool();
    test_boot_request();
    puts("Passport app pool tests passed");
}
