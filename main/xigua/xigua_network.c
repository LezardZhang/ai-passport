#include "xigua_network.h"
#include "xigua_wifi_policy.h"
#include "xigua_json.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdatomic.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "xigua_net";
enum { XG_CONNECT_TIMEOUT_MS = 20000, XG_STACK_RETRY_MS = 30000 };
typedef enum { NET_STARTED, NET_DISCONNECTED, NET_GOT_IP } net_event_kind_t;
typedef struct {
    net_event_kind_t kind;
    int reason;
    uint32_t generation;
    esp_netif_t *netif;
    uint8_t ssid[32], ssid_len;
} net_event_t;

static QueueHandle_t s_events, s_config_queue;
static atomic_bool s_started, s_event_overflow, s_time_synced;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static xigua_network_status_t s_status = {.profile_index = -1};
static xigua_config_t s_current_config, s_incoming_config;
static xigua_wifi_scan_status_t s_scan;
static atomic_bool s_scan_requested;

static uint64_t uptime_ms(void) { return (uint64_t)esp_timer_get_time() / 1000u; }

static void status_set(bool configured, bool connected, int profile, int reason,
                       bool new_generation)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.configured = configured;
    s_status.connected = connected;
    s_status.profile_index = profile;
    s_status.last_disconnect_reason = reason;
    if (new_generation) ++s_status.generation;
    portEXIT_CRITICAL(&s_status_lock);
}

static bool wifi_profiles_valid(const xigua_config_t *config)
{
    if (!config) return false;
    for (unsigned i = 0; i < XG_WIFI_PROFILES; ++i) {
        const xigua_wifi_profile_t *p = &config->wifi[i];
        size_t ssid_len = strnlen(p->ssid, sizeof(p->ssid));
        size_t password_len = strnlen(p->password, sizeof(p->password));
        if (ssid_len == sizeof(p->ssid) || password_len == sizeof(p->password) ||
            (p->enabled && (!ssid_len || (password_len && password_len < 8)))) return false;
    }
    return true;
}

static bool has_profiles(const xigua_config_t *config)
{
    for (unsigned i = 0; i < XG_WIFI_PROFILES; ++i)
        if (config->wifi[i].enabled && config->wifi[i].ssid[0]) return true;
    return false;
}

static void on_time_sync(struct timeval *tv)
{
    if (tv && tv->tv_sec >= 1609459200)
        atomic_store_explicit(&s_time_synced, true, memory_order_release);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const uint32_t *generation = arg;
    net_event_t event = {.generation = *generation};
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        event.kind = NET_STARTED;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = data;
        event.kind = NET_DISCONNECTED;
        if (disconnected) {
            event.reason = disconnected->reason;
            event.ssid_len = disconnected->ssid_len;
            if (event.ssid_len <= sizeof(event.ssid))
                memcpy(event.ssid, disconnected->ssid, event.ssid_len);
            else event.ssid_len = 0;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = data;
        event.kind = NET_GOT_IP;
        event.netif = got_ip ? got_ip->esp_netif : NULL;
    } else return;
    if (xQueueSend(s_events, &event, 0) != pdTRUE)
        atomic_store_explicit(&s_event_overflow, true, memory_order_relaxed);
}

typedef struct {
    esp_netif_t *netif;
    esp_event_handler_instance_t wifi_handler, ip_handler;
    bool wifi_registered, ip_registered, initialized, started;
    uint32_t generation;
} wifi_stack_t;

static void stack_stop(wifi_stack_t *stack)
{
    if (stack->started) esp_wifi_stop();
    if (stack->wifi_registered)
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, stack->wifi_handler);
    if (stack->ip_registered)
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, stack->ip_handler);
    if (stack->initialized) esp_wifi_deinit();
    if (stack->netif) esp_netif_destroy_default_wifi(stack->netif);
    *stack = (wifi_stack_t){0};
    xQueueReset(s_events); /* Discard events emitted by the retired station. */
    atomic_store_explicit(&s_event_overflow, false, memory_order_relaxed);
}

