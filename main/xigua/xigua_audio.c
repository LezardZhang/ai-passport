#include "xigua_audio.h"
#include "xigua_audio_policy.h"
#include "xigua_sound.h"
#include "bsp_audio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>

#define XIGUA_AUDIO_CHUNK_SAMPLES 320u /* 20 ms, 640 bytes of PCM. */
#define XIGUA_AUDIO_STACK_BYTES 4096u
_Static_assert(XIGUA_AUDIO_ALERT_SAMPLES == XIGUA_SOUND_ALERT_SAMPLES,
               "alert policy and generator durations must agree");
_Static_assert(XIGUA_AUDIO_TRACK_COUNT == XIGUA_SOUND_ALERT,
               "ambient policy IDs must match sound generator IDs");

static const char *TAG = "xigua_audio";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static xigua_audio_policy_t s_policy;
static xigua_audio_status_t s_status = {.volume = XIGUA_AUDIO_VOLUME_DEFAULT, .error = ESP_OK};
static TaskHandle_t s_task;

static void notify_worker(void)
{
    TaskHandle_t task;
    portENTER_CRITICAL(&s_lock);
    task = s_task;
    portEXIT_CRITICAL(&s_lock);
    if (task) xTaskNotifyGive(task);
}

static void publish_active(xigua_audio_segment_t segment, bool busy)
{
    portENTER_CRITICAL(&s_lock);
    s_status.busy = busy;
    s_status.playing = segment == XIGUA_AUDIO_SEGMENT_BACKGROUND && !busy;
    s_status.alerting = segment == XIGUA_AUDIO_SEGMENT_ALERT && !busy;
    portEXIT_CRITICAL(&s_lock);
}

static void publish_error(esp_err_t error, const xigua_audio_selection_t *selection)
{
    portENTER_CRITICAL(&s_lock);
    if (selection) xigua_audio_policy_fail_if_current(&s_policy, selection);
    s_status.busy = false;
    s_status.playing = false;
    s_status.alerting = false;
    s_status.error = error;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGE(TAG, "codec/PCM operation failed: %s", esp_err_to_name(error));
}

static void publish_ready(void)
{
    portENTER_CRITICAL(&s_lock);
    s_status.available = true;
    s_status.error = ESP_OK;
    portEXIT_CRITICAL(&s_lock);
}

static xigua_audio_selection_t select_next(void)
{
    xigua_audio_selection_t selection;
    portENTER_CRITICAL(&s_lock);
    selection = xigua_audio_policy_select(&s_policy, XIGUA_AUDIO_CHUNK_SAMPLES);
    portEXIT_CRITICAL(&s_lock);
    return selection;
}

static bool still_current(const xigua_audio_selection_t *selection)
{
    bool current;
    portENTER_CRITICAL(&s_lock);
    current = xigua_audio_policy_is_current(&s_policy, selection);
    portEXIT_CRITICAL(&s_lock);
    return current;
}

static void worker_fault(esp_err_t error,
                         const xigua_audio_selection_t *selection,
                         bool initialized, bool *sleeping, bool *format_ready,
                         uint8_t *hardware_volume)
{
    publish_error(error, selection);
    if (initialized && !*sleeping) {
        esp_err_t sleep_error = bsp_audio_sleep();
        if (sleep_error != ESP_OK)
            ESP_LOGE(TAG, "codec cleanup failed: %s", esp_err_to_name(sleep_error));
        *sleeping = true;
    }
    *format_ready = false;
    *hardware_volume = UINT8_MAX;
}

