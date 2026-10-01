#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

/* Calls from button callbacks only enqueue work. The manager owns NimBLE. */
esp_err_t xigua_ble_begin(void);
void xigua_ble_end(void);
esp_err_t xigua_ble_shutdown(void);
bool xigua_ble_active(void);
void xigua_ble_screen(char *out, size_t capacity);
