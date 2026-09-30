#include "quota_app.h"

#include "bsp_battery.h"
#include "quota_config.h"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

static const char *TAG = "quota_app";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_MAX_RETRIES 5
#define QUOTA_REQUEST_TIMEOUT_MS 20000
#define QUOTA_MAX_ATTEMPTS 4

typedef struct {
    char label[48];
    char plan[16];
    bool online;
    bool query_ok;
    bool allowed;
    bool limit_reached;
    int primary_left;
    int secondary_left;
    int primary_reset_after;
    int secondary_reset_after;
} quota_account_t;

typedef struct {
    quota_account_t accounts[QUOTA_MAX_ACCOUNTS];
    size_t count;
    int battery_soc;
    bool service_ok;
    bool refreshing;
    bool manual_refresh_pending;
    bool manual_refresh_result_available;
    bool manual_refresh_succeeded;
    int64_t manual_refresh_completed_us;
    char error[64];
} quota_snapshot_t;

typedef struct {
    char *data;
    size_t length;
} response_buffer_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_status;
static lv_obj_t *s_account;
static lv_obj_t *s_primary;
static lv_obj_t *s_secondary;
static lv_obj_t *s_battery;
static lv_timer_t *s_timer;

static quota_snapshot_t s_snapshot;
static SemaphoreHandle_t s_snapshot_mutex;
static EventGroupHandle_t s_wifi_events;
static TaskHandle_t s_worker;
static esp_netif_t *s_netif;
static bool s_wifi_started;
static bool s_wifi_initialized;
static bool s_event_loop_ready;
static bool s_app_wifi_handler;
static bool s_app_ip_handler;
static int s_wifi_retries;
static size_t s_selected;
static volatile bool s_stop_requested;
static volatile bool s_refresh_requested;

static esp_err_t quota_wifi_stop(void);

static void mask_label(const char *input, char *output, size_t output_size)
{
    if (!input || !*input) {
        snprintf(output, output_size, "unknown");
        return;
    }
    const char *at = strchr(input, '@');
    if (at) {
        size_t local_len = (size_t)(at - input);
        if (local_len > 2) local_len = 2;
        snprintf(output, output_size, "%.*s***%s", (int)local_len, input, at);
        return;
    }
    size_t prefix = strlen(input);
    if (prefix > 3) prefix = 3;
    snprintf(output, output_size, "%.*s***", (int)prefix, input);
}

static esp_err_t response_append(response_buffer_t *buffer, const char *data, size_t length)
{
    if (!buffer || !data || length == 0) return ESP_OK;
    if (length > QUOTA_HTTP_BUFFER_SIZE - 1 - buffer->length) return ESP_ERR_NO_MEM;
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return ESP_OK;
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    response_buffer_t *buffer = event ? event->user_data : NULL;
    if (!buffer) return ESP_OK;
    if (event->event_id == HTTP_EVENT_ON_DATA) {
        return response_append(buffer, (const char *)event->data, event->data_len);
    }
    return ESP_OK;
}

static bool basic_auth_header(char *output, size_t output_size)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char credentials[256];
    int credential_length = snprintf(credentials, sizeof(credentials), "%s:%s",
                                     QUOTA_API_USERNAME, QUOTA_API_PASSWORD);
    if (credential_length <= 0 || (size_t)credential_length >= sizeof(credentials)) return false;
    size_t encoded_length = 4 * (((size_t)credential_length + 2) / 3);
    if (output_size <= sizeof("Basic ") - 1 + encoded_length) return false;
    memcpy(output, "Basic ", sizeof("Basic ") - 1);
    size_t out = sizeof("Basic ") - 1;
    for (size_t i = 0; i < (size_t)credential_length; i += 3) {
        uint32_t block = (uint32_t)(uint8_t)credentials[i] << 16;
        bool has_second = i + 1 < (size_t)credential_length;
        bool has_third = i + 2 < (size_t)credential_length;
        if (has_second) block |= (uint32_t)(uint8_t)credentials[i + 1] << 8;
        if (has_third) block |= (uint8_t)credentials[i + 2];
        output[out++] = alphabet[(block >> 18) & 0x3f];
        output[out++] = alphabet[(block >> 12) & 0x3f];
        output[out++] = has_second ? alphabet[(block >> 6) & 0x3f] : '=';
        output[out++] = has_third ? alphabet[block & 0x3f] : '=';
    }
    output[out] = '\0';
    memset(credentials, 0, sizeof(credentials));
    return true;
}

