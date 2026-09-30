#pragma once

#include "bsp_button.h"
#include "esp_err.h"

// The quota application owns its screen and the background Wi-Fi/API worker.
// enter/exit are called while the LVGL lock is held; start/stop are not.
void quota_app_enter(void);
void quota_app_exit(void);
esp_err_t quota_app_start(void);
esp_err_t quota_app_stop(void);
void quota_app_key(bsp_btn_t btn, bsp_btn_ev_t ev);
