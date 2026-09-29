#ifndef XIGUA_SERVICE_H
#define XIGUA_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
#include "xigua_network.h"
typedef enum {
    XG_WIFI_CONFIG_IDLE, XG_WIFI_CONFIG_SAVED, XG_WIFI_CONFIG_INVALID,
    XG_WIFI_CONFIG_READ_ONLY, XG_WIFI_CONFIG_FULL, XG_WIFI_CONFIG_SAVE_FAILED,
    XG_WIFI_CONFIG_UNAVAILABLE
} xigua_wifi_config_result_t;
typedef struct {
    bool available, config_read_only, wifi_configured, wifi_connected;
    bool time_synced, timezone_configured, ai_configured, ai_busy;
    int ai_status, http_status, wifi_reason;
    char local_time[6];
    bool day_valid;
    int64_t day_begin_ms, day_end_ms;
    char local_date[11];
    bool wifi_scan_busy, wifi_config_busy;
    uint8_t wifi_scan_count;
    uint32_t wifi_scan_generation, wifi_config_generation;
    int wifi_scan_error;
    xigua_wifi_config_result_t wifi_config_result;
    xigua_wifi_scan_entry_t wifi_scan[XG_WIFI_SCAN_MAX];
    char wifi_ssid[33]; /* Active profile only; passwords are never published. */
} xigua_service_status_t;
/* Owns USB management and configuration NVS in a worker, never accesses UI. */
bool xigua_service_start(void);
void xigua_service_status(xigua_service_status_t *out);
bool xigua_service_wifi_scan(void);
/* Copies credentials to a bounded mailbox. Only explicit save persists them. */
bool xigua_service_wifi_save(const char *ssid, const char *password);
#endif