static esp_err_t http_request(const char *path, const char *method,
                              const char *body, response_buffer_t *response)
{
    if (!path || !response || QUOTA_API_BASE_URL[0] == '\0') return ESP_ERR_INVALID_ARG;
    bool use_tls = strncmp(QUOTA_API_BASE_URL, "https://", 8) == 0;
    if (!use_tls && !QUOTA_ALLOW_INSECURE_HTTP) return ESP_ERR_NOT_SUPPORTED;
    if (!use_tls && strncmp(QUOTA_API_BASE_URL, "http://", 7) != 0) return ESP_ERR_INVALID_ARG;
    char url[256];
    int written = snprintf(url, sizeof(url), "%s%s", QUOTA_API_BASE_URL, path);
    if (written < 0 || (size_t)written >= sizeof(url)) return ESP_ERR_INVALID_SIZE;

    esp_http_client_config_t config = {
        .url = url,
        .method = method && strcmp(method, "POST") == 0 ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = QUOTA_REQUEST_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = response,
        .disable_auto_redirect = true,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    char authorization[384];
    if (!basic_auth_header(authorization, sizeof(authorization))) {
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_http_client_set_header(client, "Authorization", authorization);
    if (body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) return err;
    return (status >= 200 && status < 300) ? ESP_OK : ESP_FAIL;
}

static cJSON *request_json(const char *path, const char *method, const char *body)
{
    char *buffer = calloc(1, QUOTA_HTTP_BUFFER_SIZE);
    if (!buffer) return NULL;
    response_buffer_t response = { .data = buffer, .length = 0 };
    esp_err_t err = http_request(path, method, body, &response);
    cJSON *json = err == ESP_OK ? cJSON_Parse(response.data) : NULL;
    free(buffer);
    return json;
}

static void copy_json_string(cJSON *object, const char *key, char *target, size_t target_size)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(value) && value->valuestring) {
        snprintf(target, target_size, "%s", value->valuestring);
    }
}

static void parse_window(cJSON *window, int *left, int *reset_after)
{
    if (!cJSON_IsObject(window)) return;
    cJSON *used = cJSON_GetObjectItemCaseSensitive(window, "used_percent");
    if (cJSON_IsNumber(used)) {
        double percent = used->valuedouble;
        if (isfinite(percent)) {
            if (percent >= 100.0) *left = 0;
            else if (percent <= 0.0) *left = 100;
            else *left = 100 - (int)(percent + 0.5);
        }
    }
    cJSON *reset = cJSON_GetObjectItemCaseSensitive(window, "reset_after_seconds");
    if (cJSON_IsNumber(reset) && reset->valuedouble >= 0 && reset->valuedouble <= INT32_MAX) {
        *reset_after = (int)reset->valuedouble;
    }
}