static esp_err_t stack_start(wifi_stack_t *stack, uint32_t generation)
{
    stack->generation = generation;
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();
    stack->netif = esp_netif_new(&netif_config);
    if (!stack->netif) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_netif_attach_wifi_station(stack->netif);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_default_wifi_sta_handlers();
    if (err != ESP_OK) return err;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    stack->initialized = true;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               on_wifi_event, &stack->generation, &stack->wifi_handler);
    if (err != ESP_OK) return err;
    stack->wifi_registered = true;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               on_wifi_event, &stack->generation, &stack->ip_handler);
    if (err != ESP_OK) return err;
    stack->ip_registered = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err == ESP_OK) stack->started = true;
    return err;
}

static xigua_wifi_failure_t classify(int reason)
{
    if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_HANDSHAKE_TIMEOUT)
        return XG_WIFI_AUTH_FAILED;
    if (reason == WIFI_REASON_NO_AP_FOUND ||
        reason == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY ||
        reason == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD ||
        reason == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD) return XG_WIFI_NO_AP;
    return XG_WIFI_TRANSIENT;
}

static void attempt(const xigua_config_t *config, xigua_wifi_policy_t *policy, uint64_t now,
                    uint64_t *started_ms)
{
    int index = xigua_wifi_policy_next(policy, now);
    if (index < 0) return;
    const xigua_wifi_profile_t *profile = &config->wifi[index];
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, profile->ssid, strlen(profile->ssid));
    memcpy(wifi.sta.password, profile->password, strlen(profile->password));
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "profile %d connection start failed: %s", index, esp_err_to_name(err));
        xigua_wifi_policy_failed(policy, index, XG_WIFI_TRANSIENT, now);
        status_set(true, false, -1, (int)err, false);
        return;
    }
    xigua_wifi_policy_begin(policy, index);
    *started_ms = now;
    status_set(true, false, index, 0, false);
}

static void scan_finish(int error)
{
    portENTER_CRITICAL(&s_status_lock);
    s_scan.error = error;
    s_scan.busy = false;
    ++s_scan.generation;
    portEXIT_CRITICAL(&s_status_lock);
}

static void scan_run(void)
{
    /* ESP32-C3 STA supports 2.4 GHz only. Blocking is limited to this worker;
     * event callbacks keep queueing and no application/UI lock is held. */
    wifi_scan_config_t config = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 40, .max = 120}};
    esp_err_t err = esp_wifi_scan_start(&config, true);
    static wifi_ap_record_t records[XG_WIFI_SCAN_MAX]; /* Single worker owner. */
    uint16_t count = XG_WIFI_SCAN_MAX;
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&count, records);
    esp_wifi_clear_ap_list();
    xigua_wifi_scan_status_t result = {0};
    if (err == ESP_OK) for (unsigned i = 0; i < count; ++i) {
        size_t n = strnlen((const char *)records[i].ssid, 33);
        if (!n || n > 32 || !xigua_utf8_valid((const char *)records[i].ssid, n)) continue;
        bool printable = true, duplicate = false;
        for (unsigned j = 0; j < n; ++j)
            if (records[i].ssid[j] < 32 || records[i].ssid[j] == 127) printable = false;
        for (unsigned j = 0; j < result.count; ++j)
            if (!strcmp(result.entries[j].ssid, (const char *)records[i].ssid)) duplicate = true;
        if (!printable || duplicate) continue;
        xigua_wifi_scan_entry_t *entry = &result.entries[result.count++];
        memcpy(entry->ssid, records[i].ssid, n + 1);
        entry->rssi = records[i].rssi;
        entry->secured = records[i].authmode != WIFI_AUTH_OPEN;
    }
    portENTER_CRITICAL(&s_status_lock);
    s_scan.count = result.count;
    memcpy(s_scan.entries, result.entries, sizeof(s_scan.entries));
    portEXIT_CRITICAL(&s_status_lock);
    scan_finish((int)err);
}

