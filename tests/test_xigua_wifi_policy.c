#include "xigua_wifi_policy.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static void profile(xigua_wifi_profile_t *p, const char *name, uint8_t priority)
{
    strcpy(p->ssid, name);
    p->enabled = true;
    p->priority = priority;
}

int main(void)
{
    xigua_wifi_profile_t profiles[XG_WIFI_PROFILES] = {0};
    profile(&profiles[0], "home", 0);
    profile(&profiles[1], "mobile", 1);
    profile(&profiles[2], "office", 0);
    xigua_wifi_policy_t policy;
    xigua_wifi_policy_init(&policy, profiles);

    assert(xigua_wifi_policy_next(&policy, 0) == 0);
    xigua_wifi_policy_begin(&policy, 0);
    assert(xigua_wifi_policy_next(&policy, 0) == -1);
    xigua_wifi_policy_failed(&policy, 0, XG_WIFI_AUTH_FAILED, 1000);
    assert(xigua_wifi_policy_next(&policy, 1000) == 2);
    assert(policy.retry_at_ms[0] == 61000);

    xigua_wifi_policy_begin(&policy, 2);
    xigua_wifi_policy_failed(&policy, 2, XG_WIFI_NO_AP, 1000);
    assert(xigua_wifi_policy_next(&policy, 1000) == 1);
    xigua_wifi_policy_begin(&policy, 1);
    xigua_wifi_policy_connected(&policy, 1);
    assert(xigua_wifi_policy_next(&policy, 1000000) == -1); /* stable IP, no roam */

    xigua_wifi_policy_failed(&policy, 1, XG_WIFI_TRANSIENT, 2000);
    assert(xigua_wifi_policy_wait(&policy, 2000) == 2000);
    assert(xigua_wifi_policy_next(&policy, 11000) == 2);
    xigua_wifi_policy_begin(&policy, 2);
    xigua_wifi_policy_failed(&policy, 2, XG_WIFI_NO_AP, 11000);
    assert(policy.retry_at_ms[2] == 31000);
    assert(xigua_wifi_policy_next(&policy, 11000) == 1);
    xigua_wifi_policy_begin(&policy, 1);
    xigua_wifi_policy_connected(&policy, 1);
    assert(policy.failures[1] == 0);

    /* Repeated errors cap at five minutes and never overflow the clock. */
    for (int i = 0; i < 8; ++i)
        xigua_wifi_policy_failed(&policy, 0, XG_WIFI_AUTH_FAILED, UINT64_MAX - 10);
    assert(policy.retry_at_ms[0] == UINT64_MAX);
    assert(xigua_wifi_policy_wait(&policy, UINT64_MAX - 10) == UINT32_MAX);

    xigua_wifi_policy_init(&policy, profiles);
    assert(xigua_wifi_policy_next(&policy, 0) == 0);
    memset(profiles, 0, sizeof(profiles));
    xigua_wifi_policy_init(&policy, profiles);
    assert(xigua_wifi_policy_next(&policy, 0) == -1);
    assert(xigua_wifi_policy_wait(&policy, 0) == UINT32_MAX);
    return 0;
}
