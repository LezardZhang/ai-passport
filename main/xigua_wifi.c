#include "xigua_wifi.h"
#include "xigua_wifi_radio.h"
#include "xigua_wifi_credentials.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "apps/esp_sntp.h"
#include "lwip/ip4_addr.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#ifndef XIGUA_WIFI_BUILTIN_COUNT
#define XIGUA_WIFI_BUILTIN_COUNT 0
#endif
#ifndef XIGUA_WIFI_BUILTIN_SSID_0
#define XIGUA_WIFI_BUILTIN_SSID_0 ""
#endif
#ifndef XIGUA_WIFI_BUILTIN_PASSWORD_0
#define XIGUA_WIFI_BUILTIN_PASSWORD_0 ""
#endif
#ifndef XIGUA_WIFI_BUILTIN_SSID_1
#define XIGUA_WIFI_BUILTIN_SSID_1 ""
#endif
#ifndef XIGUA_WIFI_BUILTIN_PASSWORD_1
#define XIGUA_WIFI_BUILTIN_PASSWORD_1 ""
#endif
#ifndef XIGUA_WIFI_BUILTIN_SSID_2
#define XIGUA_WIFI_BUILTIN_SSID_2 ""
#endif
#ifndef XIGUA_WIFI_BUILTIN_PASSWORD_2
#define XIGUA_WIFI_BUILTIN_PASSWORD_2 ""
#endif

static const char *TAG = "xigua_wifi";

static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static wifi_config_t s_sta_config;
static wifi_config_t s_saved_config;
static volatile xigua_wifi_state_t s_state = XIGUA_WIFI_OFF;
static volatile esp_err_t s_error;
static char s_ip[16];
static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_wifi_handler_registered;
static bool s_ip_handler_registered;
static bool s_sntp_started;
static bool s_wifi_got_ip;
static bool s_scan_pending;
static bool s_auto_scan_pending;
static bool s_auto_connecting;
static bool s_auto_saved_seen;
static bool s_auto_saved_tried;
static bool s_auto_builtin_seen[3];
static bool s_auto_builtin_tried[3];
static bool s_reconnect_after_disconnect;
static uint8_t s_connect_retries;
static size_t s_scan_count;
static wifi_ap_record_t s_scan_records[XIGUA_WIFI_SCAN_MAX];
static const char *const s_builtin_ssids[] = {
    XIGUA_WIFI_BUILTIN_SSID_0, XIGUA_WIFI_BUILTIN_SSID_1, XIGUA_WIFI_BUILTIN_SSID_2
};
static const char *const s_builtin_passwords[] = {
    XIGUA_WIFI_BUILTIN_PASSWORD_0, XIGUA_WIFI_BUILTIN_PASSWORD_1,
    XIGUA_WIFI_BUILTIN_PASSWORD_2
};

static void normalize_sta_config(wifi_config_t *config)
{
    if (!config) return;
    config->sta.bssid_set = false;
    memset(config->sta.bssid, 0, sizeof(config->sta.bssid));
    config->sta.pmf_cfg.capable = config->sta.password[0] != '\0';
    config->sta.pmf_cfg.required = false;
    config->sta.threshold.authmode = config->sta.password[0] != '\0'
        ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
}

static size_t builtin_index_for_ssid(const char *ssid)
{
    for (size_t i = 0; i < xigua_wifi_builtin_count(); ++i) {
        if (strcmp(ssid, s_builtin_ssids[i]) == 0) return i;
    }
    return SIZE_MAX;
}

static esp_err_t persist_connected_config(void)
{
    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &s_sta_config);
    esp_err_t ram_err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    return err != ESP_OK ? err : ram_err;
}

static void collect_scan_results(void)
{
    uint16_t count = XIGUA_WIFI_SCAN_MAX;
    memset(s_scan_records, 0, sizeof(s_scan_records));
    if (esp_wifi_scan_get_ap_records(&count, s_scan_records) != ESP_OK) count = 0;
    s_scan_count = count;
    ESP_LOGI(TAG, "Wi-Fi scan complete networks=%u", (unsigned)count);
}

