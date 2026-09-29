#ifndef XIGUA_CONFIG_H
#define XIGUA_CONFIG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XG_WIFI_PROFILES 8
#define XG_ENDPOINT_MAX 192
#define XG_KEY_MAX 192
#define XG_MODEL_MAX 64
#define XG_TZ_MAX 64
typedef struct {
    char ssid[33];
    char password[65];
    bool enabled;
    uint8_t priority;
} xigua_wifi_profile_t;
typedef struct {
    char endpoint[XG_ENDPOINT_MAX]; /* full HTTPS chat-completions URL */
    char key[XG_KEY_MAX];
    char model[XG_MODEL_MAX];
} xigua_ai_config_t;
typedef struct {
    xigua_wifi_profile_t wifi[XG_WIFI_PROFILES];
    xigua_ai_config_t ai;
    char timezone[XG_TZ_MAX]; /* empty = not configured, never assume local TZ */
} xigua_config_t;
void xigua_config_defaults(xigua_config_t *config);
bool xigua_config_valid(const xigua_config_t *config);
bool xigua_config_timezone_valid(const char *timezone);
bool xigua_config_ai_valid(const xigua_ai_config_t *ai, bool require_key);
#endif