static bool parse_quota_result(cJSON *outer, quota_account_t *account)
{
    if (!cJSON_IsObject(outer) || !account) return false;
    cJSON *status_code = cJSON_GetObjectItemCaseSensitive(outer, "status_code");
    cJSON *body = cJSON_GetObjectItemCaseSensitive(outer, "body");
    if (!cJSON_IsNumber(status_code) || status_code->valueint != 200 || !cJSON_IsString(body)) {
        return false;
    }
    cJSON *usage = cJSON_Parse(body->valuestring);
    if (!usage) return false;
    cJSON *rate = cJSON_GetObjectItemCaseSensitive(usage, "rate_limit");
    bool ok = cJSON_IsObject(rate);
    if (ok) {
        copy_json_string(usage, "plan_type", account->plan, sizeof(account->plan));
        cJSON *allowed = cJSON_GetObjectItemCaseSensitive(rate, "allowed");
        cJSON *limit = cJSON_GetObjectItemCaseSensitive(rate, "limit_reached");
        account->allowed = cJSON_IsTrue(allowed);
        account->limit_reached = cJSON_IsTrue(limit);
        parse_window(cJSON_GetObjectItemCaseSensitive(rate, "primary_window"),
                     &account->primary_left, &account->primary_reset_after);
        parse_window(cJSON_GetObjectItemCaseSensitive(rate, "secondary_window"),
                     &account->secondary_left, &account->secondary_reset_after);
    }
    cJSON_Delete(usage);
    return ok;
}

static bool query_accounts(quota_snapshot_t *snapshot)
{
    cJSON *files_payload = request_json("/operator-api/management/auth-files", "GET", NULL);
    if (!files_payload) return false;
    cJSON *files = cJSON_GetObjectItemCaseSensitive(files_payload, "files");
    if (!cJSON_IsArray(files)) {
        cJSON_Delete(files_payload);
        return false;
    }

    size_t count = 0;
    cJSON *file = NULL;
    cJSON_ArrayForEach(file, files) {
        if (count >= QUOTA_MAX_ACCOUNTS || s_stop_requested) break;
        cJSON *type = cJSON_GetObjectItemCaseSensitive(file, "type");
        cJSON *provider = cJSON_GetObjectItemCaseSensitive(file, "provider");
        const char *type_text = cJSON_IsString(type) ? type->valuestring : "";
        const char *provider_text = cJSON_IsString(provider) ? provider->valuestring : "";
        if (strcasecmp(type_text, "codex") != 0 && strcasecmp(provider_text, "codex") != 0) continue;

        cJSON *auth_index = cJSON_GetObjectItemCaseSensitive(file, "auth_index");
        if (!cJSON_IsString(auth_index) || !auth_index->valuestring) continue;
        quota_account_t *account = &snapshot->accounts[count];
        memset(account, 0, sizeof(*account));
        account->primary_left = -1;
        account->secondary_left = -1;
        account->primary_reset_after = -1;
        account->secondary_reset_after = -1;
        cJSON *label = cJSON_GetObjectItemCaseSensitive(file, "email");
        if (!cJSON_IsString(label)) label = cJSON_GetObjectItemCaseSensitive(file, "label");
        mask_label(cJSON_IsString(label) ? label->valuestring : "unknown",
                   account->label, sizeof(account->label));
        copy_json_string(file, "plan", account->plan, sizeof(account->plan));
        account->online = true;

        cJSON *body = cJSON_CreateObject();
        bool body_ok = body &&
            cJSON_AddStringToObject(body, "auth_index", auth_index->valuestring) &&
            cJSON_AddStringToObject(body, "method", "GET") &&
            cJSON_AddStringToObject(body, "url", "https://chatgpt.com/backend-api/wham/usage");
        cJSON *header = body_ok ? cJSON_AddObjectToObject(body, "header") : NULL;
        body_ok = body_ok && header &&
            cJSON_AddStringToObject(header, "Authorization", "Bearer $TOKEN$") &&
            cJSON_AddStringToObject(header, "Accept", "application/json") &&
            cJSON_AddStringToObject(header, "Referer", "https://chatgpt.com/") &&
            cJSON_AddStringToObject(header, "Origin", "https://chatgpt.com") &&
            cJSON_AddStringToObject(header, "User-Agent", "Mozilla/5.0");
        char *body_text = body_ok ? cJSON_PrintUnformatted(body) : NULL;
        cJSON_Delete(body);
        if (body_text) {
            for (int attempt = 1; attempt <= QUOTA_MAX_ATTEMPTS && !s_stop_requested; attempt++) {
                cJSON *outer = request_json("/operator-api/management/api-call", "POST", body_text);
                account->query_ok = outer && parse_quota_result(outer, account);
                cJSON_Delete(outer);
                if (account->query_ok || attempt == QUOTA_MAX_ATTEMPTS) break;
                vTaskDelay(pdMS_TO_TICKS(1000 << (attempt - 1)));
            }
            free(body_text);
        }
        count++;
    }
    cJSON_Delete(files_payload);
    snapshot->count = count;
    return count > 0;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retries++ < WIFI_MAX_RETRIES) {
            esp_wifi_connect();
        } else if (s_wifi_events) {
            xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && s_wifi_events) {
        s_wifi_retries = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t quota_wifi_start(void)
{
    if (QUOTA_WIFI_SSID[0] == '\0') return ESP_ERR_INVALID_STATE;
    if (s_wifi_started) return ESP_OK;
    if (!s_event_loop_ready) {
        esp_err_t nvs_err = nvs_flash_init();
        if (nvs_err != ESP_OK && nvs_err != ESP_ERR_INVALID_STATE) return nvs_err;
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        s_event_loop_ready = true;
    }
    if (!s_wifi_events) s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) return ESP_ERR_NO_MEM;
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();
    s_netif = esp_netif_new(&netif_config);
    if (!s_netif) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_netif_attach_wifi_station(s_netif);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_wifi_set_default_wifi_sta_handlers();
    if (err != ESP_OK) {
        goto fail;
    }
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) goto fail;
    s_wifi_initialized = true;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    if (err != ESP_OK) goto fail;
    s_app_wifi_handler = true;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    if (err != ESP_OK) goto fail;
    s_app_ip_handler = true;
    wifi_config_t wifi_config = { 0 };
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", QUOTA_WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", QUOTA_WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    s_wifi_started = true;
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (bits & WIFI_CONNECTED_BIT) return ESP_OK;
    err = ESP_ERR_TIMEOUT;