static void auto_try_next_candidate(void)
{
    while (true) {
        size_t builtin = SIZE_MAX;
        for (size_t i = 0; i < xigua_wifi_builtin_count(); ++i) {
            if (s_auto_builtin_seen[i] && !s_auto_builtin_tried[i]) {
                builtin = i;
                break;
            }
        }
        if (builtin != SIZE_MAX) {
            s_auto_builtin_tried[builtin] = true;
            memset(&s_sta_config, 0, sizeof(s_sta_config));
            snprintf((char *)s_sta_config.sta.ssid, sizeof(s_sta_config.sta.ssid), "%s",
                     s_builtin_ssids[builtin]);
            snprintf((char *)s_sta_config.sta.password, sizeof(s_sta_config.sta.password), "%s",
                     s_builtin_passwords[builtin]);
            ESP_LOGI(TAG, "auto Wi-Fi trying built-in network %.32s", s_builtin_ssids[builtin]);
        } else if (s_auto_saved_seen && !s_auto_saved_tried) {
            s_auto_saved_tried = true;
            s_sta_config = s_saved_config;
            ESP_LOGI(TAG, "auto Wi-Fi trying saved network %.32s", s_sta_config.sta.ssid);
        } else {
            break;
        }
        normalize_sta_config(&s_sta_config);
        esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &s_sta_config);
        if (err == ESP_OK) err = esp_wifi_connect();
        if (err == ESP_OK) {
            s_auto_connecting = true;
            s_connect_retries = 0;
            s_state = XIGUA_WIFI_CONNECTING;
            return;
        }
        ESP_LOGW(TAG, "auto Wi-Fi candidate could not start: %s", esp_err_to_name(err));
    }
    s_auto_connecting = false;
    s_wifi_got_ip = false;
    s_ip[0] = '\0';
    s_state = XIGUA_WIFI_READY;
    ESP_LOGI(TAG, "no saved or built-in Wi-Fi available; waiting for Wi-Fi search");
}

static void auto_connect_after_scan(void)
{
    s_auto_saved_seen = false;
    s_auto_saved_tried = false;
    memset(s_auto_builtin_seen, 0, sizeof(s_auto_builtin_seen));
    memset(s_auto_builtin_tried, 0, sizeof(s_auto_builtin_tried));
    for (size_t i = 0; i < s_scan_count; ++i) {
        if (s_saved_config.sta.ssid[0] != '\0' &&
            strcmp((const char *)s_scan_records[i].ssid,
                   (const char *)s_saved_config.sta.ssid) == 0) {
            s_auto_saved_seen = true;
        }
        size_t builtin = builtin_index_for_ssid((const char *)s_scan_records[i].ssid);
        if (builtin != SIZE_MAX) s_auto_builtin_seen[builtin] = true;
    }
    auto_try_next_candidate();
}

