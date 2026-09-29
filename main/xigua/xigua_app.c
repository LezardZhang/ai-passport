#include "xigua_app.h"
#include "xigua_core.h"
#include "xigua_input.h"
#include "xigua_store.h"
#include "xigua_ui.h"
#include "xigua_service.h"
#include "xigua_network.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdatomic.h>

static const char *TAG = "xigua";
static xigua_state_t s_state, s_candidate;
static uint8_t s_payload[XIGUA_SNAPSHOT_MAX];
static uint8_t s_scratch[XIGUA_SNAPSHOT_MAX + XG_STORE_HEADER];
static xigua_store_t s_store;
static nvs_handle_t s_nvs;
static bool s_read_only = true;
static bool s_records_available;
static QueueHandle_t s_inputs;
typedef struct { uint32_t serial; xigua_export_request_t request; } export_message_t;
typedef struct { uint32_t serial; xigua_export_status_t status; xigua_export_result_t result; } export_reply_t;
static QueueHandle_t s_export_requests, s_export_replies;
static uint32_t s_export_serial;
static atomic_bool s_input_overflow;
static uint32_t s_sequence;
static uint64_t s_feedback_until, s_undo_until, s_last_activity;
static xigua_ui_feedback_t s_feedback;
static int s_battery = -1;
static bool s_timer_alerted;
static bool s_time_reconstructed;
typedef struct { bsp_btn_t key; bsp_btn_ev_t event; uint64_t ms; } input_t;
static uint64_t uptime_ms(void) { return (uint64_t)esp_timer_get_time() / 1000u; }
bool xigua_app_export(const xigua_export_request_t *request,
    xigua_export_status_t *status, xigua_export_result_t *result)
{
    if (!request || !status || !result || !s_export_requests || !s_export_replies) return false;
    export_message_t message = {.serial = ++s_export_serial, .request = *request};
    if (xQueueSend(s_export_requests, &message, 0) != pdTRUE) return false;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(1500);
    export_reply_t reply;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(deadline - now) <= 0 ||
            xQueueReceive(s_export_replies, &reply, deadline - now) != pdTRUE) return false;
        if (reply.serial == message.serial) {
            *status = reply.status;
            *result = reply.result;
            return true;
        }
    }
}
static xigua_clock_t now_clock(void)
{
    return xigua_network_clock(s_state.boot_id, uptime_ms());
}
static void on_button(bsp_btn_t key, bsp_btn_ev_t event, void *context)
{
    (void)context;
    if (!s_inputs || (event != BSP_BTN_PRESS && event != BSP_BTN_RELEASE)) return;
    const input_t input = {key, event, uptime_ms()};
    if (xQueueSend(s_inputs, &input, 0) != pdTRUE)
        atomic_store_explicit(&s_input_overflow, true, memory_order_relaxed);
}
static int read_slot(void *context, unsigned slot, uint8_t *out, size_t capacity, size_t *length)
{
    (void)context;
    const char *key = slot ? "state_b" : "state_a";
    size_t needed = 0;
    esp_err_t err = nvs_get_blob(s_nvs, key, NULL, &needed);
    if (err == ESP_ERR_NVS_NOT_FOUND) return 1;
    if (err != ESP_OK || needed > capacity) return -1;
    *length = needed;
    return nvs_get_blob(s_nvs, key, out, length) == ESP_OK ? 0 : -1;
}
static bool write_slot(void *context, unsigned slot, const uint8_t *data, size_t length)
{
    (void)context;
    esp_err_t err = nvs_set_blob(s_nvs, slot ? "state_b" : "state_a", data, length);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGE(TAG, "storage commit failed: %s", esp_err_to_name(err));
    return err == ESP_OK;
}
static const xigua_store_backend_t s_backend = {NULL, read_slot, write_slot};
static bool payload_valid(const uint8_t *data, size_t length, void *context)
{
    (void)context;
    return xigua_snapshot_decode(&s_candidate, data, length);
}
static bool save_candidate(void)
{
    size_t length = xigua_snapshot_encode(&s_candidate, s_payload, sizeof(s_payload));
    if (!length || !xigua_store_save(&s_store, &s_backend, s_payload, length,
                                     s_scratch, sizeof(s_scratch))) {
        s_read_only = true;
        return false;
    }
    s_state = s_candidate;
    return true;
}
static void load_storage(void)
{
    s_records_available = false;
    xigua_init(&s_state, 1);
    esp_err_t err = nvs_flash_init_partition("xigua_data");
    if (err == ESP_OK) err = nvs_open_from_partition("xigua_data", "xigua", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage unavailable; no erase: %s", esp_err_to_name(err));
        return;
    }
    size_t length = 0;
    xigua_store_result_t loaded = xigua_store_load(&s_store, &s_backend,
        s_payload, sizeof(s_payload), &length, s_scratch, sizeof(s_scratch), payload_valid, NULL);
    if (loaded == XG_STORE_ERROR) {
        ESP_LOGE(TAG, "invalid snapshots; read-only, no automatic reset");
        return;
    }
    if (loaded != XG_STORE_EMPTY && !xigua_snapshot_decode(&s_state, s_payload, length)) return;
    if (loaded != XG_STORE_EMPTY) s_records_available = true;
    s_candidate = s_state;
    if (loaded != XG_STORE_EMPTY) {
        if (s_state.boot_id == UINT32_MAX || !xigua_reboot(&s_candidate, s_state.boot_id + 1)) return;
    }
    if (loaded == XG_STORE_RECOVERED) {
        s_state = s_candidate;
        ESP_LOGE(TAG, "older snapshot recovered; read-only until inspected");
        return;
    }
    if (save_candidate()) { s_read_only = false; s_records_available = true; }
    else s_state = s_candidate; /* New boot identity even when commit fails. */
}
static xigua_ui_view_t view_now(void)
{
    uint64_t ms = uptime_ms();
    if (ms >= s_feedback_until) s_feedback = XIGUA_UI_IDLE;
    xigua_service_status_t service; xigua_service_status(&service);
    xigua_audio_status_t audio; xigua_audio_get_status(&audio);
    return (xigua_ui_view_t){
        .state = &s_state, .now = now_clock(), .battery_pct = s_battery,
        .read_only = s_read_only, .feedback = s_feedback,
        .undo_available = !s_read_only && s_state.undo.valid && ms < s_undo_until,
        .service = service,
        .audio = audio,
    };
}
static void render(void)
{
    if (!bsp_lvgl_lock(100)) return;
    xigua_ui_view_t view = view_now();
    xigua_ui_render(&view);
    bsp_lvgl_unlock();
}
static void execute(xigua_ui_intent_t intent)
{
    if (intent.audio != XIGUA_UI_AUDIO_NONE) {
        if (intent.audio == XIGUA_UI_AUDIO_PLAY) xigua_audio_request_play((uint8_t)intent.value);
        else if (intent.audio == XIGUA_UI_AUDIO_PAUSE) xigua_audio_request_pause();
        else if (intent.audio == XIGUA_UI_AUDIO_VOLUME) xigua_audio_request_volume((uint8_t)intent.value);
        return;
    }
    if (!intent.valid) return;
    if (s_read_only || s_sequence == UINT32_MAX) {
        s_feedback = XIGUA_UI_READ_ONLY;
        s_feedback_until = uptime_ms() + 5000;
        return;
    }
    xigua_command_t cmd = {
        .command_id = ((uint64_t)s_state.boot_id << 32) | ++s_sequence,
        .action = intent.action, .value = intent.value,
    };
    const xigua_event_t *target = NULL;
    if (cmd.action == XIGUA_FEED_EDIT) target = xigua_latest(&s_state, XIGUA_FEED);
    if (cmd.action == XIGUA_SLEEP_STOP) target = xigua_active_sleep(&s_state);
    if (cmd.action == XIGUA_TUMMY_STOP) target = xigua_active_tummy(&s_state);
    if (target) { cmd.target_event_id = target->id; cmd.expected_revision = target->revision; }
    if (cmd.action == XIGUA_UNDO) {
        if (uptime_ms() >= s_undo_until) return;
        cmd.target_event_id = s_state.undo.target_event_id;
        cmd.expected_revision = s_state.revision;
    }
    if (cmd.action == XIGUA_TIMER_CANCEL) cmd.expected_revision = s_state.revision;
    s_candidate = s_state;
    xigua_clock_t now = now_clock();
    xigua_result_t result = xigua_apply(&s_candidate, &cmd, &now);
    bool saved = false;
    if (result.status == XIGUA_OK) {
        s_feedback = XIGUA_UI_SAVING;
        s_feedback_until = uptime_ms() + 5000;
        render();
        saved = save_candidate();
    }
    s_feedback = saved ? XIGUA_UI_SAVED :
        (result.status == XIGUA_FULL ? XIGUA_UI_STORAGE_FULL : XIGUA_UI_ERROR);
    s_feedback_until = uptime_ms() + 3000;
    s_undo_until = saved && cmd.action != XIGUA_UNDO ? uptime_ms() + 5000 : 0;
    if (saved && (cmd.action == XIGUA_TIMER_START || cmd.action == XIGUA_TIMER_CANCEL ||
                  (cmd.action == XIGUA_UNDO && cmd.target_event_id == 0))) {
        s_timer_alerted = false;
        xigua_audio_request_dismiss_alert();
    }
    if (bsp_lvgl_lock(100)) { xigua_ui_command_result(saved); bsp_lvgl_unlock(); }
    ESP_LOGI(TAG, "command=%u status=%u committed=%d records=%u",
             (unsigned)cmd.action, (unsigned)result.status, saved, s_state.event_count);
}
static void dispatch_key(int key, xigua_input_action_t action)
{
    if (action != XG_INPUT_CLICK && action != XG_INPUT_LONG) return;
    xigua_ui_key_t ui_key = (xigua_ui_key_t)(key + (action == XG_INPUT_LONG ? 3 : 0));
    xigua_ui_intent_t intent = {0};
    if (bsp_lvgl_lock(100)) {
        xigua_ui_view_t view = view_now();
        intent = xigua_ui_key(ui_key, &view);
        bsp_lvgl_unlock();
    }
    execute(intent);
}
static void controller(void *context)
{
    (void)context;
    load_storage();
    s_inputs = xQueueCreate(16, sizeof(input_t));
    if (!s_inputs) { ESP_LOGE(TAG, "input queue failed"); vTaskDelete(NULL); return; }
    s_export_requests = xQueueCreate(1, sizeof(export_message_t));
    s_export_replies = xQueueCreate(1, sizeof(export_reply_t));
    if (!s_export_requests || !s_export_replies) ESP_LOGE(TAG, "record export queue unavailable");
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display initialization failed"); vTaskDelete(NULL); return;
    }
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "UI initialization lock failed"); vTaskDelete(NULL); return;
    }
    xigua_ui_create();
    bsp_lvgl_unlock();
    bsp_display_backlight(65);
    render();
    esp_err_t button_result = bsp_button_init(on_button, NULL);
    if (button_result != ESP_OK) ESP_LOGE(TAG, "buttons unavailable: %s", esp_err_to_name(button_result));
    if (bsp_battery_init() == ESP_OK) s_battery = bsp_battery_soc();
    xigua_input_t gesture;
    xigua_input_init(&gesture);
    s_last_activity = uptime_ms();
    if (xigua_audio_start() != ESP_OK) ESP_LOGE(TAG, "audio worker unavailable");
    if (!xigua_service_start()) ESP_LOGE(TAG, "network/configuration worker unavailable");
    uint64_t next_render = 0, next_battery = 0;
    unsigned brightness = 65;
    ESP_LOGI(TAG, "Xigua ready; records=%u readonly=%d heap=%u largest=%u",
        s_state.event_count, s_read_only, (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    for (;;) {
        input_t input;
        if (xQueueReceive(s_inputs, &input, pdMS_TO_TICKS(40)) == pdTRUE) {
            /* Preserve edge timestamps and drain RELEASE before ticking wall time:
             * an ordinary click queued during a commit must not become a hold. */
            do {
                if (atomic_exchange_explicit(&s_input_overflow, false, memory_order_relaxed)) {
                    xQueueReset(s_inputs);
                    xigua_input_init(&gesture);
                    ESP_LOGW(TAG, "input overflow; canceled queued gesture");
                    break;
                }
                xigua_input_action_t action;
                if (input.event == BSP_BTN_PRESS) {
                    action = xigua_input_press(&gesture, input.key, input.ms, brightness == 0);
                    s_last_activity = uptime_ms();
                    if (brightness != 65) { bsp_display_backlight(65); brightness = 65; }
                } else action = xigua_input_release(&gesture, input.key, input.ms);
                dispatch_key(input.key, action);
                next_render = 0;
            } while (xQueueReceive(s_inputs, &input, 0) == pdTRUE);
        }
        if (atomic_exchange_explicit(&s_input_overflow, false, memory_order_relaxed)) {
            xQueueReset(s_inputs);
            xigua_input_init(&gesture);
        }
        export_message_t export_request;
        if (s_export_requests && s_export_replies &&
            xQueueReceive(s_export_requests, &export_request, 0) == pdTRUE) {
            export_reply_t response = {.serial = export_request.serial};
            response.status = s_records_available ?
                xigua_export_read(&s_state, &export_request.request, &response.result) : XG_EXPORT_UNAVAILABLE;
            xQueueOverwrite(s_export_replies, &response);
        }
        uint64_t ms = uptime_ms();
        int key = -1;
        xigua_input_action_t held = xigua_input_tick(&gesture, ms, &key);
        if (held != XG_INPUT_NONE) { dispatch_key(key, held); next_render = 0; }
        uint32_t remaining;
        xigua_clock_t now = now_clock();
        if (!s_time_reconstructed && now.quality == XIGUA_TIME_TRUSTED) {
            s_time_reconstructed = true;
            if (!s_read_only) {
                s_candidate = s_state;
                if (xigua_reconstruct(&s_candidate, &now)) (void)save_candidate();
            }
            next_render = 0;
        }
        if (!s_timer_alerted && xigua_timer_remaining(&s_state, &now, &remaining) && !remaining) {
            s_timer_alerted = true; s_last_activity = ms; next_render = 0;
            xigua_audio_request_alert();
        }
        unsigned desired = ms - s_last_activity >= 60000 ? 0 : ms - s_last_activity >= 30000 ? 15 : 65;
        if (desired != brightness) { bsp_display_backlight(desired); brightness = desired; }
        if (ms >= next_render) { render(); next_render = ms + 500; }
        if (ms >= next_battery) { s_battery = bsp_battery_soc(); next_battery = ms + 30000; }
    }
}
void xigua_app_start(void)
{
    if (xTaskCreate(controller, "xigua", 8192, NULL, 5, NULL) != pdPASS)
        ESP_LOGE(TAG, "controller allocation failed");
}
