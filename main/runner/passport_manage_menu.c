#include "passport_manage_menu.h"

void passport_manage_open(passport_manage_menu_t *menu)
{
    *menu = (passport_manage_menu_t){.page = PASSPORT_MANAGE_DETAILS, .delete_selected = true};
}

bool passport_manage_click(passport_manage_menu_t *menu, bool ok_key)
{
    if (menu->page == PASSPORT_MANAGE_CLOSED) return false;
    if (!ok_key) {
        menu->delete_selected = !menu->delete_selected;
        return false;
    }
    if (menu->page == PASSPORT_MANAGE_DETAILS && menu->delete_selected) {
        menu->page = PASSPORT_MANAGE_CONFIRM;
        menu->delete_selected = false;
        return false;
    }
    if (menu->page == PASSPORT_MANAGE_CONFIRM && !menu->delete_selected) {
        passport_manage_open(menu);
        return false;
    }
    bool remove = menu->page == PASSPORT_MANAGE_CONFIRM;
    menu->page = PASSPORT_MANAGE_CLOSED;
    return remove;
}
