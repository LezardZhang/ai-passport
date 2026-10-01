#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIGUA_CARE_MAGIC 0x58434131U
#define XIGUA_REMINDER_CAPACITY 8
#define XIGUA_REMINDER_TITLE_BYTES 96
#define XIGUA_CARE_MIN_EPOCH 1700000000LL

typedef struct {
    uint32_t id;
    int64_t due_epoch;
    char title[XIGUA_REMINDER_TITLE_BYTES];
} xigua_reminder_t;

/* A separate, versioned NVS key preserves the existing childcare record layout. */
typedef struct {
    uint32_t magic;
    uint32_t next_id;
    xigua_reminder_t items[XIGUA_REMINDER_CAPACITY];
} xigua_care_t;

void xigua_care_init(xigua_care_t *care);
bool xigua_care_valid(const xigua_care_t *care);
/* Exact local YYYY-MM-DD HH:MM or HH:MM. Calendar/DST normalization is rejected. */
bool xigua_care_time(const char *text, int64_t now, unsigned days_ago, int64_t *epoch);
bool xigua_care_add(xigua_care_t *care, const char *title, int64_t due, int64_t now,
                    uint32_t *id);
const xigua_reminder_t *xigua_care_due(const xigua_care_t *care, int64_t now,
                                     uint32_t dismissed_id);
bool xigua_care_finish(xigua_care_t *care, uint32_t id);
bool xigua_care_snooze(xigua_care_t *care, uint32_t id, int64_t now);