static void worker(void *context)
{
    (void)context;
    xigua_wifi_policy_t policy;
    xigua_wifi_policy_init(&policy, s_current_config.wifi);
    wifi_stack_t stack = {0};
    bool configured = false, disconnect_pending = false;
    bool station_ready = false;
    bool sntp_ready = false;
    bool scan_pending = false;
    uint64_t scan_requested_ms = 0;
    uint32_t stack_epoch = 0;
    uint64_t started_ms = 0, retry_stack_ms = 0, disconnect_ms = 0;
    esp_err_t err;

    for (;;) {
        if (atomic_exchange_explicit(&s_scan_requested, false, memory_order_acquire)) {
            scan_pending = true;
            scan_requested_ms = uptime_ms();
        }
        if (xQueueReceive(s_config_queue, &s_incoming_config, 0) == pdTRUE) {
            /* The one-element mailbox always contains the latest update. */
            /* Explicit configure is also a retry request after a failed join. */
            {
                stack_stop(&stack);
                s_current_config = s_incoming_config;
                xigua_wifi_policy_init(&policy, s_current_config.wifi);
                configured = has_profiles(&s_current_config);
                disconnect_pending = false;
                station_ready = false;
                retry_stack_ms = 0;
                status_set(configured, false, -1, 0, true);
            }
        }
        uint64_t now = uptime_ms();
        if ((configured || scan_pending) && !stack.started && now >= retry_stack_ms) {
            err = stack_start(&stack, ++stack_epoch);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Wi-Fi stack unavailable: %s", esp_err_to_name(err));
                stack_stop(&stack);
                retry_stack_ms = now + XG_STACK_RETRY_MS;
                if (scan_pending) { scan_finish((int)err); scan_pending = false; }
            }
        }
        if (atomic_exchange_explicit(&s_event_overflow, false, memory_order_relaxed)) {
            /* Lost link/IP events make state unreliable. Recreate the station. */
            stack_stop(&stack);
            xigua_wifi_policy_init(&policy, s_current_config.wifi);
            retry_stack_ms = now + 1000;
            disconnect_pending = false;
            station_ready = false;
            status_set(configured, false, -1, 0, false);
        }
        net_event_t event;
        if (xQueueReceive(s_events, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            now = uptime_ms();
            if (event.generation != stack.generation || !stack.started) continue;
            if (event.kind == NET_STARTED) {
                station_ready = true;
            } else if (event.kind == NET_DISCONNECTED) {
                int index = policy.active;
                if (index >= 0 && event.ssid_len &&
                    (strlen(s_current_config.wifi[index].ssid) != event.ssid_len ||
                     memcmp(s_current_config.wifi[index].ssid, event.ssid, event.ssid_len)))
                    continue;
                if (index >= 0) xigua_wifi_policy_failed(&policy, index,
                                                           classify(event.reason), now);
                disconnect_pending = false;
                status_set(configured, false, -1, event.reason, false);
            } else if (event.kind == NET_GOT_IP && event.netif == stack.netif &&
                       policy.active >= 0 && !disconnect_pending) {
                wifi_ap_record_t ap;
                const char *expected = s_current_config.wifi[policy.active].ssid;
                if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK ||
                    strncmp((const char *)ap.ssid, expected, sizeof(ap.ssid)) != 0) continue;
                xigua_wifi_policy_connected(&policy, policy.active);
                status_set(configured, true, policy.active, 0, false);
                if (!sntp_ready) {
                    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
                    sntp.sync_cb = on_time_sync;
                    err = esp_netif_sntp_init(&sntp);
                    sntp_ready = err == ESP_OK;
                    if (!sntp_ready)
                        ESP_LOGW(TAG, "SNTP unavailable: %s", esp_err_to_name(err));
                }
            }
        }
        now = uptime_ms();
        if (scan_pending && now - scan_requested_ms >= 25000) {
            scan_finish(ESP_ERR_TIMEOUT);
            scan_pending = false;
        }
        if (stack.started && policy.active >= 0 && !policy.connected && !disconnect_pending &&
            now - started_ms >= XG_CONNECT_TIMEOUT_MS) {
            disconnect_pending = true;
            disconnect_ms = now;
            status_set(configured, false, -1, 0, false);
            if (esp_wifi_disconnect() != ESP_OK) {
                xigua_wifi_policy_failed(&policy, policy.active, XG_WIFI_TRANSIENT, now);
                disconnect_pending = false;
            }
        }
        if (disconnect_pending && now - disconnect_ms >= 3000) {
            if (policy.active >= 0)
                xigua_wifi_policy_failed(&policy, policy.active, XG_WIFI_TRANSIENT, now);
            disconnect_pending = false;
            stack_stop(&stack); /* No disconnect confirmation: discard driver state. */
            station_ready = false;
            retry_stack_ms = now + 1000;
        }
        if (scan_pending && stack.started && station_ready && !disconnect_pending &&
            (policy.active < 0 || policy.connected)) {
            scan_run();
            scan_pending = false;
            if (!configured) { stack_stop(&stack); station_ready = false; }
        }
        if (!scan_pending && configured && stack.started && station_ready && !disconnect_pending)
            attempt(&s_current_config, &policy, now, &started_ms);
    }
}

