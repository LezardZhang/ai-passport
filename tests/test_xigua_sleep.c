#include "xigua_sleep.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    uint16_t minutes;
    assert(xigua_sleep_running(-1));
    assert(!xigua_sleep_running(0) && !xigua_sleep_running(1700000000));
    /* Page changes and an independent timer do not enter this duration formula. */
    assert(xigua_sleep_duration(true, 1000000, 7201000000, 0, 0, &minutes));
    assert(minutes == 120);
    assert(xigua_sleep_duration(false, 0, 0, 1700000000, 1700001800, &minutes));
    assert(minutes == 30); /* Trusted epoch restores timing after reboot. */
    assert(!xigua_sleep_duration(false, 0, 0, 0, 1700001800, &minutes));
    assert(minutes == 0);
    assert(!xigua_sleep_duration(false, 0, 0, 1700001800, 1700000000, &minutes));
    assert(xigua_sleep_duration(true, 0, 59 * 1000000LL, 0, 0, &minutes) && minutes == 0);
    assert(xigua_sleep_duration(true, 0, 70000LL * 60000000, 0, 0, &minutes));
    assert(minutes == UINT16_MAX);
    puts("Background sleep, reboot timing and duration bounds: PASS");
}
