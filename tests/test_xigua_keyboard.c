#include <assert.h>
#include <stdio.h>
#include "xigua_keyboard.h"

int main(void)
{
    assert(xigua_keyboard_page_count(26) == 1);
    assert(xigua_keyboard_page_count(44) == 2);
    assert(xigua_keyboard_step(0, 30, -1) == 29);
    assert(xigua_keyboard_step(29, 30, 1) == 0);
    assert(xigua_keyboard_row(2, 26, 0, 1) == 8);
    assert(xigua_keyboard_row(8, 26, 0, -1) == 2);
    assert(xigua_keyboard_row(11, 26, 0, 1) == 17);
    assert(xigua_keyboard_row(25, 26, 0, 1) == 26);
    assert(xigua_keyboard_row(26, 26, 0, -1) == 24);
    xigua_keyboard_input_t input = {0};
    size_t page = 0, index = 0;
    index = xigua_keyboard_press(&input, index, 26, &page, 1);
    assert(index == 1); /* Immediate single press, no click callback needed. */
    index = xigua_keyboard_press(&input, index, 26, &page, 1);
    index = xigua_keyboard_double(&input, index, 26, &page, 1);
    assert(index == 6); /* Double moves exactly one row from the original key. */
    for (int i = 0; i < 4; ++i)
        index = xigua_keyboard_press(&input, index, 26, &page, 1);
    assert(index == 10); /* Rapid multi-clicks never freeze the cursor. */
    input = (xigua_keyboard_input_t){0};
    index = 11; page = 0;
    index = xigua_keyboard_press(&input, index, 44, &page, 1);
    index = xigua_keyboard_press(&input, index, 44, &page, 1);
    index = xigua_keyboard_double(&input, index, 44, &page, 1);
    assert(page == 0 && index == 17); /* Restore the gesture page at a boundary. */
    /* All populated rows and actions are reachable on every partial page. */
    for (size_t n = 1; n <= 64; ++n) {
        for (size_t page = 0; page < xigua_keyboard_page_count(n); ++page) {
            size_t start = page * XIGUA_KEYBOARD_PAGE_KEYS;
            size_t end = start + XIGUA_KEYBOARD_PAGE_KEYS;
            if (end > n) end = n;
            for (size_t i = start; i < n + XIGUA_KEYBOARD_ACTIONS; ++i) {
                if (i >= end && i < n) continue;
                for (int direction = -1; direction <= 1; direction += 2) {
                    size_t next = xigua_keyboard_row(i, n, page, direction);
                    assert((next >= start && next < end) ||
                           (next >= n && next < n + XIGUA_KEYBOARD_ACTIONS));
                }
            }
        }
    }
    puts("Xigua keyboard paging and row navigation: PASS");
    return 0;
}
