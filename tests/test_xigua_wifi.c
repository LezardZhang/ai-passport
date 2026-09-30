#include "xigua_wifi_test_stubs.h"
#include <assert.h>
#include <stdio.h>

#include "../main/xigua_wifi.c"

esp_err_t demo_radio_nvs_prepare(void) { return ESP_OK; }
esp_err_t demo_radio_network_prepare(void) { return ESP_OK; }

static void disconnect(unsigned reason)
{
    wifi_event_sta_disconnected_t event = { .reason = (uint8_t)reason };
    wifi_event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event);
}

int main(void)
{
    /* A built-in AP below the UI's first 16 results must still be connected. */
    test_ap_count = 20;
    for (unsigned i=0; i<test_ap_count; ++i)
        snprintf((char *)test_aps[i].ssid, sizeof(test_aps[i].ssid), "other-%u", i);
    snprintf((char *)test_aps[19].ssid, sizeof(test_aps[19].ssid), "%s", s_builtin_ssids[1]);
    s_wifi_started=true; s_auto_enabled=true; s_retry_timer=(void *)1;
    collect_scan_results();
    assert(s_scan_count == XIGUA_WIFI_SCAN_MAX);
    assert(test_ap_read == 20 && s_auto_builtin_seen[1]);
    auto_connect_after_scan();
    assert(test_connect_calls == 1);
    assert(strcmp((char *)test_config.sta.ssid, s_builtin_ssids[1]) == 0);
    assert(!test_config.sta.bssid_set && test_config.sta.channel == 0);
    for(unsigned i=0; i<3; ++i) {
        disconnect(4);
        assert(s_state == XIGUA_WIFI_CONNECTING);
        assert(s_connect_retries == i+1);
    }
    disconnect(4);
    assert(test_connect_calls == 4 && s_state == XIGUA_WIFI_READY);
    assert(test_timer_starts == 1); /* Retry budget exhausted: rescan later. */
    wifi_event(NULL,XIGUA_WIFI_RETRY_EVENT,0,NULL);
    assert(test_scan_calls == 1 && s_auto_scan_pending);
    s_auto_scan_pending=false;
    xigua_wifi_scan();
    unsigned scans=test_scan_calls;
    wifi_event(NULL,XIGUA_WIFI_RETRY_EVENT,0,NULL);
    assert(test_scan_calls == scans); /* Manual search cancels background takeover. */

    s_scan_pending=false; s_auto_enabled=true; s_wifi_got_ip=true;
    s_state=XIGUA_WIFI_CONNECTED; s_connect_retries=3;
    disconnect(WIFI_REASON_BEACON_TIMEOUT);
    assert(!s_wifi_got_ip && s_connect_retries == 1);
    assert(s_state == XIGUA_WIFI_CONNECTING);
    s_reconnect_after_disconnect=true;
    snprintf((char *)s_sta_config.sta.ssid,sizeof(s_sta_config.sta.ssid),"new-network");
    disconnect(8);
    assert(strcmp((char *)test_config.sta.ssid,"new-network") == 0);
    wifi_stop_internal();
    assert(!s_auto_enabled && s_retry_timer == NULL);
    puts("Xigua Wi-Fi complete-scan, association retry and reconnect: PASS");
    return 0;
}
