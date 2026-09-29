#include "xigua_service.h"
#include "xigua_calendar.h"
#include "xigua_app.h"
#include "xigua_management.h"
#include "xigua_store.h"
#include "xigua_network.h"
#include "xigua_ai.h"
#include "xigua_audio.h"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

static xigua_config_t s_config, s_candidate;
static xigua_management_request_t s_request;
static xigua_service_status_t s_status = {.ai_status = -1};
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static xigua_store_t s_store;
static nvs_handle_t s_nvs;
static bool s_writable, s_network_started;
static char s_payload[XG_CONFIG_WIRE_MAX];
static uint8_t s_scratch[XG_CONFIG_WIRE_MAX + XG_STORE_HEADER];
static char s_line[XG_COMMAND_MAX + 5];
static xigua_ai_result_t s_ai_result;
static bool s_wifi_pending, s_wifi_busy;
static xigua_wifi_profile_t s_wifi_request;
static xigua_wifi_config_result_t s_wifi_result;
static uint32_t s_wifi_generation;

bool xigua_service_wifi_scan(void)
{
    return xigua_network_scan();
}

bool xigua_service_wifi_save(const char *ssid, const char *password)
{
    if (!ssid || !password) return false;
    size_t sn = strnlen(ssid, 33), pn = strnlen(password, 65);
    if (!sn || sn > 32 || pn > 64) return false;
    portENTER_CRITICAL(&s_lock);
    bool accepted = s_status.available && !s_wifi_busy;
    if (accepted) {
        memset(&s_wifi_request, 0, sizeof(s_wifi_request));
        memcpy(s_wifi_request.ssid, ssid, sn + 1);
        memcpy(s_wifi_request.password, password, pn + 1);
        s_wifi_pending = s_wifi_busy = true;
        s_wifi_result = XG_WIFI_CONFIG_IDLE;
    }
    portEXIT_CRITICAL(&s_lock);
    return accepted;
}

