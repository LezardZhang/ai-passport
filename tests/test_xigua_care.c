#define _POSIX_C_SOURCE 200809L
#include "xigua_care.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void)
{
    setenv("TZ", "CST-8", 1); tzset();
    int64_t now = 1790827200LL, at, yesterday, tomorrow;
    assert(xigua_care_time("2026-10-01 12:00", now, 0, &at));
    assert(at == now);
    assert(xigua_care_time("12:00", now, 1, &yesterday));
    assert(yesterday == now - 86400);
    assert(xigua_care_time("2026-10-02 12:00", now, 0, &tomorrow));
    assert(!xigua_care_time("2026-02-30 12:00", now, 0, &at));
    assert(!xigua_care_time("2026-10-01 24:00", now, 0, &at));
    assert(!xigua_care_time("12:00junk", now, 0, &at));
    assert(!xigua_care_time("+1:00", now, 0, &at));
    assert(!xigua_care_time("12:00", 0, 0, &at));
    assert(!xigua_care_time("2026-10-01 12:00", now, 1, &at));
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1); tzset();
    assert(!xigua_care_time("2026-03-08 02:30", now, 0, &at)); /* Nonexistent local time. */
    assert(xigua_care_time("2026-03-09 12:00", now, 0, &at));
    assert(xigua_care_time("12:00", at, 1, &yesterday));
    int64_t march8;
    assert(xigua_care_time("2026-03-08 12:00", now, 0, &march8));
    assert(yesterday == march8);
    xigua_care_t care; xigua_care_init(&care);
    assert(xigua_care_valid(&care));
    assert(!xigua_care_add(&care, "", now + 60, now, NULL));
    assert(!xigua_care_add(&care, "past", now, now, NULL));
    assert(!xigua_care_add(&care, "unknown clock", now + 60, 0, NULL));
    uint32_t id;
    assert(xigua_care_add(&care, "later", now + 120, now, &id) && id == 1);
    assert(xigua_care_add(&care, "earlier", now + 60, now, &id) && id == 2);
    assert(!xigua_care_due(&care, now, 0));
    assert(!xigua_care_due(&care, 0, 0));
    assert(xigua_care_due(&care, now + 120, 0)->id == 2);
    assert(xigua_care_due(&care, now + 120, 2)->id == 1);
    xigua_care_t restarted; memcpy(&restarted, &care, sizeof(care));
    assert(xigua_care_valid(&restarted));
    assert(xigua_care_due(&restarted, now + 120, 0)->id == 2);
    assert(xigua_care_snooze(&restarted, 2, now + 120));
    assert(xigua_care_due(&restarted, now + 120, 0)->id == 1);
    assert(xigua_care_finish(&restarted, 1));
    assert(!xigua_care_finish(&restarted, 1));
    assert(!xigua_care_due(&restarted, now + 719, 0));
    assert(xigua_care_due(&restarted, now + 720, 0)->id == 2);
    for (size_t i = 1; i < XIGUA_REMINDER_CAPACITY; ++i)
        assert(xigua_care_add(&restarted, "capacity", now + 60, now, NULL));
    assert(!xigua_care_add(&restarted, "overflow", tomorrow, now, NULL));
    restarted.items[0].title[0] = 0;
    assert(!xigua_care_valid(&restarted));
    puts("Care calendar, DST, reminder reboot, overdue, snooze and capacity: PASS");
}
