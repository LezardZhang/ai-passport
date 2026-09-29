#define _POSIX_C_SOURCE 200809L
#include "xigua_calendar.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void timezone_set(const char *tz)
{
#ifdef _WIN32
    assert(_putenv_s("TZ", tz) == 0);
    _tzset();
#else
    assert(setenv("TZ", tz, 1) == 0);
    tzset();
#endif
}
static void day(int64_t epoch, int64_t expected_begin, int64_t hours)
{
    int64_t begin = -1, end = -1;
    assert(xigua_calendar_day(epoch, &begin, &end));
    assert(begin == expected_begin * 1000);
    assert(end - begin == hours * 3600000);
    int64_t again_begin, again_end;
    assert(xigua_calendar_day(end / 1000 - 1, &again_begin, &again_end));
    assert(again_begin == begin && again_end == end);
    assert(xigua_calendar_day(end / 1000, &again_begin, &again_end));
    assert(again_begin == end);
}
int main(void)
{
    timezone_set("UTC0");
    day(0, 0, 24);
    day(1709251199, 1709164800, 24); /* Leap day: 2024-02-29 23:59:59. */
    day(1735689599, 1735603200, 24); /* 2024-12-31, then year rollover. */
    timezone_set("CST-8");
    day(1709222400, 1709222400, 24); /* 2024-03-01 midnight UTC+8. */
    timezone_set("NPT-5:45");
    day(1709251200, 1709230500, 24); /* Non-hour offset. */
    timezone_set("EST5EDT");
    day(1710072000, 1710046800, 23); /* 2024-03-10 US spring transition. */
    day(1730635200, 1730606400, 25); /* 2024-11-03 US autumn transition. */
#ifndef _WIN32
    /* POSIX rule strings are supported by target newlib, but not Windows CRT. */
    timezone_set("AEST-10AEDT,M10.1.0/2,M4.1.0/3");
    day(1728172800, 1728136800, 23); /* 2024-10-06 southern spring. */
    day(1712448000, 1712408400, 25); /* 2024-04-07 southern autumn. */
#endif
    int64_t begin = 17, end = 19;
    assert(!xigua_calendar_day(-1, &begin, &end));
    assert(!xigua_calendar_day(INT64_MAX, &begin, &end));
    assert(!xigua_calendar_day(1709251200, NULL, &end));
    assert(!xigua_calendar_day(1709251200, &begin, NULL));
    assert(!xigua_calendar_day(1709251200, &begin, &begin));
    timezone_set("CST-8");
    assert(!xigua_calendar_day(0, &begin, &end)); /* Local day starts before epoch. */
    assert(begin == 17 && end == 19);
    puts("xigua calendar tests passed");
    return 0;
}
