#include "xigua_wifi_policy.h"
#include <limits.h>
#include <string.h>

void xigua_wifi_policy_init(xigua_wifi_policy_t *policy,
                            const xigua_wifi_profile_t profiles[XG_WIFI_PROFILES])
{
    if (!policy) return;
    memset(policy, 0, sizeof(*policy));
    policy->active = -1;
    policy->last_tried = -1;
    if (!profiles) return;
    for (int i = 0; i < XG_WIFI_PROFILES; ++i) {
        policy->enabled[i] = profiles[i].enabled && profiles[i].ssid[0];
        policy->priority[i] = profiles[i].priority;
    }
}

int xigua_wifi_policy_next(const xigua_wifi_policy_t *policy, uint64_t now_ms)
{
    if (!policy || policy->connected || policy->active >= 0) return -1;
    int best = -1;
    for (int step = 1; step <= XG_WIFI_PROFILES; ++step) {
        int i = (policy->last_tried + step) % XG_WIFI_PROFILES;
        if (!policy->enabled[i] || now_ms < policy->retry_at_ms[i]) continue;
        if (best < 0 || policy->priority[i] < policy->priority[best]) best = i;
    }
    return best;
}

void xigua_wifi_policy_begin(xigua_wifi_policy_t *policy, int index)
{
    if (!policy || index < 0 || index >= XG_WIFI_PROFILES || !policy->enabled[index]) return;
    policy->active = index;
    policy->last_tried = index;
    policy->connected = false;
}

void xigua_wifi_policy_connected(xigua_wifi_policy_t *policy, int index)
{
    if (!policy || index < 0 || index >= XG_WIFI_PROFILES || !policy->enabled[index]) return;
    policy->active = index;
    policy->last_tried = index;
    policy->connected = true;
    policy->failures[index] = 0;
    policy->retry_at_ms[index] = 0;
}

void xigua_wifi_policy_failed(xigua_wifi_policy_t *policy, int index,
                              xigua_wifi_failure_t reason, uint64_t now_ms)
{
    if (!policy || index < 0 || index >= XG_WIFI_PROFILES || !policy->enabled[index]) return;
    if (policy->failures[index] < 8) ++policy->failures[index];
    uint64_t base = reason == XG_WIFI_AUTH_FAILED ? 60000u :
                    reason == XG_WIFI_NO_AP ? 10000u : 2000u;
    uint64_t delay = base << (policy->failures[index] - 1);
    if (delay > 300000u) delay = 300000u;
    policy->retry_at_ms[index] = UINT64_MAX - now_ms < delay ? UINT64_MAX : now_ms + delay;
    if (policy->active == index) {
        policy->active = -1;
        policy->connected = false;
    }
}

uint32_t xigua_wifi_policy_wait(const xigua_wifi_policy_t *policy, uint64_t now_ms)
{
    if (!policy || policy->connected || policy->active >= 0) return UINT32_MAX;
    uint64_t shortest = UINT64_MAX;
    for (int i = 0; i < XG_WIFI_PROFILES; ++i) {
        if (!policy->enabled[i]) continue;
        if (policy->retry_at_ms[i] <= now_ms) return 0;
        uint64_t delay = policy->retry_at_ms[i] - now_ms;
        if (delay < shortest) shortest = delay;
    }
    return shortest > UINT32_MAX ? UINT32_MAX : (uint32_t)shortest;
}
