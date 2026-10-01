#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIGUA_WIFI_SCAN_MAX 16

typedef enum {
    XIGUA_WIFI_OFF = 0,
    XIGUA_WIFI_STARTING,
    XIGUA_WIFI_READY,
    XIGUA_WIFI_CONNECTING,
    XIGUA_WIFI_CONNECTED,
    XIGUA_WIFI_FAILED,
} xigua_wifi_state_t;

esp_err_t xigua_wifi_start(void);
void xigua_wifi_stop(void);
xigua_wifi_state_t xigua_wifi_state(void);
void xigua_wifi_status(char *ssid, size_t ssid_size, char *ip, size_t ip_size);
void xigua_wifi_clear_credentials(void);
esp_err_t xigua_wifi_set_credentials(const char *ssid, const char *password);
esp_err_t xigua_wifi_connect(void);
esp_err_t xigua_wifi_scan(void);
bool xigua_wifi_scan_in_progress(void);
size_t xigua_wifi_scan_count(void);
const char *xigua_wifi_scan_ssid(size_t index);
int8_t xigua_wifi_scan_rssi(size_t index);
size_t xigua_wifi_builtin_count(void);
const char *xigua_wifi_builtin_ssid(size_t index);
esp_err_t xigua_wifi_connect_builtin(size_t index);
bool xigua_wifi_credentials_saved(void);
