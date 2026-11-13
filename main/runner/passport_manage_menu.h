#pragma once

#include <stdbool.h>

typedef enum {
    PASSPORT_MANAGE_CLOSED,
    PASSPORT_MANAGE_DETAILS,
    PASSPORT_MANAGE_CONFIRM,
} passport_manage_page_t;

typedef struct {
    passport_manage_page_t page;
    bool delete_selected;
} passport_manage_menu_t;

void passport_manage_open(passport_manage_menu_t *menu);
// An UP or DOWN click moves the selection. Only OK on the selected confirmation requests deletion.
bool passport_manage_click(passport_manage_menu_t *menu, bool ok_key);