fail:
    (void)quota_wifi_stop();
    return err;
}

static esp_err_t quota_wifi_stop(void)
{
    if (s_app_wifi_handler) {
        (void)esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler);
        s_app_wifi_handler = false;
    }
    if (s_app_ip_handler) {
        (void)esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler);
        s_app_ip_handler = false;
    }
    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = false;
    }
    if (s_wifi_initialized) {
        esp_wifi_deinit();
        s_wifi_initialized = false;
    }
    if (s_netif) {
        esp_netif_destroy_default_wifi(s_netif);
        s_netif = NULL;
    }
    return ESP_OK;
}

static void refresh_data(bool manual)
{
    quota_snapshot_t next = { .refreshing = true, .battery_soc = -1 };
    next.battery_soc = bsp_battery_soc();
    if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        s_snapshot.refreshing = true;
        s_snapshot.error[0] = '\0';
        xSemaphoreGive(s_snapshot_mutex);
    }
    esp_err_t wifi_err = quota_wifi_start();
    bool ok = wifi_err == ESP_OK && query_accounts(&next);
    if (!ok) (void)quota_wifi_stop();
    next.refreshing = false;
    if (!ok) {
        snprintf(next.error, sizeof(next.error), "%s",
                 wifi_err == ESP_OK ? "quota query failed" : "Wi-Fi unavailable");
    } else {
        next.service_ok = true;
    }
    if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (manual && !s_refresh_requested) {
            next.manual_refresh_pending = false;
            next.manual_refresh_result_available = true;
            next.manual_refresh_succeeded = ok;
            next.manual_refresh_completed_us = esp_timer_get_time();
        } else {
            next.manual_refresh_pending = s_snapshot.manual_refresh_pending || s_refresh_requested;
            next.manual_refresh_result_available = s_snapshot.manual_refresh_result_available;
            next.manual_refresh_succeeded = s_snapshot.manual_refresh_succeeded;
            next.manual_refresh_completed_us = s_snapshot.manual_refresh_completed_us;
        }
        s_snapshot = next;
        xSemaphoreGive(s_snapshot_mutex);
    }
    ESP_LOGI(TAG, "quota refresh complete: accounts=%u status=%s",
             (unsigned)next.count, next.service_ok ? "online" : "offline");
}

