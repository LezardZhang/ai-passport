#ifndef XIGUA_NETWORK_H
#define XIGUA_NETWORK_H

#include "xigua_config.h"
#include "xigua_core.h"
#include <stdbool.h>
#include <stdint.h>

#define XG_WIFI_SCAN_MAX 8
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secured;
} xigua_wifi_scan_entry_t;
typedef struct {
    bool busy;
    uint8_t count;
    uint32_t generation;
    int error;
    xigua_wifi_scan_entry_t entries[XG_WIFI_SCAN_MAX];
} xigua_wifi_scan_status_t;

typedef struct {
    bool configured;       /* At least one enabled Wi-Fi profile. */
    bool connected;        /* STA has an IPv4 address. */
    bool time_synced;      /* SNTP completed in this boot. */
    int profile_index;     /* 0..7 while connected/connecting, -1 otherwise. */
    int last_disconnect_reason;
    uint32_t generation;   /* Increments when a new configuration is accepted. */
} xigua_network_status_t;

/* Initializes the default NVS partition without erasing it. This module owns
 * Wi-Fi STA and SNTP, and shares esp-netif/the default event loop. It keeps
 * Wi-Fi credentials in RAM; the caller persists xigua_config_t separately. */
bool xigua_network_start(const xigua_config_t *config);
/* Nonblocking, latest configuration wins. Safe from an ordinary application
 * task; never call from an ISR. The worker copies the complete configuration. */
bool xigua_network_configure(const xigua_config_t *config);
void xigua_network_status(xigua_network_status_t *out);
/* Short-lock request/snapshot. Driver scan runs only on the network worker. */
bool xigua_network_scan(void);
void xigua_network_scan_status(xigua_wifi_scan_status_t *out);
/* Absolute time is trusted only after SNTP has synchronized in this boot.
 * Monotonic time and boot ID are always supplied by the caller. */
xigua_clock_t xigua_network_clock(uint32_t boot_id, uint64_t monotonic_ms);

#endif
