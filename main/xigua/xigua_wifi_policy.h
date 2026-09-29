#ifndef XIGUA_WIFI_POLICY_H
#define XIGUA_WIFI_POLICY_H

#include "xigua_config.h"
#include <stdbool.h>
#include <stdint.h>

/* Pure scheduling policy. Lower priority number is preferred. An established
 * connection is never displaced just because another profile has priority. */
typedef enum {
    XG_WIFI_TRANSIENT, /* link loss or driver error */
    XG_WIFI_NO_AP,
    XG_WIFI_AUTH_FAILED,
} xigua_wifi_failure_t;

typedef struct {
    bool enabled[XG_WIFI_PROFILES];
    uint8_t priority[XG_WIFI_PROFILES];
    uint8_t failures[XG_WIFI_PROFILES];
    uint64_t retry_at_ms[XG_WIFI_PROFILES];
    int active;
    int last_tried;
    bool connected;
} xigua_wifi_policy_t;

void xigua_wifi_policy_init(xigua_wifi_policy_t *policy,
                            const xigua_wifi_profile_t profiles[XG_WIFI_PROFILES]);
/* Select a due profile, or -1. Does not mutate the schedule; call begin once
 * the driver has accepted an attempt. */
int xigua_wifi_policy_next(const xigua_wifi_policy_t *policy, uint64_t now_ms);
void xigua_wifi_policy_begin(xigua_wifi_policy_t *policy, int index);
void xigua_wifi_policy_connected(xigua_wifi_policy_t *policy, int index);
void xigua_wifi_policy_failed(xigua_wifi_policy_t *policy, int index,
                              xigua_wifi_failure_t reason, uint64_t now_ms);
/* Milliseconds until another profile is due, UINT32_MAX if none. */
uint32_t xigua_wifi_policy_wait(const xigua_wifi_policy_t *policy, uint64_t now_ms);

#endif