bool xigua_network_start(const xigua_config_t *config)
{
    if (!wifi_profiles_valid(config) || atomic_load_explicit(&s_started, memory_order_acquire))
        return false;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS unavailable: %s; partition not erased", esp_err_to_name(err));
        return false;
    }
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp-netif unavailable: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "network prerequisites failed: %s; NVS not erased", esp_err_to_name(err));
        return false;
    }
    s_events = xQueueCreate(16, sizeof(net_event_t));
    s_config_queue = xQueueCreate(1, sizeof(xigua_config_t));
    if (!s_events || !s_config_queue) {
        if (s_events) vQueueDelete(s_events);
        if (s_config_queue) vQueueDelete(s_config_queue);
        s_events = s_config_queue = NULL;
        return false;
    }
    xQueueOverwrite(s_config_queue, config);
    if (xTaskCreate(worker, "xigua_net", 6144, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_events);
        vQueueDelete(s_config_queue);
        s_events = s_config_queue = NULL;
        return false;
    }
    atomic_store_explicit(&s_started, true, memory_order_release);
    return true;
}

bool xigua_network_configure(const xigua_config_t *config)
{
    if (!wifi_profiles_valid(config) ||
        !atomic_load_explicit(&s_started, memory_order_acquire)) return false;
    return xQueueOverwrite(s_config_queue, config) == pdTRUE;
}

void xigua_network_status(xigua_network_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_status_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    out->time_synced = atomic_load_explicit(&s_time_synced, memory_order_acquire);
}

bool xigua_network_scan(void)
{
    if (!atomic_load_explicit(&s_started, memory_order_acquire)) return false;
    portENTER_CRITICAL(&s_status_lock);
    bool accepted = !s_scan.busy;
    if (accepted) { s_scan.busy = true; s_scan.error = 0; }
    portEXIT_CRITICAL(&s_status_lock);
    if (accepted) atomic_store_explicit(&s_scan_requested, true, memory_order_release);
    return accepted;
}

void xigua_network_scan_status(xigua_wifi_scan_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_status_lock); *out = s_scan; portEXIT_CRITICAL(&s_status_lock);
}

xigua_clock_t xigua_network_clock(uint32_t boot_id, uint64_t monotonic_ms)
{
    xigua_clock_t clock = { .boot_id = boot_id, .monotonic_ms = monotonic_ms,
                             .quality = XIGUA_TIME_UNKNOWN };
    if (!atomic_load_explicit(&s_time_synced, memory_order_acquire)) return clock;
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0 || tv.tv_sec < 1609459200) return clock;
    clock.unix_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    clock.quality = XIGUA_TIME_TRUSTED;
    return clock;
}
