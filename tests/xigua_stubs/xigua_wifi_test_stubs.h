#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_ERR_INVALID_SIZE 4
#define ESP_EVENT_ANY_ID -1
#define ESP_EVENT_DEFINE_BASE(name) const char *name = #name
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef void (*test_event_cb_t)(void *, esp_event_base_t, int32_t, void *);
static const char *WIFI_EVENT = "wifi", *IP_EVENT = "ip";
enum { WIFI_EVENT_STA_START, WIFI_EVENT_SCAN_DONE, WIFI_EVENT_STA_DISCONNECTED,
       IP_EVENT_STA_GOT_IP };
enum { WIFI_REASON_AUTH_EXPIRE=2, WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY=4,
       WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT=15, WIFI_REASON_BEACON_TIMEOUT=200,
       WIFI_REASON_NO_AP_FOUND=201, WIFI_REASON_AUTH_FAIL=202,
       WIFI_REASON_ASSOC_FAIL=203, WIFI_REASON_HANDSHAKE_TIMEOUT=204,
       WIFI_REASON_CONNECTION_FAIL=205 };
enum { WIFI_IF_STA, WIFI_STORAGE_RAM, WIFI_STORAGE_FLASH, WIFI_MODE_STA,
       WIFI_PS_NONE, WIFI_AUTH_OPEN, WIFI_AUTH_WPA2_PSK,
       WIFI_SCAN_TYPE_ACTIVE, WIFI_ALL_CHANNEL_SCAN, WPA3_SAE_PWE_BOTH,
       ESP_SNTP_OPMODE_POLL };
typedef struct {
    struct {
        uint8_t ssid[32], password[64], bssid[6], channel;
        bool bssid_set;
        struct { bool capable, required; } pmf_cfg;
        struct { int authmode; } threshold;
        int scan_method, sae_pwe_h2e;
    } sta;
} wifi_config_t;
typedef struct { uint8_t ssid[33]; int8_t rssi; } wifi_ap_record_t;
typedef struct { bool show_hidden; int scan_type; } wifi_scan_config_t;
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct { uint8_t reason; } wifi_event_sta_disconnected_t;
typedef struct { struct { unsigned ip; } ip_info; } ip_event_got_ip_t;
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) ((void)(ip),192u),168u,1u,2u
typedef struct { int unused; } esp_netif_t;
typedef struct { void (*callback)(void *); const char *name; } esp_timer_create_args_t;
typedef void *esp_timer_handle_t;

static wifi_ap_record_t test_aps[64];
static uint16_t test_ap_count, test_ap_read;
static unsigned test_connect_calls, test_scan_calls, test_timer_starts;
static esp_err_t test_connect_result;
static wifi_config_t test_config;
static esp_netif_t test_netif;

static esp_err_t esp_wifi_scan_get_ap_num(uint16_t *n) { *n=test_ap_count; return ESP_OK; }
static esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *r) {
    if(test_ap_read>=test_ap_count) return ESP_ERR_INVALID_STATE;
    *r=test_aps[test_ap_read++]; return ESP_OK;
}
static esp_err_t esp_wifi_clear_ap_list(void) { return ESP_OK; }
static esp_err_t esp_wifi_connect(void) { ++test_connect_calls; return test_connect_result; }
static esp_err_t esp_wifi_set_config(int i,const wifi_config_t *c) { (void)i; test_config=*c; return ESP_OK; }
static esp_err_t esp_wifi_get_config(int i,wifi_config_t *c) { (void)i; *c=test_config; return ESP_OK; }
static esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *c,bool b) { (void)c;(void)b; ++test_scan_calls; return ESP_OK; }
static esp_err_t esp_wifi_set_storage(int s) { (void)s; return ESP_OK; }
static esp_err_t esp_wifi_set_mode(int s) { (void)s; return ESP_OK; }
static esp_err_t esp_wifi_set_ps(int s) { (void)s; return ESP_OK; }
static esp_err_t esp_wifi_scan_stop(void) { return ESP_OK; }
static esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }
static esp_err_t esp_wifi_start(void) { return ESP_OK; }
static esp_err_t esp_wifi_stop(void) { return ESP_OK; }
static esp_err_t esp_wifi_deinit(void) { return ESP_OK; }
static esp_err_t esp_wifi_init(const wifi_init_config_t *c) { (void)c; return ESP_OK; }
static esp_err_t esp_event_handler_instance_register(esp_event_base_t b,int id,test_event_cb_t cb,void *a,esp_event_handler_instance_t *h) {
    (void)b;(void)id;(void)cb;(void)a; *h=(void *)1; return ESP_OK;
}
static esp_err_t esp_event_handler_instance_unregister(esp_event_base_t b,int id,esp_event_handler_instance_t h) { (void)b;(void)id;(void)h;return ESP_OK; }
static esp_err_t esp_event_post(esp_event_base_t b,int id,const void *d,size_t n,unsigned wait) {
    (void)b;(void)id;(void)d;(void)n;(void)wait;return ESP_OK;
}
static esp_netif_t *esp_netif_create_default_wifi_sta(void) { return &test_netif; }
static void esp_netif_destroy_default_wifi(esp_netif_t *n) { (void)n; }
static esp_err_t esp_timer_create(const esp_timer_create_args_t *a,esp_timer_handle_t *h) { (void)a;*h=(void *)1;return ESP_OK; }
static esp_err_t esp_timer_start_once(esp_timer_handle_t h,uint64_t us) { (void)h;(void)us;++test_timer_starts;return ESP_OK; }
static esp_err_t esp_timer_stop(esp_timer_handle_t h) { (void)h;return ESP_OK; }
static esp_err_t esp_timer_delete(esp_timer_handle_t h) { (void)h;return ESP_OK; }
static void esp_sntp_setoperatingmode(int m) { (void)m; }
static void esp_sntp_setservername(int i,const char *n) { (void)i;(void)n; }
static void esp_sntp_init(void) {}
static void esp_sntp_stop(void) {}
static void esp_sntp_restart(void) {}