static void audio_worker(void *context)
{
    (void)context;
    int16_t pcm[XIGUA_AUDIO_CHUNK_SAMPLES];
    xigua_sound_t background = {0}, alert = {0};
    bool initialized = false, sleeping = false, format_ready = false;
    bool bg_valid = false, alert_valid = false;
    uint8_t bg_track = 0, hardware_volume = UINT8_MAX;
    uint32_t bg_generation = 0, alert_serial = 0, fade_chunks = 0;
    xigua_audio_segment_t previous_segment = XIGUA_AUDIO_SEGMENT_IDLE;
    int64_t previous_feed_us = 0;
    uint32_t chunks = 0;

    for (;;) {
        xigua_audio_selection_t selection = select_next();
        if (selection.segment == XIGUA_AUDIO_SEGMENT_IDLE) {
            publish_active(XIGUA_AUDIO_SEGMENT_IDLE, false);
            previous_segment = XIGUA_AUDIO_SEGMENT_IDLE;
            previous_feed_us = 0;
            if (initialized && !sleeping) {
                esp_err_t error = bsp_audio_sleep();
                sleeping = true; /* BSP records sleeping even if verification failed. */
                format_ready = false;
                hardware_volume = UINT8_MAX;
                if (error != ESP_OK) publish_error(error, NULL);
            }
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        if (previous_segment != selection.segment || !initialized || sleeping)
            publish_active(XIGUA_AUDIO_SEGMENT_IDLE, true);
        esp_err_t error = ESP_OK;
        if (!initialized) {
            error = bsp_audio_init();
            if (error == ESP_OK) initialized = true;
        }
        if (error == ESP_OK && sleeping) {
            error = bsp_audio_wake();
            if (error == ESP_OK) { sleeping = false; format_ready = false; }
        }
        if (error == ESP_OK && !format_ready) {
            error = bsp_audio_set_format(XIGUA_SOUND_SAMPLE_RATE, 16, 1);
            if (error == ESP_OK) format_ready = true;
        }
        if (error != ESP_OK) {
            worker_fault(error, &selection, initialized, &sleeping,
                         &format_ready, &hardware_volume);
            continue;
        }
        /* codec-dev maps 0..100 onto -50..0 dB. Preserve the full range: a
         * limit of 40 leaves even the loudest setting attenuated by 30 dB. */
        if (hardware_volume != selection.volume) {
            bsp_audio_set_volume(selection.volume);
            hardware_volume = selection.volume;
        }
        publish_ready();
        if (!still_current(&selection)) continue;

        xigua_sound_t *sound;
        if (selection.segment == XIGUA_AUDIO_SEGMENT_ALERT) {
            if (!alert_valid || alert_serial != selection.alert_serial) {
                if (!xigua_sound_init(&alert, XIGUA_SOUND_ALERT)) {
                    worker_fault(ESP_FAIL, &selection, initialized, &sleeping,
                                 &format_ready, &hardware_volume);
                    continue;
                }
                alert_serial = selection.alert_serial;
                alert_valid = true;
            }
            sound = &alert;
        } else {
            if (!bg_valid || bg_track != selection.track ||
                bg_generation != selection.generation) {
                if (!xigua_sound_init(&background, (xigua_sound_track_t)selection.track)) {
                    worker_fault(ESP_FAIL, &selection, initialized, &sleeping,
                                 &format_ready, &hardware_volume);
                    continue;
                }
                bg_track = selection.track;
                bg_generation = selection.generation;
                bg_valid = true;
            }
            sound = &background;
        }

        if (previous_segment != selection.segment) {
            fade_chunks = 0;
            previous_feed_us = 0;
        }
        previous_segment = selection.segment;
        size_t count = xigua_sound_render(sound, pcm, selection.samples);
        if (count != selection.samples) {
            worker_fault(ESP_FAIL, &selection, initialized, &sleeping,
                         &format_ready, &hardware_volume);
            continue;
        }
        /* Bounded fade-in reduces abrupt starts; audible transitions need board testing. */
        if (fade_chunks < 4) {
            unsigned gain = ++fade_chunks;
            for (size_t i = 0; i < count; ++i)
                pcm[i] = (int16_t)((int32_t)pcm[i] * (int32_t)gain / 4);
        }
        /* Requests arriving during codec setup/render discard this stale chunk. */
        if (!still_current(&selection)) continue;
        /* Mute remains effective for new PCM even if codec volume control fails. */
        if (!selection.volume)
            for (size_t i = 0; i < count; ++i) pcm[i] = 0;
        int64_t feed_us = esp_timer_get_time();
        if (previous_feed_us > 0 && feed_us > previous_feed_us) {
            uint32_t gap_ms = (uint32_t)((feed_us - previous_feed_us) / 1000);
            portENTER_CRITICAL(&s_lock);
            if (gap_ms > s_status.max_feed_gap_ms) s_status.max_feed_gap_ms = gap_ms;
            portEXIT_CRITICAL(&s_lock);
        }
        previous_feed_us = feed_us;
        error = bsp_audio_write(pcm, count * sizeof(pcm[0]));
        if (error != ESP_OK) {
            worker_fault(error, &selection, initialized, &sleeping,
                         &format_ready, &hardware_volume);
            continue;
        }
        portENTER_CRITICAL(&s_lock);
        xigua_audio_policy_rendered(&s_policy, &selection, count);
        portEXIT_CRITICAL(&s_lock);
        if (still_current(&selection)) publish_active(selection.segment, false);
        if (++chunks % 50u == 0) {
            uint32_t remaining = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
            portENTER_CRITICAL(&s_lock);
            s_status.stack_min_bytes = remaining;
            portEXIT_CRITICAL(&s_lock);
        }
    }
}

esp_err_t xigua_audio_start(void)
{
    portENTER_CRITICAL(&s_lock);
    if (s_task) { portEXIT_CRITICAL(&s_lock); return ESP_OK; }
    xigua_audio_policy_init(&s_policy);
    s_status = (xigua_audio_status_t){.volume = XIGUA_AUDIO_VOLUME_DEFAULT, .error = ESP_OK};
    portEXIT_CRITICAL(&s_lock);
    TaskHandle_t task = NULL;
    if (xTaskCreate(audio_worker, "xigua_audio", XIGUA_AUDIO_STACK_BYTES,
                    NULL, 5, &task) != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_status.error = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&s_lock);
    s_task = task;
    s_status.available = true; /* Codec readiness remains lazy; failures are published. */
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

void xigua_audio_request_play(uint8_t track)
{
    bool changed;
    portENTER_CRITICAL(&s_lock);
    changed = xigua_audio_policy_play(&s_policy, track);
    portEXIT_CRITICAL(&s_lock);
    if (changed) notify_worker();
}

void xigua_audio_request_pause(void)
{
    portENTER_CRITICAL(&s_lock);
    xigua_audio_policy_pause(&s_policy);
    portEXIT_CRITICAL(&s_lock);
    notify_worker();
}

void xigua_audio_request_stop(void)
{
    portENTER_CRITICAL(&s_lock);
    xigua_audio_policy_stop(&s_policy);
    portEXIT_CRITICAL(&s_lock);
    notify_worker();
}

void xigua_audio_request_alert(void)
{
    portENTER_CRITICAL(&s_lock);
    xigua_audio_policy_alert(&s_policy);
    portEXIT_CRITICAL(&s_lock);
    notify_worker();
}

void xigua_audio_request_dismiss_alert(void)
{
    portENTER_CRITICAL(&s_lock);
    xigua_audio_policy_dismiss_alert(&s_policy);
    portEXIT_CRITICAL(&s_lock);
    notify_worker();
}

void xigua_audio_request_volume(uint8_t level)
{
    portENTER_CRITICAL(&s_lock);
    xigua_audio_policy_volume(&s_policy, level);
    portEXIT_CRITICAL(&s_lock);
    notify_worker();
}

void xigua_audio_get_status(xigua_audio_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    out->requested_playing = s_policy.desired == XIGUA_AUDIO_DESIRED_PLAY;
    out->requested_track = s_policy.track;
    out->track = s_policy.track;
    out->volume = s_policy.volume;
    portEXIT_CRITICAL(&s_lock);
}
