#include "xigua_menu.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    size_t focus = 0;
    for (size_t round = 0; round < 4; ++round) {
        for (size_t item = 0; item < 7; ++item) {
            assert(focus == item);
            size_t first = xigua_menu_first(focus, 3);
            assert(first <= focus && focus < first + 3);
            assert(first == (item < 3 ? 0 : item < 6 ? 3 : 6));
            focus = xigua_menu_move(focus, 7, true);
        }
    }
    assert(xigua_menu_move(0, 7, false) == 6);
    assert(xigua_menu_first(6, 3) == 6); /* Last page has one valid card. */
    assert(xigua_menu_move(6, 7, true) == 0);
    assert(xigua_menu_move(3, 0, true) == 0);
    assert(xigua_menu_first(0, 0) == 0);
    puts("Home menu wrapping and three-card pages: PASS");
}