static esp_err_t start_scan(bool automatic)
{
    wifi_scan_config_t config = { 0 };
    config.show_hidden = true;
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    s_scan_count = 0;
    s_scan_pending = !automatic;
    s_auto_scan_pending = automatic;
    if (automatic) s_state = XIGUA_WIFI_CONNECTING;
    esp_err_t err = esp_wifi_scan_start(&config, false);
    if (err != ESP_OK) {
        s_scan_pending = false;
        s_auto_scan_pending = false;
        s_state = XIGUA_WIFI_READY;
        ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t request_wifi_connect(void)
{
    bool was_connected = s_wifi_got_ip;
    normalize_sta_config(&s_sta_config);
    s_wifi_got_ip = false;
    s_connect_retries = 0;
    s_state = XIGUA_WIFI_CONNECTING;
    if (s_reconnect_after_disconnect) return ESP_OK;
    if (s_wifi_started && was_connected) {
        s_reconnect_after_disconnect = true;
        if (esp_wifi_disconnect() == ESP_OK) return ESP_OK;
        s_reconnect_after_disconnect = false;
    }
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &s_sta_config);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        s_error = err;
        s_state = XIGUA_WIFI_FAILED;
    }
    return err;
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        (void)start_scan(true);
        return;
    }
    if (id == WIFI_EVENT_SCAN_DONE) {
        if (s_auto_scan_pending) {
            s_auto_scan_pending = false;
            collect_scan_results();
            auto_connect_after_scan();
        } else if (s_scan_pending) {
            s_scan_pending = false;
            collect_scan_results();
            s_state = s_wifi_got_ip ? XIGUA_WIFI_CONNECTED : XIGUA_WIFI_READY;
        }
        return;
    }
    if (id != WIFI_EVENT_STA_DISCONNECTED) return;

    wifi_event_sta_disconnected_t *event = data;
    uint8_t reason = event ? event->reason : 0;
    ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%u", reason);
    if (s_reconnect_after_disconnect) {
        s_reconnect_after_disconnect = false;
        (void)esp_wifi_connect();
        return;
    }
    bool retryable = reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_AUTH_EXPIRE ||
                     reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                     reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_ASSOC_FAIL ||
                     reason == WIFI_REASON_NO_AP_FOUND;
    if ((s_auto_connecting || s_state == XIGUA_WIFI_CONNECTING) && retryable &&
        s_connect_retries < 3) {
        ++s_connect_retries;
        ESP_LOGW(TAG, "Wi-Fi retry %u/3", (unsigned)s_connect_retries);
        (void)esp_wifi_connect();
        return;
    }
    if (s_auto_connecting) {
        auto_try_next_candidate();
        return;
    }
    s_wifi_got_ip = false;
    s_ip[0] = '\0';
    s_state = XIGUA_WIFI_READY;
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;
    ip_event_got_ip_t *event = data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
    s_wifi_got_ip = true;
    s_auto_connecting = false;
    s_state = XIGUA_WIFI_CONNECTED;
    esp_err_t persist_err = persist_connected_config();
    if (persist_err == ESP_OK) s_saved_config = s_sta_config;
    else ESP_LOGW(TAG, "connected Wi-Fi could not be saved: %s", esp_err_to_name(persist_err));
    if (!s_sntp_started) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        s_sntp_started = true;
    } else {
        esp_sntp_restart();
    }
    ESP_LOGI(TAG, "Wi-Fi connected ssid=%.32s ip=%s", s_sta_config.sta.ssid, s_ip);
}

static esp_err_t wifi_start(void)
{
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = demo_radio_network_prepare();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_ERR_NO_MEM;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err != ESP_OK) return err;
    s_wifi_initialized = true;
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event, NULL, &s_wifi_handler);
    if (err != ESP_OK) return err;
    s_wifi_handler_registered = true;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              ip_event, NULL, &s_ip_handler);
    if (err != ESP_OK) return err;
    s_ip_handler_registered = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_get_config(WIFI_IF_STA, &s_saved_config);
    if (err == ESP_OK) {
        normalize_sta_config(&s_saved_config);
        s_sta_config = s_saved_config;
    }
    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) s_wifi_started = true;
    return err;
}

static void wifi_stop_internal(void)
{
    s_scan_pending = false;
    s_auto_scan_pending = false;
    s_auto_connecting = false;
    if (s_wifi_started) {
        (void)esp_wifi_scan_stop();
        (void)esp_wifi_disconnect();
        (void)esp_wifi_stop();
        s_wifi_started = false;
    }
    if (s_sntp_started) {
        esp_sntp_stop();
        s_sntp_started = false;
    }
    if (s_ip_handler_registered) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        s_ip_handler_registered = false;
    }
    if (s_wifi_handler_registered) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        s_wifi_handler_registered = false;
    }
    if (s_wifi_initialized) {
        esp_wifi_deinit();
        s_wifi_initialized = false;
    }
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    s_wifi_got_ip = false;
    s_state = XIGUA_WIFI_OFF;
}

