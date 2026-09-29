#define _POSIX_C_SOURCE 200809L
#include "xigua_calendar.h"
#include <limits.h>
#include <time.h>

bool xigua_calendar_day(int64_t unix_seconds, int64_t *begin_ms, int64_t *end_ms)
{
    if (!begin_ms || !end_ms || begin_ms == end_ms || unix_seconds < 0 ||
        unix_seconds > INT64_MAX / 1000) return false;
    time_t epoch = (time_t)unix_seconds;
    if ((int64_t)epoch != unix_seconds) return false;
    struct tm local;
    if (!localtime_r(&epoch, &local)) return false;
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    struct tm next = local;
    ++next.tm_mday;
    /* Keep separate inputs: mktime may normalize a nonexistent midnight to 01:00. */
    time_t start = mktime(&local);
    time_t finish = mktime(&next);
    if (start == (time_t)-1 || finish == (time_t)-1) return false;
    int64_t begin = (int64_t)start, end = (int64_t)finish;
    if (begin < 0 || end <= begin || begin > unix_seconds || unix_seconds >= end ||
        end > INT64_MAX / 1000) return false;
    *begin_ms = begin * 1000;
    *end_ms = end * 1000;
    return true;
}