void xigua_service_status(xigua_service_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    out->wifi_config_busy = s_wifi_busy;
    out->wifi_config_result = s_wifi_result;
    out->wifi_config_generation = s_wifi_generation;
    portEXIT_CRITICAL(&s_lock);
    xigua_wifi_scan_status_t scan; xigua_network_scan_status(&scan);
    out->wifi_scan_busy = scan.busy;
    out->wifi_scan_count = scan.count;
    out->wifi_scan_generation = scan.generation;
    out->wifi_scan_error = scan.error;
    memcpy(out->wifi_scan, scan.entries, sizeof(out->wifi_scan));
}
static void publish(const xigua_service_status_t *status)
{
    portENTER_CRITICAL(&s_lock); s_status = *status; portEXIT_CRITICAL(&s_lock);
}
static int read_slot(void *ctx, unsigned slot, uint8_t *data, size_t cap, size_t *n)
{
    (void)ctx; size_t needed = 0;
    const char *key = slot ? "cfg_b" : "cfg_a";
    esp_err_t e = nvs_get_blob(s_nvs, key, NULL, &needed);
    if (e == ESP_ERR_NVS_NOT_FOUND) return 1;
    if (e != ESP_OK || needed > cap) return -1;
    *n = needed; return nvs_get_blob(s_nvs, key, data, n) == ESP_OK ? 0 : -1;
}
static bool write_slot(void *ctx, unsigned slot, const uint8_t *data, size_t n)
{
    (void)ctx;
    return nvs_set_blob(s_nvs, slot ? "cfg_b" : "cfg_a", data, n) == ESP_OK && nvs_commit(s_nvs) == ESP_OK;
}
static const xigua_store_backend_t s_backend = {NULL, read_slot, write_slot};
static bool valid(const uint8_t *data, size_t n, void *ctx)
{
    (void)ctx; return xigua_config_decode(&s_candidate, (const char *)data, n);
}
static void load(void)
{
    xigua_config_defaults(&s_config);
    if (nvs_flash_init_partition("xigua_data") != ESP_OK ||
        nvs_open_from_partition("xigua_data", "settings", NVS_READWRITE, &s_nvs) != ESP_OK) return;
    size_t n;
    xigua_store_result_t r = xigua_store_load(&s_store, &s_backend, (uint8_t *)s_payload, sizeof(s_payload),
        &n, s_scratch, sizeof(s_scratch), valid, NULL);
    if (r == XG_STORE_ERROR) return;
    if (r != XG_STORE_EMPTY && !xigua_config_decode(&s_config, s_payload, n)) return;
    s_writable = r != XG_STORE_RECOVERED;
    memset(s_payload, 0, sizeof(s_payload)); memset(s_scratch, 0, sizeof(s_scratch));
}
static bool save(void)
{
    size_t n = xigua_config_encode(&s_candidate, s_payload, sizeof(s_payload));
    bool ok = s_writable && n && xigua_store_save(&s_store, &s_backend, (const uint8_t *)s_payload,
        n, s_scratch, sizeof(s_scratch));
    if (ok) s_config = s_candidate;
    else s_writable = false;
    memset(s_candidate.ai.key, 0, sizeof(s_candidate.ai.key));
    memset(s_payload, 0, sizeof(s_payload)); memset(s_scratch, 0, sizeof(s_scratch));
    return ok;
}
static void timezone(void)
{
    /* Only this task converts civil time. Empty TZ is explicitly unconfigured. */
    setenv("TZ", s_config.timezone[0] ? s_config.timezone : "UTC0", 1); tzset();
}
static void update_status(void)
{
    xigua_service_status_t s; xigua_service_status(&s);
    xigua_network_status_t n; xigua_network_status(&n);
    s.config_read_only = !s_writable; s.wifi_configured = n.configured;
    s.wifi_connected = n.connected; s.wifi_reason = n.last_disconnect_reason;
    s.wifi_ssid[0] = '\0';
    if (n.profile_index >= 0 && n.profile_index < XG_WIFI_PROFILES)
        memcpy(s.wifi_ssid, s_config.wifi[n.profile_index].ssid, sizeof(s.wifi_ssid));
    s.time_synced = n.time_synced; s.timezone_configured = s_config.timezone[0] != 0;
    s.ai_configured = s_config.ai.key[0] != 0;
    strcpy(s.local_time, "--:--");
    s.day_valid = false;
    s.day_begin_ms = s.day_end_ms = 0;
    s.local_date[0] = '\0';
    if (s.time_synced && s.timezone_configured) {
        time_t now = time(NULL); struct tm local;
        if (localtime_r(&now, &local)) {
            strftime(s.local_time, sizeof(s.local_time), "%H:%M", &local);
            if (strftime(s.local_date, sizeof(s.local_date), "%Y-%m-%d", &local))
                s.day_valid = xigua_calendar_day((int64_t)now, &s.day_begin_ms, &s.day_end_ms);
        }
    }
    publish(&s);
}
static void reply(int id, const char *error, cJSON *data)
{
    cJSON *r = cJSON_CreateObject();
    if (!r) { cJSON_Delete(data); return; }
    bool ok = cJSON_AddNumberToObject(r, "id", id) && cJSON_AddBoolToObject(r, "ok", error == NULL);
    if (error) ok = ok && cJSON_AddStringToObject(r, "error", error);
    if (data && !cJSON_AddItemToObject(r, "data", data)) { cJSON_Delete(data); ok = false; }
    char *out = ok ? cJSON_PrintUnformatted(r) : NULL;
    if (out) { flockfile(stdout); printf("\nXG1 %s\n", out); fflush(stdout); funlockfile(stdout); cJSON_free(out); }
    cJSON_Delete(r);
}
static void status_reply(int id)
{
    update_status(); xigua_service_status_t s; xigua_service_status(&s);
    xigua_audio_status_t audio; xigua_audio_get_status(&audio);
    cJSON *d = cJSON_CreateObject(); if (!d) { reply(id, "no_memory", NULL); return; }
    bool ok = cJSON_AddBoolToObject(d, "config_read_only", s.config_read_only) &&
        cJSON_AddBoolToObject(d, "wifi_configured", s.wifi_configured) &&
        cJSON_AddBoolToObject(d, "wifi_connected", s.wifi_connected) &&
        cJSON_AddNumberToObject(d, "wifi_reason", s.wifi_reason) &&
        cJSON_AddBoolToObject(d, "time_synced", s.time_synced) &&
        cJSON_AddBoolToObject(d, "timezone_configured", s.timezone_configured) &&
        cJSON_AddStringToObject(d, "local_time", s.local_time) &&
        cJSON_AddBoolToObject(d, "day_valid", s.day_valid) &&
        cJSON_AddStringToObject(d, "local_date", s.local_date) &&
        cJSON_AddBoolToObject(d, "ai_configured", s.ai_configured) &&
        cJSON_AddNumberToObject(d, "ai_status", s.ai_status) &&
        cJSON_AddNumberToObject(d, "http_status", s.http_status) &&
        cJSON_AddBoolToObject(d, "audio_available", audio.available) &&
        cJSON_AddBoolToObject(d, "audio_playing", audio.playing) &&
        cJSON_AddBoolToObject(d, "audio_alerting", audio.alerting) &&
        cJSON_AddNumberToObject(d, "audio_track", audio.track) &&
        cJSON_AddNumberToObject(d, "audio_volume", audio.volume) &&
        cJSON_AddNumberToObject(d, "audio_error", audio.error) &&
        cJSON_AddNumberToObject(d, "audio_max_feed_gap_ms", audio.max_feed_gap_ms) &&
        cJSON_AddNumberToObject(d, "audio_stack_min_bytes", audio.stack_min_bytes) &&
        cJSON_AddNumberToObject(d, "free_heap", esp_get_free_heap_size()) &&
        cJSON_AddNumberToObject(d, "minimum_free_heap", esp_get_minimum_free_heap_size()) &&
        cJSON_AddNumberToObject(d, "largest_free_block", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (ok) reply(id, NULL, d); else { cJSON_Delete(d); reply(id, "no_memory", NULL); }
}
static bool add_u64(cJSON *j, const char *key, uint64_t value)
{
    char text[24];
    snprintf(text, sizeof(text), "%" PRIu64, value);
    return cJSON_AddStringToObject(j, key, text) != NULL;
}
static bool add_i64(cJSON *j, const char *key, int64_t value)
{
    char text[24];
    snprintf(text, sizeof(text), "%" PRId64, value);
    return cJSON_AddStringToObject(j, key, text) != NULL;
}
static const char *time_quality(xigua_time_quality_t quality)
{
    static const char *const names[] = {"unknown", "trusted", "reconstructed"};
    return (unsigned)quality < 3 ? names[quality] : "unknown";
}
static const char *duration_quality(xigua_duration_quality_t quality)
{
    static const char *const names[] = {"unknown", "monotonic", "utc"};
    return (unsigned)quality < 3 ? names[quality] : "unknown";
}
static const char *event_type(xigua_event_type_t type)
{
    static const char *const names[] = {"", "feed", "sleep", "diaper", "bath", "tummy"};
    return (unsigned)type < 6 ? names[type] : "";
}
static bool add_clock(cJSON *record, const char *key, const xigua_clock_t *clock)
{
    cJSON *j = cJSON_AddObjectToObject(record, key);
    return j && cJSON_AddNumberToObject(j, "boot_id", clock->boot_id) &&
        add_u64(j, "monotonic_ms", clock->monotonic_ms) &&
        add_i64(j, "unix_ms", clock->unix_ms) &&
        cJSON_AddStringToObject(j, "quality", time_quality(clock->quality));
}
static bool add_record(cJSON *data, const xigua_event_t *event)
{
    cJSON *j = cJSON_AddObjectToObject(data, "record");
    return j && cJSON_AddNumberToObject(j, "id", event->id) &&
        cJSON_AddNumberToObject(j, "revision", event->revision) &&
        cJSON_AddStringToObject(j, "type", event_type(event->type)) &&
        cJSON_AddNumberToObject(j, "value", event->value) &&
        cJSON_AddBoolToObject(j, "active", event->active) &&
        add_clock(j, "start", &event->start) && add_clock(j, "end", &event->end) &&
        add_u64(j, "duration_ms", event->duration_ms) &&
        cJSON_AddStringToObject(j, "duration_quality", duration_quality(event->duration_quality));
}
static void record_reply(int id, const xigua_management_request_t *request)
{
    xigua_export_request_t read = {
        .op = request->op == XG_M_RECORDS_BEGIN ? XG_EXPORT_BEGIN :
            request->op == XG_M_RECORDS_ITEM ? XG_EXPORT_ITEM : XG_EXPORT_FINISH,
        .boot_id = request->boot_id, .revision = request->revision,
        .count = request->count, .index = request->index,
    };
    xigua_export_result_t result;
    xigua_export_status_t status;
    if (!xigua_app_export(&read, &status, &result)) { reply(id, "controller_busy", NULL); return; }
    if (status != XG_EXPORT_OK) {
        reply(id, status == XG_EXPORT_STALE ? "snapshot_changed" :
            status == XG_EXPORT_UNAVAILABLE ? "records_unavailable" : "invalid_range", NULL);
        return;
    }
    cJSON *data = cJSON_CreateObject();
    bool ok = data && cJSON_AddStringToObject(data, "format", "xigua-records-v1") &&
        cJSON_AddNumberToObject(data, "boot_id", result.boot_id) &&
        cJSON_AddNumberToObject(data, "revision", result.revision) &&
        cJSON_AddNumberToObject(data, "count", result.count) &&
        cJSON_AddNumberToObject(data, "index", result.index);
    if (ok && result.has_event) ok = add_record(data, &result.event);
    if (ok) reply(id, NULL, data);
    else { cJSON_Delete(data); reply(id, "no_memory", NULL); }
}
static void handle(const char *json, size_t n)
{
    memset(&s_request, 0, sizeof(s_request));
    if (!xigua_management_parse(json, n, &s_request)) { reply(0, "invalid_request", NULL); return; }
    const int id = s_request.id;
    if (s_request.op == XG_M_STATUS) { status_reply(id); return; }
    if (s_request.op == XG_M_RECORDS_BEGIN || s_request.op == XG_M_RECORDS_ITEM ||
        s_request.op == XG_M_RECORDS_FINISH) { record_reply(id, &s_request); return; }
    if (s_request.op == XG_M_AI_ASK || s_request.op == XG_M_AI_TEST) {
        xigua_service_status_t s; update_status(); xigua_service_status(&s);
        const char *error = !s.ai_configured ? "ai_not_configured" :
            !s.wifi_connected ? "offline" : !s.time_synced ? "time_not_synced" : NULL;
        if (error) { reply(id, error, NULL); return; }
        /* Fail before TLS under memory pressure, leaving local recording usable. */
        if (esp_get_free_heap_size() < 65536 || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 32768) {
            reply(id, "low_memory", NULL); return;
        }
        s.ai_busy = true; publish(&s);
        bool success = xigua_ai_request(&s_config.ai,
            s_request.op == XG_M_AI_TEST ? "Reply OK only." : s_request.text, &s_ai_result);
        s.ai_busy = false; s.ai_status = s_ai_result.status; s.http_status = s_ai_result.http_status; publish(&s);
        cJSON *d = cJSON_CreateObject();
        bool ok = d && cJSON_AddNumberToObject(d, "ai_status", s.ai_status) && cJSON_AddNumberToObject(d, "http_status", s.http_status);
        if (success) ok = ok && cJSON_AddStringToObject(d, "answer", s_ai_result.answer) && cJSON_AddBoolToObject(d, "truncated", s_ai_result.truncated);
        if (ok) reply(id, success ? NULL : "ai_failed", d);
        else { cJSON_Delete(d); reply(id, "no_memory", NULL); }
        memset(&s_ai_result, 0, sizeof(s_ai_result)); return;
    }
    if (!s_writable) { reply(id, "config_read_only", NULL); return; }
    s_candidate = s_config;
    if (!xigua_management_apply(&s_request, &s_candidate)) { reply(id, "invalid_config", NULL); return; }
    if (!save()) { reply(id, "save_failed", NULL); update_status(); return; }
    bool applied = true;
    if (s_request.op == XG_M_WIFI_SET || s_request.op == XG_M_WIFI_CLEAR)
        applied = s_network_started && xigua_network_configure(&s_config);
    if (s_request.op == XG_M_TIME_SET) timezone();
    if (s_request.op == XG_M_AI_SET || s_request.op == XG_M_AI_CLEAR) {
        xigua_service_status_t s; xigua_service_status(&s);
        s.ai_status = -1; s.http_status = 0; publish(&s);
    }
    update_status();
    cJSON *d = cJSON_CreateObject();
    if (d && cJSON_AddBoolToObject(d, "saved", true) && cJSON_AddBoolToObject(d, "reboot_required", !applied)) reply(id, NULL, d);
    else { cJSON_Delete(d); reply(id, "reply_allocation_failed", NULL); }
}
static void worker(void *unused)
{
    (void)unused; load(); timezone();
    if (nvs_flash_init() == ESP_OK) s_network_started = xigua_network_start(&s_config);
    usb_serial_jtag_driver_config_t usb = {.rx_buffer_size = 4096, .tx_buffer_size = 2048};
    if (usb_serial_jtag_driver_install(&usb) != ESP_OK) { vTaskDelete(NULL); return; }
    usb_serial_jtag_vfs_use_driver();
    xigua_service_status_t status = {.available = true, .ai_status = -1}; publish(&status);
    size_t used = 0; bool overflow = false; uint8_t bytes[128];
    for (;;) {
        xigua_wifi_profile_t pending = {0};
        portENTER_CRITICAL(&s_lock);
        bool have_request = s_wifi_pending;
        if (have_request) {
            pending = s_wifi_request;
            memset(&s_wifi_request, 0, sizeof(s_wifi_request));
            s_wifi_pending = false;
        }
        portEXIT_CRITICAL(&s_lock);
        if (have_request) {
            xigua_wifi_config_result_t result = XG_WIFI_CONFIG_READ_ONLY;
            if (s_writable) {
                s_candidate = s_config;
                int slot = xigua_management_wifi_upsert(&s_candidate, pending.ssid, pending.password);
                if (slot < 0) result = slot == -2 ? XG_WIFI_CONFIG_FULL : XG_WIFI_CONFIG_INVALID;
                else if (!save()) result = XG_WIFI_CONFIG_SAVE_FAILED;
                else result = s_network_started && xigua_network_configure(&s_config) ?
                    XG_WIFI_CONFIG_SAVED : XG_WIFI_CONFIG_UNAVAILABLE;
                memset(&s_candidate, 0, sizeof(s_candidate));
            }
            memset(&pending, 0, sizeof(pending));
            portENTER_CRITICAL(&s_lock);
            s_wifi_result = result; s_wifi_busy = false; ++s_wifi_generation;
            portEXIT_CRITICAL(&s_lock);
        }
        int n = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), pdMS_TO_TICKS(50));
        for (int i = 0; i < n; ++i) {
            if (bytes[i] == '\n') {
                if (used && s_line[used - 1] == '\r') --used;
                if (overflow) reply(0, "line_too_long", NULL);
                else if (used >= 4 && !memcmp(s_line, "XG1 ", 4)) handle(s_line + 4, used - 4);
                memset(s_line, 0, sizeof(s_line)); memset(&s_request, 0, sizeof(s_request));
                used = 0; overflow = false;
            } else {
                if (used < sizeof(s_line) - 1 && !overflow) s_line[used++] = (char)bytes[i];
                else overflow = true;
            }
        }
        memset(bytes, 0, sizeof(bytes)); update_status();
    }
}
bool xigua_service_start(void)
{
    return xTaskCreate(worker, "xigua_service", 12288, NULL, 3, NULL) == pdPASS;
}
