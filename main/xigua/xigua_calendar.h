#ifndef XIGUA_CALENDAR_H
#define XIGUA_CALENDAR_H

#include <stdbool.h>
#include <stdint.h>

/* Local calendar day containing unix_seconds, as a UTC millisecond range [begin,end).
 * The service owns process TZ/tzset and must serialize changes with this call.
 * Uses calendar midnight conversion, so DST days may be shorter/longer than 24 hours.
 * Returns false for unsupported times, pre-epoch boundaries, or overflow; outputs are
 * untouched on failure. Neither argument may be NULL, and outputs must be distinct. */
bool xigua_calendar_day(int64_t unix_seconds, int64_t *begin_ms, int64_t *end_ms);

#endif
