#include <assert.h>
#include <stdio.h>

#include "passport_manage_menu.h"

int main(void)
{
    passport_manage_menu_t menu = {0};
    assert(!passport_manage_click(&menu, true));
    passport_manage_open(&menu);
    assert(menu.page == PASSPORT_MANAGE_DETAILS);

    // Repeated OK presses on opening the menu can only alternate between
    // the app details and the confirmation with Cancel selected.
    for (int i = 0; i < 3; ++i) {
        assert(!passport_manage_click(&menu, true));
        assert(menu.page == PASSPORT_MANAGE_CONFIRM && !menu.delete_selected);
        assert(!passport_manage_click(&menu, true));
        assert(menu.page == PASSPORT_MANAGE_DETAILS);
    }

    // Selecting Return closes the details without deleting.
    assert(!passport_manage_click(&menu, false));
    assert(!passport_manage_click(&menu, true));
    assert(menu.page == PASSPORT_MANAGE_CLOSED);

    // An explicit selection on the confirmation screen is required.
    passport_manage_open(&menu);
    assert(!passport_manage_click(&menu, true));
    assert(!passport_manage_click(&menu, false));
    assert(menu.page == PASSPORT_MANAGE_CONFIRM && menu.delete_selected);
    assert(passport_manage_click(&menu, true));
    assert(menu.page == PASSPORT_MANAGE_CLOSED);
    puts("Passport management menu: cancel, return and explicit delete PASS");
    return 0;
}