static void quota_worker(void *arg)
{
    (void)arg;
    bool startup_is_manual = false;
    if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        startup_is_manual = s_snapshot.manual_refresh_pending;
        xSemaphoreGive(s_snapshot_mutex);
    }
    refresh_data(startup_is_manual);
    while (!s_stop_requested) {
        if (s_refresh_requested) {
            s_refresh_requested = false;
            refresh_data(true);
            continue;
        }
        for (int i = 0; i < 300 && !s_stop_requested; i++) vTaskDelay(pdMS_TO_TICKS(1000));
        if (!s_stop_requested) refresh_data(false);
    }
    quota_wifi_stop();
    s_worker = NULL;
    vTaskDelete(NULL);
}

static const char *format_countdown(int seconds, char *buffer, size_t size)
{
    if (seconds < 0) return "unknown";
    if (seconds == 0) return "reset soon";
    int days = seconds / 86400;
    int hours = (seconds % 86400) / 3600;
    int minutes = (seconds % 3600) / 60;
    if (days > 0) snprintf(buffer, size, "%dd %dh", days, hours);
    else if (hours > 0) snprintf(buffer, size, "%dh %dm", hours, minutes);
    else snprintf(buffer, size, "%dm", minutes);
    return buffer;
}

static void ui_refresh(lv_timer_t *timer)
{
    (void)timer;
    if (!s_snapshot_mutex) return;
    quota_snapshot_t snapshot;
    size_t selected;
    if (xSemaphoreTake(s_snapshot_mutex, 0) != pdTRUE) return;
    snapshot = s_snapshot;
    selected = s_selected;
    xSemaphoreGive(s_snapshot_mutex);
    bool show_manual_result = snapshot.manual_refresh_result_available &&
                              esp_timer_get_time() - snapshot.manual_refresh_completed_us < 4000000;
    if (snapshot.battery_soc >= 0 && s_battery) {
        lv_label_set_text_fmt(s_battery, "%d%%", snapshot.battery_soc);
    }

    if (snapshot.refreshing || snapshot.manual_refresh_pending) {
        lv_label_set_text(s_status, "Refreshing quota...");
        return;
    }
    if (!snapshot.service_ok || snapshot.count == 0) {
        if (show_manual_result && !snapshot.manual_refresh_succeeded) {
            lv_label_set_text(s_status, "REFRESH FAILED");
        } else {
            lv_label_set_text_fmt(s_status, "OFFLINE: %.48s", snapshot.error);
        }
        lv_label_set_text(s_account, "No quota data");
        lv_label_set_text(s_primary, "Press OK to retry");
        lv_label_set_text(s_secondary, "");
        return;
    }
    if (selected >= snapshot.count) selected = snapshot.count - 1;
    quota_account_t *account = &snapshot.accounts[selected];
    if (show_manual_result && snapshot.manual_refresh_succeeded) {
        lv_label_set_text(s_status, "REFRESH COMPLETE");
    } else {
        lv_label_set_text_fmt(s_status, "ONLINE  %u accounts", (unsigned)snapshot.count);
    }
    lv_label_set_text_fmt(s_account, "%u/%u  %s  %s", (unsigned)(selected + 1),
                          (unsigned)snapshot.count, account->label,
                          account->plan[0] ? account->plan : "unknown");
    if (!account->query_ok) {
        lv_label_set_text(s_primary, "Quota unavailable");
        lv_label_set_text(s_secondary, "Try OK again");
        return;
    }
    char primary_reset[16];
    char secondary_reset[16];
    char primary_left[16];
    char secondary_left[16];
    format_countdown(account->primary_reset_after, primary_reset, sizeof(primary_reset));
    format_countdown(account->secondary_reset_after, secondary_reset, sizeof(secondary_reset));
    if (account->primary_left < 0) snprintf(primary_left, sizeof(primary_left), "--");
    else snprintf(primary_left, sizeof(primary_left), "%d", account->primary_left);
    if (account->secondary_left < 0) snprintf(secondary_left, sizeof(secondary_left), "--");
    else snprintf(secondary_left, sizeof(secondary_left), "%d", account->secondary_left);
    lv_label_set_text_fmt(s_primary, "5H  %s%% left  reset %s", primary_left, primary_reset);
    lv_label_set_text_fmt(s_secondary, "WEEK %s%% left  reset %s", secondary_left, secondary_reset);
}

