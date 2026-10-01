#include "xigua_care.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void xigua_care_init(xigua_care_t *care)
{
    memset(care, 0, sizeof(*care));
    care->magic = XIGUA_CARE_MAGIC;
    care->next_id = 1;
}

bool xigua_care_valid(const xigua_care_t *care)
{
    if (care->magic != XIGUA_CARE_MAGIC || !care->next_id) return false;
    for (size_t i = 0; i < XIGUA_REMINDER_CAPACITY; ++i) {
        const xigua_reminder_t *r = &care->items[i];
        if (!r->id) continue;
        if (r->id >= care->next_id || r->due_epoch < XIGUA_CARE_MIN_EPOCH ||
            !r->title[0] || !memchr(r->title, 0, sizeof(r->title))) return false;
        for (size_t j = 0; j < i; ++j) if (care->items[j].id == r->id) return false;
    }
    return true;
}

bool xigua_care_time(const char *text, int64_t now, unsigned days_ago, int64_t *epoch)
{
    if (!text || !epoch || now < XIGUA_CARE_MIN_EPOCH || days_ago > 365) return false;
    time_t raw = (time_t)now;
    struct tm date;
    if (!localtime_r(&raw, &date)) return false;
    int year, month, day, hour, minute, consumed = 0;
    size_t length = strlen(text);
    if (length != 5 && length != 16) return false;
    if ((length == 5 && text[2] != ':') || (length == 16 &&
        (text[4] != '-' || text[7] != '-' || text[10] != ' ' || text[13] != ':'))) return false;
    for (size_t i = 0; i < length; ++i) {
        if ((length == 5 && i == 2) || (length == 16 && (i == 4 || i == 7 || i == 10 || i == 13))) continue;
        if (text[i] < '0' || text[i] > '9') return false;
    }
    if (length == 16 && sscanf(text, "%4d-%2d-%2d %2d:%2d%n",
                               &year, &month, &day, &hour, &minute, &consumed) == 5 && consumed == 16) {
        if (days_ago || year < 2023 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31)
            return false;
        date.tm_year = year - 1900; date.tm_mon = month - 1; date.tm_mday = day;
    } else if (length == 5 && sscanf(text, "%2d:%2d%n", &hour, &minute, &consumed) == 2 && consumed == 5) {
        /* Calendar-day subtraction, including daylight-saving transitions. */
        date.tm_mday -= (int)days_ago;
        date.tm_hour = 12; date.tm_min = 0; date.tm_sec = 0; date.tm_isdst = -1;
        raw = mktime(&date);
        if (raw == (time_t)-1 || !localtime_r(&raw, &date)) return false;
    } else return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    date.tm_hour = hour; date.tm_min = minute; date.tm_sec = 0; date.tm_isdst = -1;
    struct tm wanted = date;
    raw = mktime(&date);
    if (raw == (time_t)-1 || date.tm_year != wanted.tm_year || date.tm_mon != wanted.tm_mon ||
        date.tm_mday != wanted.tm_mday || date.tm_hour != hour || date.tm_min != minute)
        return false;
    *epoch = (int64_t)raw;
    return *epoch >= XIGUA_CARE_MIN_EPOCH;
}

bool xigua_care_add(xigua_care_t *care, const char *title, int64_t due, int64_t now,
                    uint32_t *id)
{
    if (!title || !title[0] || strlen(title) >= XIGUA_REMINDER_TITLE_BYTES ||
        now < XIGUA_CARE_MIN_EPOCH || due <= now || due - now > 366LL * 86400 ||
        !care->next_id || care->next_id == UINT32_MAX) return false;
    for (size_t i = 0; i < XIGUA_REMINDER_CAPACITY; ++i) {
        if (care->items[i].id) continue;
        care->items[i] = (xigua_reminder_t){ .id = care->next_id++, .due_epoch = due };
        snprintf(care->items[i].title, sizeof(care->items[i].title), "%s", title);
        if (id) *id = care->items[i].id;
        return true;
    }
    return false;
}

const xigua_reminder_t *xigua_care_due(const xigua_care_t *care, int64_t now, uint32_t dismissed_id)
{
    if (now < XIGUA_CARE_MIN_EPOCH) return NULL;
    const xigua_reminder_t *first = NULL;
    for (size_t i = 0; i < XIGUA_REMINDER_CAPACITY; ++i) {
        const xigua_reminder_t *r = &care->items[i];
        if (r->id && r->id != dismissed_id && r->due_epoch <= now &&
            (!first || r->due_epoch < first->due_epoch)) first = r;
    }
    return first;
}

bool xigua_care_finish(xigua_care_t *care, uint32_t id)
{
    if (!id) return false;
    for (size_t i = 0; i < XIGUA_REMINDER_CAPACITY; ++i) {
        if (care->items[i].id == id) {
            memset(&care->items[i], 0, sizeof(care->items[i]));
            return true;
        }
    }
    return false;
}

bool xigua_care_snooze(xigua_care_t *care, uint32_t id, int64_t now)
{
    if (!id || now < XIGUA_CARE_MIN_EPOCH) return false;
    for (size_t i = 0; i < XIGUA_REMINDER_CAPACITY; ++i) {
        if (care->items[i].id == id) { care->items[i].due_epoch = now + 600; return true; }
    }
    return false;
}
