#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdint.h>
#include "xigua_audio.h"

/* Compile xigua_audio.c into this translation unit to execute its persistent
 * task with a deterministic fake scheduler and codec. */
#include "../main/xigua/xigua_audio.c"

typedef enum {
    TEST_LAZY_STOP,
    TEST_ALERT_STOP,
    TEST_FAULT_RETRY,
    TEST_STOP_DURING_INIT,
    TEST_STALE_FAILURE,
    TEST_STEADY,
    TEST_ALERT_RESUME,
    TEST_MUTE,
    TEST_MAX_VOLUME,
} scenario_t;

static jmp_buf done;
static scenario_t scenario;
static unsigned inits, formats, wakes, sleeps, writes, volumes, waits;
static bool observed_fault;
static int64_t fake_time;

const char *esp_err_to_name(esp_err_t error)
{
    (void)error;
    return "fake error";
}

int64_t esp_timer_get_time(void) { fake_time += 20000; return fake_time; }

int xTaskCreate(void (*task)(void *), const char *name, uint32_t stack_bytes,
                void *context, unsigned priority, TaskHandle_t *created)
{
    (void)task; (void)name; (void)context; (void)priority;
    assert(stack_bytes <= 4096);
    *created = (TaskHandle_t)(uintptr_t)1;
    return pdPASS;
}

void xTaskNotifyGive(TaskHandle_t task) { assert(task); }
uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    (void)task;
    return 2048;
}

uint32_t ulTaskNotifyTake(int clear, uint32_t wait)
{
    (void)clear; (void)wait;
    waits++;
    if (scenario == TEST_FAULT_RETRY && waits == 1) {
        xigua_audio_status_t status;
        xigua_audio_get_status(&status);
        observed_fault = status.available && status.error == ESP_FAIL &&
                         !status.requested_playing;
        xigua_audio_request_play(0);
        return 1;
    }
    longjmp(done, 1);
}

esp_err_t bsp_audio_init(void)
{
    inits++;
    if (scenario == TEST_STOP_DURING_INIT) xigua_audio_request_stop();
    return ESP_OK;
}
esp_err_t bsp_audio_set_format(uint32_t hz, uint8_t bits, uint8_t channels)
{
    assert(hz == 16000 && bits == 16 && channels == 1);
    formats++;
    return ESP_OK;
}
esp_err_t bsp_audio_wake(void) { assert(sleeps > 0); wakes++; return ESP_OK; }
esp_err_t bsp_audio_sleep(void) { sleeps++; return ESP_OK; }
void bsp_audio_set_volume(uint8_t volume)
{
    assert(volume <= 100 && volume % 5 == 0);
    if (scenario == TEST_MAX_VOLUME) assert(volume == 100);
    volumes++;
}
esp_err_t bsp_audio_write(const void *pcm, size_t bytes)
{
    assert(pcm && bytes > 0 && bytes <= 640);
    writes++;
    if (scenario == TEST_STEADY || scenario == TEST_MUTE || scenario == TEST_MAX_VOLUME) {
        if (writes > 1) {
            xigua_audio_status_t status;
            xigua_audio_get_status(&status);
            assert(status.playing && !status.busy && !status.alerting);
        }
        if (scenario == TEST_MUTE) {
            const int16_t *samples = pcm;
            for (size_t i = 0; i < bytes / sizeof(*samples); ++i) assert(samples[i] == 0);
        }
        if (writes == 60) xigua_audio_request_stop();
    }
    if (scenario == TEST_ALERT_RESUME) {
        if (writes == 1) xigua_audio_request_alert();
        else if (writes == 2) xigua_audio_request_play(2);
        else if (writes == 52) {
            assert(s_policy.alert_samples_remaining == 0 && s_policy.track == 2);
            xigua_audio_request_stop();
        }
    }
    if (scenario == TEST_LAZY_STOP) xigua_audio_request_stop();
    if (scenario == TEST_ALERT_STOP) {
        if (writes == 1) xigua_audio_request_alert();
        else xigua_audio_request_stop();
    }
    if (scenario == TEST_FAULT_RETRY) {
        if (writes == 1) return ESP_FAIL;
        xigua_audio_request_stop();
    }
    if (scenario == TEST_STALE_FAILURE) {
        if (writes == 1) {
            xigua_audio_request_play(1);
            return ESP_FAIL;
        }
        assert(s_policy.track == 1);
        xigua_audio_request_stop();
    }
    return ESP_OK;
}

static void run(scenario_t chosen)
{
    scenario = chosen;
    inits = formats = wakes = sleeps = writes = volumes = waits = 0;
    fake_time = 0;
    observed_fault = false;
    s_task = NULL;
    assert(xigua_audio_start() == ESP_OK);
    assert(inits == 0); /* Start does not touch codec. */
    xigua_audio_request_play(0);
    if (chosen == TEST_MUTE) xigua_audio_request_volume(0);
    if (chosen == TEST_MAX_VOLUME) xigua_audio_request_volume(100);
    if (setjmp(done) == 0) audio_worker(NULL);
    xigua_audio_status_t status;
    xigua_audio_get_status(&status);
    assert(status.available && !status.requested_playing);
    assert(inits == 1 && sleeps == ((chosen == TEST_FAULT_RETRY ||
                                     chosen == TEST_STALE_FAILURE) ? 2u : 1u) &&
           volumes >= 1);
    assert(waits >= 1);
    if (chosen == TEST_LAZY_STOP) {
        assert(writes == 1 && formats == 1 && wakes == 0);
    } else if (chosen == TEST_ALERT_STOP) {
        assert(writes == 2 && formats == 1 && wakes == 0);
    } else if (chosen == TEST_FAULT_RETRY) {
        assert(observed_fault && writes == 2 && wakes == 1 && sleeps == 2);
        assert(status.error == ESP_OK);
    } else if (chosen == TEST_STALE_FAILURE) {
        assert(writes == 2 && wakes == 1 && sleeps == 2 && formats == 2);
        assert(status.error == ESP_OK);
    } else if (chosen == TEST_STEADY || chosen == TEST_MUTE || chosen == TEST_MAX_VOLUME) {
        assert(writes == 60 && formats == 1 && status.stack_min_bytes == 2048);
    } else if (chosen == TEST_ALERT_RESUME) {
        assert(writes == 52 && formats == 1);
    } else {
        assert(writes == 0 && formats == 1);
    }
}

int main(void)
{
    run(TEST_LAZY_STOP);
    run(TEST_ALERT_STOP);
    run(TEST_FAULT_RETRY);
    run(TEST_STOP_DURING_INIT);
    run(TEST_STALE_FAILURE);
    run(TEST_STEADY);
    run(TEST_ALERT_RESUME);
    run(TEST_MUTE);
    run(TEST_MAX_VOLUME);
    puts("xigua audio worker PASS");
    return 0;
}
