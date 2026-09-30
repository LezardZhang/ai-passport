#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Sentinel in the existing persisted end timestamp: no NVS layout change. */
#define XIGUA_SLEEP_RUNNING_END (-1LL)

static inline bool xigua_sleep_running(int64_t end_epoch)
{
    return end_epoch == XIGUA_SLEEP_RUNNING_END;
}

static inline bool xigua_sleep_duration(bool monotonic_known, int64_t start_us,
                                       int64_t now_us, int64_t start_epoch,
                                       int64_t now_epoch, uint16_t *minutes)
{
    int64_t elapsed;
    if (monotonic_known && now_us >= start_us) elapsed = (now_us - start_us) / 60000000;
    else if (start_epoch >= 1700000000 && now_epoch >= start_epoch) {
        elapsed = (now_epoch - start_epoch) / 60;
    } else { *minutes = 0; return false; }
    *minutes = elapsed > UINT16_MAX ? UINT16_MAX : (uint16_t)elapsed;
    return true;
}
