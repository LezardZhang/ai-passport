#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    XIGUA_WIFI_OFF = 0,
    XIGUA_WIFI_STARTING,
    XIGUA_WIFI_ADVERTISING,
    XIGUA_WIFI_BLE_CONNECTED,
    XIGUA_WIFI_CONNECTING,
    XIGUA_WIFI_CONNECTED,
    XIGUA_WIFI_FAILED,
} xigua_wifi_state_t;

esp_err_t xigua_wifi_start(void);
void xigua_wifi_stop(void);
xigua_wifi_state_t xigua_wifi_state(void);
void xigua_wifi_status(char *ssid, size_t ssid_size, char *ip, size_t ip_size);
void xigua_wifi_clear_credentials(void);
const char *xigua_wifi_device_name(void);
