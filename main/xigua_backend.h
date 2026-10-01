#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
esp_err_t xigua_backend_start(void);
void xigua_backend_stop(void);
bool xigua_backend_network_idle(void);
/* Copies one cached entry and returns the current bounded catalog size. */
size_t xigua_backend_catalog_item(size_t index, char *title, size_t capacity, bool *white_noise);
size_t xigua_backend_catalog_item_category(size_t index, char *title, size_t capacity,
                                            char *category, size_t category_capacity);
void xigua_backend_refresh_catalog(void);
void xigua_backend_play_track(size_t index);
void xigua_backend_stop_sound(void);
void xigua_backend_toggle_pause(void);
void xigua_backend_status(char *out, size_t capacity);
void xigua_backend_report_playback(const char *command_id, const char *status, const char *error);
/* Cached server data only; no networking from a UI/AI caller. */
bool xigua_backend_care_context(char *text, size_t capacity, uint64_t *revision);
bool xigua_backend_handoff(char *text, size_t capacity);
/* Snapshot is allocated outside the LVGL task; caller owns the returned string. */
char *xigua_app_cloud_snapshot(void);
