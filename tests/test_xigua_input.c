#include <assert.h>
#include <stdio.h>
#include "xigua_input.h"

int main(void)
{
    xigua_input_t input;
    int key = -1;
    xigua_input_init(&input);
    assert(xigua_input_press(&input, 2, 0, true) == XG_INPUT_WAKE);
    assert(xigua_input_tick(&input, 700, &key) == XG_INPUT_NONE);
    assert(xigua_input_release(&input, 2, 900) == XG_INPUT_NONE);
    assert(xigua_input_press(&input, 2, 1000, false) == XG_INPUT_NONE);
    assert(xigua_input_release(&input, 2, 1100) == XG_INPUT_CLICK);
    xigua_input_press(&input, 0, 2000, false);
    assert(xigua_input_tick(&input, 2599, &key) == XG_INPUT_NONE);
    assert(xigua_input_tick(&input, 2600, &key) == XG_INPUT_LONG && key == 0);
    assert(xigua_input_tick(&input, 4000, &key) == XG_INPUT_NONE);
    assert(xigua_input_release(&input, 0, 4100) == XG_INPUT_NONE);
    xigua_input_press(&input, 1, 5000, false);
    assert(xigua_input_release(&input, 1, 5600) == XG_INPUT_LONG);
    xigua_input_press(&input, 0, 6000, false);
    xigua_input_press(&input, 1, 6010, false);
    assert(xigua_input_release(&input, 1, 6020) == XG_INPUT_NONE);
    assert(xigua_input_release(&input, 0, 6030) == XG_INPUT_NONE);
    xigua_input_press(&input, 2, 10000, false);
    assert(xigua_input_tick(&input, 26000, &key) == XG_INPUT_NONE);
    assert(xigua_input_release(&input, 2, 26001) == XG_INPUT_NONE);
    xigua_input_press(&input, 2, 27000, false);
    assert(xigua_input_release(&input, 2, 27100) == XG_INPUT_CLICK);
    /* Delayed delivery: drain timestamped edges before polling current uptime. */
    xigua_input_press(&input, 2, 30000, false);
    assert(xigua_input_release(&input, 2, 30100) == XG_INPUT_CLICK);
    assert(xigua_input_tick(&input, 32000, &key) == XG_INPUT_NONE);
    /* Lost edges invalidate the gesture; a later orphan RELEASE is harmless. */
    xigua_input_press(&input, 0, 33000, false);
    xigua_input_init(&input);
    assert(xigua_input_tick(&input, 34000, &key) == XG_INPUT_NONE);
    assert(xigua_input_release(&input, 0, 34100) == XG_INPUT_NONE);
    puts("Xigua gesture tests: PASS");
    return 0;
}