esp_err_t xigua_wifi_start(void)
{
    if (s_state != XIGUA_WIFI_OFF) return ESP_ERR_INVALID_STATE;
    memset(s_ip, 0, sizeof(s_ip));
    s_state = XIGUA_WIFI_STARTING;
    esp_err_t err = wifi_start();
    if (err != ESP_OK) {
        s_error = err;
        s_state = XIGUA_WIFI_FAILED;
        wifi_stop_internal();
        s_state = XIGUA_WIFI_FAILED;
        ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(err));
    }
    return err;
}

void xigua_wifi_stop(void)
{
    wifi_stop_internal();
}

xigua_wifi_state_t xigua_wifi_state(void)
{
    return s_state;
}

void xigua_wifi_status(char *ssid, size_t ssid_size, char *ip, size_t ip_size)
{
    if (ssid && ssid_size) snprintf(ssid, ssid_size, "%.32s", s_sta_config.sta.ssid);
    if (ip && ip_size) snprintf(ip, ip_size, "%.15s", s_ip);
}

void xigua_wifi_clear_credentials(void)
{
    if (!s_wifi_started) return;
    wifi_config_t empty = { 0 };
    s_reconnect_after_disconnect = false;
    s_auto_connecting = false;
    (void)esp_wifi_disconnect();
    (void)esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &empty);
    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) {
        memset(&s_sta_config, 0, sizeof(s_sta_config));
        memset(&s_saved_config, 0, sizeof(s_saved_config));
        s_wifi_got_ip = false;
        s_ip[0] = '\0';
        s_state = XIGUA_WIFI_READY;
    }
}

esp_err_t xigua_wifi_set_credentials(const char *ssid, const char *password)
{
    if (!s_wifi_started || !ssid || !password) return ESP_ERR_INVALID_ARG;
    size_t ssid_len = strnlen(ssid, sizeof(s_sta_config.sta.ssid));
    size_t password_len = strnlen(password, sizeof(s_sta_config.sta.password));
    if (ssid_len == 0 || ssid_len >= sizeof(s_sta_config.sta.ssid) ||
        password_len >= sizeof(s_sta_config.sta.password)) return ESP_ERR_INVALID_SIZE;
    wifi_config_t config = { 0 };
    memcpy(config.sta.ssid, ssid, ssid_len);
    memcpy(config.sta.password, password, password_len);
    normalize_sta_config(&config);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) s_sta_config = config;
    return err;
}

esp_err_t xigua_wifi_connect(void)
{
    if (!s_wifi_started) return ESP_ERR_INVALID_STATE;
    return request_wifi_connect();
}

esp_err_t xigua_wifi_scan(void)
{
    if (!s_wifi_started) return ESP_ERR_INVALID_STATE;
    if (s_scan_pending) return ESP_ERR_INVALID_STATE;
    if (s_auto_scan_pending) {
        (void)esp_wifi_scan_stop();
        s_auto_scan_pending = false;
    }
    return start_scan(false);
}

bool xigua_wifi_scan_in_progress(void)
{
    return s_scan_pending || s_auto_scan_pending;
}

size_t xigua_wifi_scan_count(void)
{
    return s_scan_count;
}

const char *xigua_wifi_scan_ssid(size_t index)
{
    return index < s_scan_count ? (const char *)s_scan_records[index].ssid : "";
}

int8_t xigua_wifi_scan_rssi(size_t index)
{
    return index < s_scan_count ? s_scan_records[index].rssi : -127;
}

size_t xigua_wifi_builtin_count(void)
{
    return XIGUA_WIFI_BUILTIN_COUNT > 3 ? 3 : XIGUA_WIFI_BUILTIN_COUNT;
}

const char *xigua_wifi_builtin_ssid(size_t index)
{
    return index < xigua_wifi_builtin_count() ? s_builtin_ssids[index] : "";
}

esp_err_t xigua_wifi_connect_builtin(size_t index)
{
    if (index >= xigua_wifi_builtin_count()) return ESP_ERR_INVALID_ARG;
    esp_err_t err = xigua_wifi_set_credentials(s_builtin_ssids[index], s_builtin_passwords[index]);
    if (err != ESP_OK) return err;
    return xigua_wifi_connect();
}