void quota_app_enter(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    s_battery = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_battery, lv_color_hex(0xB9C7D1), 0);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -16, 12);
    lv_label_set_text(s_battery, "--%");

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 10);
    lv_label_set_text(title, "QUOTA");

    s_status = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x69D2E7), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 18, 48);
    lv_label_set_text(s_status, "Starting...");

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 204, 150);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1D2A35), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_account = lv_label_create(card);
    lv_obj_set_width(s_account, 184);
    lv_obj_set_style_text_color(s_account, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_account, LV_ALIGN_TOP_LEFT, 10, 12);
    lv_label_set_text(s_account, "Loading account...");

    s_primary = lv_label_create(card);
    lv_obj_set_width(s_primary, 184);
    lv_obj_set_style_text_color(s_primary, lv_color_hex(0x8BE28B), 0);
    lv_obj_align(s_primary, LV_ALIGN_TOP_LEFT, 10, 54);
    lv_label_set_text(s_primary, "5H loading");

    s_secondary = lv_label_create(card);
    lv_obj_set_width(s_secondary, 184);
    lv_obj_set_style_text_color(s_secondary, lv_color_hex(0xFFD166), 0);
    lv_obj_align(s_secondary, LV_ALIGN_TOP_LEFT, 10, 88);
    lv_label_set_text(s_secondary, "WEEK loading");

    lv_obj_t *hint = lv_label_create(s_screen);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xB9C7D1), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_label_set_text(hint, "UP/DOWN account   OK refresh");

    s_timer = lv_timer_create(ui_refresh, 500, NULL);
    lv_screen_load(s_screen);
}

void quota_app_exit(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_status = s_account = s_primary = s_secondary = s_battery = NULL;
}

esp_err_t quota_app_start(void)
{
    if (!s_snapshot_mutex) s_snapshot_mutex = xSemaphoreCreateMutex();
    if (!s_snapshot_mutex) return ESP_ERR_NO_MEM;
    if (s_worker) return ESP_ERR_INVALID_STATE;
    s_stop_requested = false;
    s_refresh_requested = false;
    if (xTaskCreate(quota_worker, "quota_worker", 8192, NULL, 5, &s_worker) != pdPASS) {
        s_worker = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t quota_app_stop(void)
{
    s_stop_requested = true;
    for (int i = 0; i < 50 && s_worker; i++) vTaskDelay(pdMS_TO_TICKS(100));
    return s_worker ? ESP_ERR_TIMEOUT : ESP_OK;
}

void quota_app_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK && !(btn == BSP_BTN_OK && ev == BSP_BTN_LONG)) return;
    if (s_snapshot_mutex && xSemaphoreTake(s_snapshot_mutex, 0) == pdTRUE) {
        if (s_snapshot.count > 0 && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
            if (btn == BSP_BTN_UP) s_selected = (s_selected + s_snapshot.count - 1) % s_snapshot.count;
            if (btn == BSP_BTN_DOWN) s_selected = (s_selected + 1) % s_snapshot.count;
        }
        xSemaphoreGive(s_snapshot_mutex);
    }
    if (btn == BSP_BTN_OK) {
        if (s_snapshot_mutex && xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_snapshot.manual_refresh_pending = true;
            xSemaphoreGive(s_snapshot_mutex);
        }
        if (s_worker == NULL) (void)quota_app_start();
        else s_refresh_requested = true;
    }
}
