#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "xigua_app.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdbool.h>

static const char *TAG = "main";

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static volatile bool s_input_ready;

static void input_task(void *arg)
{
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            xigua_app_key(input.btn, input.event);
        }
    }
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "FoloToy Xigua childcare application starting");
    bsp_i2c_init();
    (void)bsp_battery_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL initialization failed");
        return;
    }
    bsp_display_backlight(100);

    s_input_queue = xQueueCreate(8, sizeof(input_event_t));
    if (!s_input_queue) {
        ESP_LOGE(TAG, "input queue allocation failed");
        return;
    }
    /* Page actions call into LVGL layout and text measurement. A 4 KiB task
     * stack was enough for the old menu, but overflows when the AI/story view
     * is rebuilt after a button event. Keep input callbacks responsive while
     * giving those synchronous UI calls a bounded stack budget. */
    if (xTaskCreate(input_task, "xigua_input", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "input task creation failed");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button initialization failed");
        return;
    }

    esp_err_t app_err = xigua_app_start();
    if (app_err != ESP_OK) {
        ESP_LOGE(TAG, "xigua app start failed: %s", esp_err_to_name(app_err));
        return;
    }
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "LVGL lock timeout");
        return;
    }
    xigua_app_enter();
    bsp_lvgl_unlock();
    s_input_ready = true;
}
