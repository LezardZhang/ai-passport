#include "xigua_app.h"

#include "bsp_battery.h"
#include "bsp_display.h"
#include "xigua_wifi.h"
#include "xigua_ai.h"
#include "xigua_keyboard.h"
#include "xigua_ai_ui.h"
#include "xigua_text.h"
#include "xigua_menu.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

extern const lv_font_t xigua_font_zh16;
extern const lv_font_t xigua_font_zh20;
extern const lv_font_t xigua_font_full20;
static lv_font_t s_xigua_font;
static lv_font_t s_xigua_font20;
static bool s_xigua_font_ready;

static const char *TAG = "xigua_app";

typedef enum {
    X_PAGE_OVERVIEW = 0,
    X_PAGE_RECORD,
    X_PAGE_DIAPER,
    X_PAGE_FEED,
    X_PAGE_ACTIVE,
    X_PAGE_TIMER,
    X_PAGE_VOICE,
    X_PAGE_STORY,
    X_PAGE_SOUND,
    X_PAGE_TODAY,
    X_PAGE_SETTINGS,
    X_PAGE_WIFI,
} x_page_t;

typedef enum {
    X_WIFI_UI_MENU = 0,
    X_WIFI_UI_SCAN,
    X_WIFI_UI_STATUS,
    X_WIFI_UI_EDIT_PASSWORD,
} x_wifi_ui_mode_t;

typedef enum {
    X_WIFI_KEY_UPPER = 0,
    X_WIFI_KEY_LOWER,
    X_WIFI_KEY_DIGIT_SYMBOL,
} x_wifi_keyboard_mode_t;

typedef enum {
    X_ACTIVE_NONE = 0,
    X_ACTIVE_SLEEP,
    X_ACTIVE_BATH,
    X_ACTIVE_TUMMY,
    X_ACTIVE_TIMER,
} x_active_t;

typedef enum {
    X_EVENT_FEED = 1,
    X_EVENT_PEE,
    X_EVENT_POOP,
    X_EVENT_SLEEP,
    X_EVENT_BATH,
    X_EVENT_TUMMY,
    X_EVENT_TIMER,
} x_event_type_t;

typedef struct {
    int64_t epoch;
    uint16_t amount;
    uint16_t duration_min;
    uint8_t type;
    uint8_t ingredient;
} x_event_t;

#define X_EVENT_CAPACITY 32

typedef struct {
    uint32_t magic;
    uint16_t milk_ml;
    uint16_t milk_count;
    uint8_t milk_ingredient;
    uint16_t diaper_count;
    uint16_t pee_count;
    uint16_t poop_count;
    uint16_t sleep_count;
    uint16_t bath_count;
    uint16_t tummy_count;
    uint16_t timer_count;
    uint16_t sleep_minutes;
    uint8_t brightness;
    uint8_t timezone_index;
    uint8_t active;
    uint16_t timer_minutes;
    int64_t last_milk_epoch;
    int64_t last_diaper_epoch;
    int64_t sleep_start_epoch;
    int64_t sleep_end_epoch;
    uint8_t event_count;
    uint8_t event_head;
    x_event_t events[X_EVENT_CAPACITY];
} x_persisted_t;

/* Layout used by the first flashed childcare build, before timezone_index was
 * added.  It has the same total size, so state_load must inspect the field
 * offsets instead of relying on the NVS blob length alone. */
typedef struct {
    uint32_t magic;
    uint16_t milk_ml;
    uint16_t milk_count;
    uint8_t milk_ingredient;
    uint16_t diaper_count;
    uint16_t pee_count;
    uint16_t poop_count;
    uint16_t sleep_count;
    uint16_t bath_count;
    uint16_t tummy_count;
    uint16_t timer_count;
    uint16_t sleep_minutes;
    uint8_t brightness;
    uint8_t active;
    uint16_t timer_minutes;
    int64_t last_milk_epoch;
    int64_t last_diaper_epoch;
    int64_t sleep_start_epoch;
    int64_t sleep_end_epoch;
    uint8_t event_count;
    uint8_t event_head;
    x_event_t events[X_EVENT_CAPACITY];
} x_legacy_persisted_t;

typedef struct {
    x_persisted_t data;
    int battery_soc;
    int64_t active_started_us;
    bool active_time_known;
} x_state_t;

#define X_UNDO_WINDOW_US 5000000LL
#define X_COMMAND_ID_MAX 40
#define X_MIN_VALID_EPOCH 1700000000LL

typedef struct {
    x_persisted_t data;
    int64_t active_started_us;
    int64_t expires_us;
    bool active_time_known;
    bool valid;
} x_undo_t;

static const uint32_t X_STATE_MAGIC = 0x58494741U;
static const char *X_NVS_NAMESPACE = "xigua";

static lv_obj_t *s_screen;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;
static lv_obj_t *s_battery;
static lv_obj_t *s_ai_panel;
static lv_obj_t *s_ai_text;
static lv_obj_t *s_ai_cards[3];
static lv_obj_t *s_home_panel;
static lv_obj_t *s_home_summary;
static lv_obj_t *s_home_cards[3];
static x_ai_view_t s_ai_rendered_view = (x_ai_view_t)-1;
static lv_timer_t *s_timer;

static SemaphoreHandle_t s_mutex;
static nvs_handle_t s_nvs;
static bool s_nvs_open;
static x_state_t s_state;
static x_page_t s_page = X_PAGE_OVERVIEW;
static size_t s_focus;
static size_t s_feed_ml;
static size_t s_feed_ingredient;
static size_t s_timer_focus;
static bool s_confirm_abort;
static bool s_abort_choice;
static bool s_running;
static char s_feedback[64];
static char s_ai_reply[XIGUA_AI_REPLY_BYTES];
static char s_ai_error[64];
static bool s_ai_truncated;
static x_ai_ui_t s_ai_ui = { .view = X_AI_READY, .pages = 1 };
static int64_t s_ai_record_started_us;
static bool s_ai_request_pending;
static char s_last_command_id[X_COMMAND_ID_MAX];
static x_undo_t s_undo;
static x_wifi_ui_mode_t s_wifi_ui_mode = X_WIFI_UI_MENU;
static char s_wifi_input_ssid[33];
static char s_wifi_input_password[65];
static size_t s_wifi_input_len;
static x_wifi_keyboard_mode_t s_wifi_keyboard_mode = X_WIFI_KEY_UPPER;
static size_t s_wifi_keyboard_index;
static size_t s_wifi_keyboard_page;
static xigua_keyboard_input_t s_wifi_nav;
static lv_obj_t *s_wifi_keyboard;
static lv_obj_t *s_wifi_password;
static lv_obj_t *s_wifi_key_labels[XIGUA_KEYBOARD_PAGE_KEYS + XIGUA_KEYBOARD_ACTIONS];
static size_t s_wifi_scan_index;

static const char *const WIFI_KEYBOARD_CHARS[] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "abcdefghijklmnopqrstuvwxyz",
    "0123456789 !@#$%^&*()-_=+[]{};:'\\\",.<>/?\\|`~",
};
static const char *const WIFI_KEYBOARD_ACTIONS[] = {
    "大写", "小写", "数字", "退格", "换页", "完成",
};

static const uint16_t TIMER_OPTIONS[] = { 5, 10, 20, 30 };
static const char *const TIMEZONE_NAMES[] = {
    "中国 +08:00", "UTC +00:00", "东京 +09:00", "纽约 -05:00"
};
static const char *const TIMEZONE_VALUES[] = {
    "CST-8", "UTC0", "JST-9", "EST5EDT,M3.2.0/2,M11.1.0/2"
};
static const char *const FEED_INGREDIENTS[] = { "奶粉", "母乳", "辅食", "其他" };
static const char *const ACTIVE_NAMES[] = {
    "", "睡眠", "洗澡", "趴玩", "计时"
};
static const char *const HOME_ITEMS[] = {
    "AI助手", "手动记录", "今天", "睡眠", "声音", "Wi-Fi配网", "设置"
};

static bool valid_timer_minutes(uint16_t minutes)
{
    for (size_t i = 0; i < sizeof(TIMER_OPTIONS) / sizeof(TIMER_OPTIONS[0]); i++) {
        if (minutes == TIMER_OPTIONS[i]) return true;
    }
    return false;
}

static bool wall_clock_known(void)
{
    return time(NULL) >= X_MIN_VALID_EPOCH;
}

static void state_defaults(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.data.magic = X_STATE_MAGIC;
    s_state.data.milk_ml = 150;
    s_state.data.milk_ingredient = 0;
    s_state.data.brightness = 80;
    s_state.data.timezone_index = 0;
    s_state.data.timer_minutes = TIMER_OPTIONS[0];
    s_state.battery_soc = -1;
}

static void apply_timezone(void)
{
    size_t count = sizeof(TIMEZONE_VALUES) / sizeof(TIMEZONE_VALUES[0]);
    if (s_state.data.timezone_index >= count) s_state.data.timezone_index = 0;
    setenv("TZ", TIMEZONE_VALUES[s_state.data.timezone_index], 1);
    tzset();
}

static esp_err_t state_save_command_id(const char *command_id)
{
    if (!s_nvs_open || !s_mutex) return ESP_ERR_INVALID_STATE;
    x_persisted_t data;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return ESP_ERR_TIMEOUT;
    data = s_state.data;
    xSemaphoreGive(s_mutex);
    esp_err_t err = nvs_set_blob(s_nvs, "state", &data, sizeof(data));
    if (err == ESP_OK && command_id && command_id[0] != '\0') {
        err = nvs_set_str(s_nvs, "last_cmd_id", command_id);
    }
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGW(TAG, "state save failed: %s", esp_err_to_name(err));
    return err;
}

static void state_save(void)
{
    (void)state_save_command_id(NULL);
}

static void load_last_command_id(void)
{
    s_last_command_id[0] = '\0';
    if (!s_nvs_open) return;
    size_t size = sizeof(s_last_command_id);
    if (nvs_get_str(s_nvs, "last_cmd_id", s_last_command_id, &size) != ESP_OK ||
        size == 0 || size > sizeof(s_last_command_id)) {
        s_last_command_id[0] = '\0';
    }
}

static bool undo_available(void)
{
    if (!s_undo.valid) return false;
    if (esp_timer_get_time() >= s_undo.expires_us) {
        s_undo.valid = false;
        return false;
    }
    return true;
}

static void undo_capture_locked(void)
{
    s_undo.data = s_state.data;
    s_undo.active_started_us = s_state.active_started_us;
    s_undo.active_time_known = s_state.active_time_known;
    s_undo.expires_us = esp_timer_get_time() + X_UNDO_WINDOW_US;
    s_undo.valid = true;
}

static void undo_clear(void)
{
    s_undo.valid = false;
    s_undo.expires_us = 0;
}

static bool undo_last(void)
{
    if (!undo_available() || !s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return false;
    if (!undo_available()) {
        xSemaphoreGive(s_mutex);
        return false;
    }
    s_state.data = s_undo.data;
    s_state.active_started_us = s_undo.active_started_us;
    s_state.active_time_known = s_undo.active_time_known;
    xSemaphoreGive(s_mutex);
    state_save();
    undo_clear();
    snprintf(s_feedback, sizeof(s_feedback), "最近记录已撤销");
    return true;
}

static void append_event_locked(x_event_type_t type, uint16_t amount,
                                uint16_t duration_min, uint8_t ingredient,
                                int64_t epoch)
{
    x_persisted_t *data = &s_state.data;
    uint8_t slot = data->event_head;
    data->events[slot] = (x_event_t){
        .epoch = epoch > 0 ? epoch : (int64_t)time(NULL),
        .amount = amount,
        .duration_min = duration_min,
        .type = (uint8_t)type,
        .ingredient = ingredient,
    };
    data->event_head = (uint8_t)((slot + 1) % X_EVENT_CAPACITY);
    if (data->event_count < X_EVENT_CAPACITY) data->event_count++;
}

static void event_totals(const x_persisted_t *data, uint16_t *feed_count,
                         uint32_t *milk_ml, uint16_t *pee, uint16_t *poop,
                         uint16_t *sleep_count, uint16_t *sleep_minutes,
                         uint16_t *bath_count, uint16_t *tummy_count,
                         uint16_t *timer_count)
{
    *feed_count = 0;
    *milk_ml = 0;
    *pee = 0;
    *poop = 0;
    *sleep_count = 0;
    *sleep_minutes = 0;
    *bath_count = 0;
    *tummy_count = 0;
    *timer_count = 0;
    time_t now = time(NULL);
    struct tm today = { 0 };
    bool have_today = now >= X_MIN_VALID_EPOCH && localtime_r(&now, &today) != NULL;
    for (uint8_t i = 0; i < data->event_count; i++) {
        const x_event_t *event = &data->events[i];
        if (!have_today || event->epoch < X_MIN_VALID_EPOCH) continue;
        time_t event_time = (time_t)event->epoch;
        struct tm event_day = { 0 };
        if (localtime_r(&event_time, &event_day) != NULL &&
            (event_day.tm_year != today.tm_year || event_day.tm_yday != today.tm_yday)) {
            continue;
        }
        switch ((x_event_type_t)event->type) {
        case X_EVENT_FEED:
            (*feed_count)++;
            *milk_ml += event->amount;
            break;
        case X_EVENT_PEE: (*pee)++; break;
        case X_EVENT_POOP: (*poop)++; break;
        case X_EVENT_SLEEP:
            (*sleep_count)++;
            *sleep_minutes += event->duration_min;
            break;
        case X_EVENT_BATH: (*bath_count)++; break;
        case X_EVENT_TUMMY: (*tummy_count)++; break;
        case X_EVENT_TIMER: (*timer_count)++; break;
        default: break;
        }
    }
    if (data->event_count == 0 && have_today) {
        *feed_count = data->milk_count;
        *milk_ml = (uint32_t)data->milk_count * data->milk_ml;
        *pee = data->pee_count;
        *poop = data->poop_count;
        *sleep_count = data->sleep_count;
        *sleep_minutes = data->sleep_minutes;
        *bath_count = data->bath_count;
        *tummy_count = data->tummy_count;
        *timer_count = data->timer_count;
    }
}

static int64_t json_event_time(cJSON *object, const char *key)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(value) && value->valuedouble >= 0 && value->valuedouble <= INT64_MAX) {
        return (int64_t)value->valuedouble;
    }
    if (cJSON_IsString(value) && value->valuestring) {
        int hour = -1;
        int minute = -1;
        if (sscanf(value->valuestring, "%d:%d", &hour, &minute) == 2 &&
            hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
            time_t now = time(NULL);
            struct tm local_time;
            if (now >= 1700000000 && localtime_r(&now, &local_time) != NULL) {
                local_time.tm_hour = hour;
                local_time.tm_min = minute;
                local_time.tm_sec = 0;
                return (int64_t)mktime(&local_time);
            }
        }
    }
    return (int64_t)time(NULL);
}

static uint8_t ingredient_from_json(cJSON *value)
{
    if (!cJSON_IsString(value) || !value->valuestring) return 3;
    for (uint8_t i = 0; i < sizeof(FEED_INGREDIENTS) / sizeof(FEED_INGREDIENTS[0]); i++) {
        if (strcasecmp(value->valuestring, FEED_INGREDIENTS[i]) == 0) return i;
    }
    if (strcasecmp(value->valuestring, "milk_powder") == 0 ||
        strcasecmp(value->valuestring, "formula_milk") == 0 ||
        strcmp(value->valuestring, "奶粉") == 0) return 0;
    if (strcasecmp(value->valuestring, "breast_milk") == 0 ||
        strcmp(value->valuestring, "母乳") == 0) return 1;
    if (strcasecmp(value->valuestring, "solid_food") == 0 ||
        strcmp(value->valuestring, "辅食") == 0) return 2;
    return 3;
}

static bool json_number_in_range(cJSON *object, const char *key, int min, int max, int *out)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value) || value->valuedouble < min || value->valuedouble > max) return false;
    *out = value->valueint;
    return true;
}

static bool apply_ai_action_locked(cJSON *action)
{
    cJSON *name = cJSON_GetObjectItemCaseSensitive(action, "action");
    if (!cJSON_IsString(name) || !name->valuestring) return false;
    const char *action_name = name->valuestring;
    int amount = 0;
    int64_t event_time = cJSON_GetObjectItemCaseSensitive(action, "timestamp") ?
        json_event_time(action, "timestamp") : json_event_time(action, "time");
    if (strcmp(action_name, "record_feeding") == 0) {
        if (!json_number_in_range(action, "amount_ml", 10, 400, &amount)) return false;
        uint8_t ingredient = ingredient_from_json(cJSON_GetObjectItemCaseSensitive(action, "ingredient"));
        s_state.data.milk_ml = (uint16_t)amount;
        s_state.data.milk_count++;
        s_state.data.milk_ingredient = ingredient;
        s_state.data.last_milk_epoch = event_time;
        append_event_locked(X_EVENT_FEED, (uint16_t)amount, 0, ingredient, event_time);
        return true;
    }
    if (strcmp(action_name, "record_diaper") == 0) {
        cJSON *kind = cJSON_GetObjectItemCaseSensitive(action, "kind");
        if (!cJSON_IsString(kind) || !kind->valuestring) return false;
        bool poop = strcasecmp(kind->valuestring, "poop") == 0 ||
                    strcasecmp(kind->valuestring, "stool") == 0 ||
                    strcmp(kind->valuestring, "便") == 0;
        bool pee = strcasecmp(kind->valuestring, "pee") == 0 ||
                   strcasecmp(kind->valuestring, "urine") == 0 ||
                   strcmp(kind->valuestring, "尿") == 0;
        if (!poop && !pee) return false;
        s_state.data.diaper_count++;
        if (poop) s_state.data.poop_count++; else s_state.data.pee_count++;
        s_state.data.last_diaper_epoch = event_time;
        append_event_locked(poop ? X_EVENT_POOP : X_EVENT_PEE, 0, 0, 0, event_time);
        return true;
    }
    if (strcmp(action_name, "record_sleep") == 0) {
        int duration = 0;
        if (!json_number_in_range(action, "duration_min", 0, 65535, &duration)) return false;
        cJSON *start = cJSON_GetObjectItemCaseSensitive(action, "start_timestamp");
        cJSON *end = cJSON_GetObjectItemCaseSensitive(action, "end_timestamp");
        s_state.data.sleep_count++;
        s_state.data.sleep_minutes += (uint16_t)duration;
        s_state.data.sleep_start_epoch = cJSON_IsNumber(start) ? (int64_t)start->valuedouble : event_time;
        s_state.data.sleep_end_epoch = cJSON_IsNumber(end) ? (int64_t)end->valuedouble : event_time;
        append_event_locked(X_EVENT_SLEEP, 0, (uint16_t)duration, 0, event_time);
        return true;
    }
    if (strcmp(action_name, "record_bath") == 0 || strcmp(action_name, "record_tummy") == 0) {
        bool bath = strcmp(action_name, "record_bath") == 0;
        if (bath) s_state.data.bath_count++; else s_state.data.tummy_count++;
        append_event_locked(bath ? X_EVENT_BATH : X_EVENT_TUMMY, 0, 0, 0, event_time);
        return true;
    }
    return false;
}

static void state_load(void)
{
    state_defaults();
    if (!s_nvs_open) return;
    uint8_t raw[sizeof(x_persisted_t)] = { 0 };
    size_t size = sizeof(raw);
    if (nvs_get_blob(s_nvs, "state", raw, &size) == ESP_OK && size == sizeof(raw)) {
        x_persisted_t data;
        x_legacy_persisted_t legacy;
        memcpy(&data, raw, sizeof(data));
        memcpy(&legacy, raw, sizeof(legacy));
        bool new_valid = data.magic == X_STATE_MAGIC &&
                         data.timezone_index < sizeof(TIMEZONE_VALUES) / sizeof(TIMEZONE_VALUES[0]) &&
                         data.active <= X_ACTIVE_TIMER && valid_timer_minutes(data.timer_minutes);
        bool legacy_valid = legacy.magic == X_STATE_MAGIC &&
                            legacy.active <= X_ACTIVE_TIMER &&
                            valid_timer_minutes(legacy.timer_minutes);
        if (!new_valid && legacy_valid) {
            data.magic = legacy.magic;
            data.milk_ml = legacy.milk_ml;
            data.milk_count = legacy.milk_count;
            data.milk_ingredient = legacy.milk_ingredient;
            data.diaper_count = legacy.diaper_count;
            data.pee_count = legacy.pee_count;
            data.poop_count = legacy.poop_count;
            data.sleep_count = legacy.sleep_count;
            data.bath_count = legacy.bath_count;
            data.tummy_count = legacy.tummy_count;
            data.timer_count = legacy.timer_count;
            data.sleep_minutes = legacy.sleep_minutes;
            data.brightness = legacy.brightness;
            data.timezone_index = 0;
            data.active = legacy.active;
            data.timer_minutes = legacy.timer_minutes;
            memcpy((uint8_t *)&data + offsetof(x_persisted_t, last_milk_epoch),
                   (const uint8_t *)&legacy + offsetof(x_legacy_persisted_t, last_milk_epoch),
                   sizeof(data) - offsetof(x_persisted_t, last_milk_epoch));
            ESP_LOGI(TAG, "migrating persisted state to timezone-aware layout");
        }
        if ((new_valid || legacy_valid) && data.magic == X_STATE_MAGIC) {
            s_state.data = data;
            if (s_state.data.milk_ml < 10 || s_state.data.milk_ml > 400) s_state.data.milk_ml = 150;
            if (s_state.data.brightness < 20 || s_state.data.brightness > 100) s_state.data.brightness = 80;
            if (s_state.data.timezone_index >= sizeof(TIMEZONE_VALUES) / sizeof(TIMEZONE_VALUES[0])) {
                s_state.data.timezone_index = 0;
            }
            if (s_state.data.milk_ingredient >= (sizeof(FEED_INGREDIENTS) / sizeof(FEED_INGREDIENTS[0]))) {
                s_state.data.milk_ingredient = 0;
            }
            if (s_state.data.event_count > X_EVENT_CAPACITY || s_state.data.event_head >= X_EVENT_CAPACITY) {
                s_state.data.event_count = 0;
                s_state.data.event_head = 0;
            }
            if (!new_valid && legacy_valid) state_save();
        }
    }
    apply_timezone();
}

static const char *active_name(x_active_t active)
{
    return active <= X_ACTIVE_TIMER ? ACTIVE_NAMES[active] : "ACTIVE";
}

static const char *wifi_state_name(xigua_wifi_state_t state)
{
    switch (state) {
    case XIGUA_WIFI_CONNECTED: return "已连接";
    case XIGUA_WIFI_CONNECTING: return "连接中";
    case XIGUA_WIFI_READY: return "等待选择网络";
    case XIGUA_WIFI_STARTING: return "启动中";
    case XIGUA_WIFI_FAILED: return "连接失败";
    default: return "未连接";
    }
}

static const char *ai_health_name(xigua_ai_health_t health)
{
    switch (health) {
    case XIGUA_AI_HEALTH_CHECKING: return "自检中";
    case XIGUA_AI_HEALTH_NETWORK_FAILED: return "网络不可用";
    case XIGUA_AI_HEALTH_MODEL_FAILED: return "模型不可用";
    case XIGUA_AI_HEALTH_READY: return "网络和模型正常";
    default: return "等待联网";
    }
}

static void wifi_ui_clear_input(void)
{
    memset(s_wifi_input_ssid, 0, sizeof(s_wifi_input_ssid));
    memset(s_wifi_input_password, 0, sizeof(s_wifi_input_password));
    s_wifi_input_len = 0;
    s_wifi_keyboard_mode = X_WIFI_KEY_UPPER;
    s_wifi_keyboard_index = 0;
    s_wifi_keyboard_page = 0;
    s_wifi_scan_index = 0;
}

static void wifi_ui_enter_menu(void)
{
    wifi_ui_clear_input();
    s_wifi_ui_mode = X_WIFI_UI_MENU;
    s_focus = 0;
}

static void wifi_ui_begin_password(void)
{
    s_wifi_ui_mode = X_WIFI_UI_EDIT_PASSWORD;
    s_wifi_input_len = 0;
    s_wifi_keyboard_mode = X_WIFI_KEY_UPPER;
    s_wifi_keyboard_index = 0;
    s_wifi_keyboard_page = 0;
    memset(&s_wifi_nav, 0, sizeof(s_wifi_nav));
    memset(s_wifi_input_password, 0, sizeof(s_wifi_input_password));
}

static void wifi_ui_start_scan(void)
{
    s_wifi_scan_index = 0;
    s_wifi_ui_mode = X_WIFI_UI_SCAN;
    if (xigua_wifi_scan() != ESP_OK) {
        snprintf(s_feedback, sizeof(s_feedback), "扫描无法开始，请重试");
    } else {
        snprintf(s_feedback, sizeof(s_feedback), "正在扫描附近 Wi-Fi");
    }
}

static void wifi_ui_select_scan(void)
{
    size_t count = xigua_wifi_scan_count();
    if (count == 0 || s_wifi_scan_index >= count) {
        snprintf(s_feedback, sizeof(s_feedback), "无可用 Wi-Fi，请重新扫描");
        return;
    }
    snprintf(s_wifi_input_ssid, sizeof(s_wifi_input_ssid), "%s",
             xigua_wifi_scan_ssid(s_wifi_scan_index));
    wifi_ui_begin_password();
    snprintf(s_feedback, sizeof(s_feedback), "请输入 %s 的密码", s_wifi_input_ssid);
}

static const char *wifi_keyboard_chars(void)
{
    return WIFI_KEYBOARD_CHARS[s_wifi_keyboard_mode];
}

static void wifi_ui_append_char(char value)
{
    size_t limit = sizeof(s_wifi_input_password) - 1;
    if (s_wifi_input_len >= limit) {
        snprintf(s_feedback, sizeof(s_feedback), "已达到长度上限");
        return;
    }
    char *buffer = s_wifi_input_password;
    buffer[s_wifi_input_len++] = value;
    buffer[s_wifi_input_len] = '\0';
}

static void wifi_ui_backspace(void)
{
    char *buffer = s_wifi_input_password;
    if (s_wifi_input_len == 0) return;
    buffer[--s_wifi_input_len] = '\0';
}

static void wifi_ui_finish_edit(void)
{
    if (s_wifi_input_ssid[0] == '\0') {
        snprintf(s_feedback, sizeof(s_feedback), "请先扫描并选择 Wi-Fi");
        return;
    }
    esp_err_t err = xigua_wifi_set_credentials(s_wifi_input_ssid, s_wifi_input_password);
    if (err == ESP_OK) err = xigua_wifi_connect();
    s_wifi_ui_mode = X_WIFI_UI_STATUS;
    if (err == ESP_OK) snprintf(s_feedback, sizeof(s_feedback), "正在连接已选 Wi-Fi");
    else snprintf(s_feedback, sizeof(s_feedback), "连接启动失败：%s", esp_err_to_name(err));
    memset(s_wifi_input_password, 0, sizeof(s_wifi_input_password));
}

static void wifi_ui_choose_key(void)
{
    memset(&s_wifi_nav, 0, sizeof(s_wifi_nav));
    const char *chars = wifi_keyboard_chars();
    size_t char_count = strlen(chars);
    if (s_wifi_keyboard_index < char_count) {
        wifi_ui_append_char(chars[s_wifi_keyboard_index]);
        return;
    }
    size_t action = s_wifi_keyboard_index - char_count;
    switch (action) {
    case 0:
        s_wifi_keyboard_mode = X_WIFI_KEY_UPPER;
        s_wifi_keyboard_index = 0;
        s_wifi_keyboard_page = 0;
        break;
    case 1:
        s_wifi_keyboard_mode = X_WIFI_KEY_LOWER;
        s_wifi_keyboard_index = 0;
        s_wifi_keyboard_page = 0;
        break;
    case 2:
        s_wifi_keyboard_mode = X_WIFI_KEY_DIGIT_SYMBOL;
        s_wifi_keyboard_index = 0;
        s_wifi_keyboard_page = 0;
        break;
    case 3:
        wifi_ui_backspace();
        break;
    case 4:
        s_wifi_keyboard_page = (s_wifi_keyboard_page + 1) %
            xigua_keyboard_page_count(char_count);
        s_wifi_keyboard_index = s_wifi_keyboard_page * XIGUA_KEYBOARD_PAGE_KEYS;
        break;
    case 5:
        wifi_ui_finish_edit();
        break;
    default:
        break;
    }
}

static const char *format_clock(int64_t epoch, char *out, size_t size)
{
    if (epoch < 1700000000LL) {
        snprintf(out, size, "时间未知");
        return out;
    }
    time_t raw = (time_t)epoch;
    struct tm local_time;
    if (localtime_r(&raw, &local_time) == NULL || strftime(out, size, "%H:%M", &local_time) == 0) {
        snprintf(out, size, "时间未知");
    }
    return out;
}

static void format_elapsed(int64_t started_us, char *out, size_t size)
{
    if (started_us <= 0) {
        snprintf(out, size, "--:--");
        return;
    }
    int64_t seconds = (esp_timer_get_time() - started_us) / 1000000;
    if (seconds < 0) seconds = 0;
    snprintf(out, size, "%02lld:%02lld", (long long)(seconds / 3600),
             (long long)((seconds / 60) % 60));
}

static void ui_label_style(lv_obj_t *label, uint32_t color, uint16_t size)
{
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    const lv_font_t *font = size >= 18 ? &s_xigua_font20 : &s_xigua_font;
    lv_obj_set_style_text_font(label, s_xigua_font_ready ? font : &xigua_font_zh16,
                               LV_PART_MAIN);
}

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w, int h,
                            const char *text, uint32_t color, uint16_t size)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    ui_label_style(label, color, size);
    lv_label_set_text(label, text);
    return label;
}

static void ui_shell(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    s_title = make_label(s_screen, 16, 10, 180, 28, "西瓜助手", 0xFFFFFF, 20);
    s_battery = make_label(s_screen, 188, 12, 40, 22, "--%", 0xB9C7D1, 14);
    s_body = make_label(s_screen, 18, 54, 204, 190, "正在加载…", 0xFFFFFF, 20);
    s_status = make_label(s_screen, 18, 246, 204, 28, "", 0x69D2E7, 14);
    s_hint = make_label(s_screen, 14, 274, 212, 42, "上/下选择  确认键打开", 0xB9C7D1, 14);
    lv_screen_load(s_screen);
}

static void ui_set_title(const char *title)
{
    if (s_title) lv_label_set_text(s_title, title);
}

static void ui_set_hint(const char *hint)
{
    if (s_hint) lv_label_set_text(s_hint, hint);
}

static void ui_set_body_font(uint16_t size)
{
    if (!s_body) return;
    const lv_font_t *font = size >= 18 ? &s_xigua_font20 : &s_xigua_font;
    lv_obj_set_style_text_font(s_body,
                               s_xigua_font_ready ? font : &xigua_font_zh16,
                               LV_PART_MAIN);
}

static void wifi_ui_create_keyboard(void)
{
    s_wifi_keyboard = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_wifi_keyboard);
    lv_obj_remove_flag(s_wifi_keyboard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_wifi_keyboard, 18, 78);
    lv_obj_set_size(s_wifi_keyboard, 204, 190);
    s_wifi_password = make_label(s_wifi_keyboard, 0, 0, 204, 22, "", 0xFFFFFF, 16);
    for (size_t i = 0; i < XIGUA_KEYBOARD_PAGE_KEYS + XIGUA_KEYBOARD_ACTIONS; ++i) {
        bool action = i >= XIGUA_KEYBOARD_PAGE_KEYS;
        size_t slot = action ? i - XIGUA_KEYBOARD_PAGE_KEYS : i;
        int x = action ? (int)(slot % 3) * 68 : (int)(slot % 6) * 34;
        int y = action ? 136 + (int)(slot / 3) * 26 : 24 + (int)(slot / 6) * 22;
        lv_obj_t *key = make_label(s_wifi_keyboard, x, y, action ? 64 : 30,
                                   action ? 24 : 20, "", 0xFFFFFF, 16);
        lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(key, 1, 0);
        lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(key, 4, 0);
        s_wifi_key_labels[i] = key;
    }
    lv_obj_add_flag(s_wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_ui_render_keyboard(void)
{
    const char *characters = wifi_keyboard_chars();
    size_t count = strlen(characters);
    size_t start = s_wifi_keyboard_page * XIGUA_KEYBOARD_PAGE_KEYS;
    char masked[7] = {0};
    size_t shown = s_wifi_input_len > 6 ? 6 : s_wifi_input_len;
    memset(masked, '*', shown);
    lv_label_set_text_fmt(s_wifi_password, "密码：%s  %u/64  %u/%u",
                          shown ? masked : "(空)", (unsigned)s_wifi_input_len,
                          (unsigned)s_wifi_keyboard_page + 1,
                          (unsigned)xigua_keyboard_page_count(count));
    for (size_t i = 0; i < XIGUA_KEYBOARD_PAGE_KEYS + XIGUA_KEYBOARD_ACTIONS; ++i) {
        lv_obj_t *key = s_wifi_key_labels[i];
        size_t index = i < XIGUA_KEYBOARD_PAGE_KEYS ? start + i :
            count + i - XIGUA_KEYBOARD_PAGE_KEYS;
        if (i < XIGUA_KEYBOARD_PAGE_KEYS && index >= count) {
            lv_obj_add_flag(key, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(key, LV_OBJ_FLAG_HIDDEN);
        if (i >= XIGUA_KEYBOARD_PAGE_KEYS) {
            lv_label_set_text(key, WIFI_KEYBOARD_ACTIONS[i - XIGUA_KEYBOARD_PAGE_KEYS]);
        } else if (characters[index] == ' ') {
            lv_label_set_text(key, "空");
        } else {
            char value[] = { characters[index], '\0' };
            lv_label_set_text(key, value);
        }
        bool selected = index == s_wifi_keyboard_index;
        bool mode = i >= XIGUA_KEYBOARD_PAGE_KEYS &&
            i - XIGUA_KEYBOARD_PAGE_KEYS == (size_t)s_wifi_keyboard_mode;
        lv_obj_set_style_bg_color(key, lv_color_hex(selected ? 0x69D2E7 :
                                                   mode ? 0x375B82 : 0x23405A), 0);
        lv_obj_set_style_text_color(key, lv_color_hex(selected ? 0x102332 : 0xFFFFFF), 0);
        lv_obj_set_style_border_width(key, 0, 0);
        lv_obj_set_style_outline_width(key, selected ? 2 : 0, 0);
        lv_obj_set_style_outline_color(key, lv_color_hex(0xFFFFFF), 0);
    }
}

static bool ai_request_voice_current_mode(void)
{
    if (!xigua_ai_configured()) {
        snprintf(s_feedback, sizeof(s_feedback), "AI 尚未配置");
        return false;
    }
    if (s_ai_request_pending) {
        snprintf(s_feedback, sizeof(s_feedback), "语音请求进行中");
        return false;
    }
    if (xigua_wifi_state() != XIGUA_WIFI_CONNECTED) {
        snprintf(s_feedback, sizeof(s_feedback), "Wi-Fi未连接，请先完成配网");
        return false;
    }
    if (xigua_ai_request_voice() != ESP_OK) {
        snprintf(s_feedback, sizeof(s_feedback), "录音启动失败");
        return false;
    }
    s_ai_request_pending = true;
    s_ai_record_started_us = esp_timer_get_time();
    snprintf(s_feedback, sizeof(s_feedback), "正在录音，松开确认键结束（最长60秒）");
    return true;
}

static lv_obj_t *ui_menu_card(lv_obj_t *parent, int y)
{
    lv_obj_t *card = make_label(parent, 0, y, 204, 38, "", 0xFFFFFF, 20);
    lv_obj_set_style_pad_left(card, 10, 0);
    lv_obj_set_style_pad_top(card, 6, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 6, 0);
    return card;
}

static void ui_menu_focus(lv_obj_t *card, bool selected)
{
    lv_obj_set_style_bg_color(card, lv_color_hex(selected ? 0x69D2E7 : 0x23405A), 0);
    lv_obj_set_style_text_color(card, lv_color_hex(selected ? 0x102332 : 0xFFFFFF), 0);
    lv_obj_set_style_outline_width(card, selected ? 2 : 0, 0);
    lv_obj_set_style_outline_color(card, lv_color_hex(0xFFFFFF), 0);
}

static void ui_home_render(uint16_t milk_ml, uint16_t sleep, unsigned diaper)
{
    if (!s_home_panel) {
        s_home_panel = lv_obj_create(s_screen);
        lv_obj_remove_style_all(s_home_panel);
        lv_obj_remove_flag(s_home_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_home_panel, 18, 54);
        lv_obj_set_size(s_home_panel, 204, 190);
        s_home_summary = make_label(s_home_panel, 0, 0, 204, 46, "", 0xB9C7D1, 16);
        for (size_t i = 0; i < 3; ++i) s_home_cards[i] = ui_menu_card(s_home_panel, 54 + (int)i * 44);
    }
    lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(s_home_summary, "奶量 %u 毫升\n睡眠 %u 次  尿便 %u 次",
                          milk_ml, sleep, diaper);
    size_t first = xigua_menu_first(s_focus, 3);
    for (size_t i = 0; i < 3; ++i) {
        if (first + i >= sizeof(HOME_ITEMS) / sizeof(HOME_ITEMS[0])) {
            lv_obj_add_flag(s_home_cards[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_home_cards[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_home_cards[i], HOME_ITEMS[first + i]);
        ui_menu_focus(s_home_cards[i], first + i == s_focus);
    }
    if (!s_feedback[0]) lv_label_set_text_fmt(s_status, "菜单 %u / 3", (unsigned)(first / 3 + 1));
    ui_set_hint("上/下选择  确认打开\n长按上键快速喂奶");
}

static void ui_ai_render(void)
{
    if (!s_ai_panel) {
        s_ai_panel = lv_obj_create(s_screen);
        lv_obj_remove_style_all(s_ai_panel);
        lv_obj_remove_flag(s_ai_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_ai_panel, 18, 54);
        lv_obj_set_size(s_ai_panel, 204, 190);
        s_ai_rendered_view = (x_ai_view_t)-1;
    }
    lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    bool cards = s_ai_ui.view == X_AI_READY || s_ai_ui.view == X_AI_ACTIONS;
    if (s_ai_rendered_view != s_ai_ui.view) {
        lv_obj_clean(s_ai_panel);
        memset(s_ai_cards, 0, sizeof(s_ai_cards));
        s_ai_text = make_label(s_ai_panel, 0, 0, 204, cards ? 46 : LV_SIZE_CONTENT,
                              "", 0xFFFFFF, cards ? 16 : 20);
        if (s_ai_ui.view == X_AI_READING) {
            lv_obj_set_style_text_font(s_ai_text, &xigua_font_full20, 0);
            lv_obj_set_style_text_line_space(s_ai_text, 0, 0);
            lv_label_set_text(s_ai_text, s_ai_reply);
            lv_point_t size;
            lv_text_get_size(&size, s_ai_reply, &xigua_font_full20, 0, 0, 204, LV_TEXT_FLAG_NONE);
            s_ai_ui.pages = x_ai_page_count((size_t)size.y, 190);
            if (s_ai_ui.page >= s_ai_ui.pages) s_ai_ui.page = s_ai_ui.pages - 1;
        }
        if (cards) {
            for (size_t i = 0; i < 3; ++i) {
                s_ai_cards[i] = ui_menu_card(s_ai_panel, 54 + (int)i * 44);
            }
        }
        s_ai_rendered_view = s_ai_ui.view;
    }
    char progress[160];
    ui_set_title(s_ai_ui.view == X_AI_READING ? "AI回复" :
                 s_ai_ui.view == X_AI_ACTIONS ? "回复操作" : "AI助手");
    if (cards) {
        const char *const ready[] = { "长按确认说话", "查看上次回复", "返回" };
        const char *const actions[] = { "继续查看", "再问一次", "返回" };
        lv_label_set_text(s_ai_text, s_ai_ui.view == X_AI_READY ?
                          "长按确认说话\n松开结束录音" : "请选择操作\n回复仍可查看");
        for (size_t i = 0; i < 3; ++i) {
            lv_obj_t *card = s_ai_cards[i];
            lv_label_set_text(card, s_ai_ui.view == X_AI_READY ? ready[i] : actions[i]);
            ui_menu_focus(card, i == s_ai_ui.focus);
        }
        lv_label_set_text(s_status, s_ai_ui.view == X_AI_ACTIONS ? "回复仍可查看" : s_feedback);
        ui_set_hint("上/下选择  确认打开\n长按下键返回");
    } else if (s_ai_ui.view == X_AI_READING) {
        lv_obj_set_y(s_ai_text, -(int32_t)(s_ai_ui.page * 190));
        lv_label_set_text_fmt(s_status, "%s %u / %u", s_ai_truncated ? "部分回复" : "回复",
                              (unsigned)s_ai_ui.page + 1, (unsigned)s_ai_ui.pages);
        ui_set_hint("上/下换页  确认打开操作\n长按下键返回");
    } else {
        if (s_ai_ui.view == X_AI_RECORDING) {
            unsigned seconds = (unsigned)((esp_timer_get_time() - s_ai_record_started_us) / 1000000);
            snprintf(progress, sizeof(progress), "正在录音\n\n%u 秒 / 60 秒\n\n松开确认键结束", seconds);
            ui_set_hint("松开结束录音\n请直接说话");
        } else if (s_ai_ui.view == X_AI_WAITING) {
            snprintf(progress, sizeof(progress), "%s\n\n请等待\n\n回复收到后可换页查看",
                     xigua_ai_voice_phase() == XIGUA_AI_VOICE_THINKING ? "正在等待回复" : "正在识别语音");
            ui_set_hint("处理中  确认键无操作\n长按下键返回");
        } else {
            snprintf(progress, sizeof(progress), "请求未完成\n\n%s\n\n确认返回，重新录音\n%s",
                     s_ai_error, s_ai_ui.has_reply ? "上次回复可查看" : "请重试");
            ui_set_hint("确认返回准备页\n长按下键返回");
        }
        lv_label_set_text(s_ai_text, progress);
        lv_label_set_text(s_status, "");
    }
}

static void ui_refresh_page(void)
{
    if (!s_body || !s_mutex) return;
    x_state_t snapshot;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    snapshot = s_state;
    xSemaphoreGive(s_mutex);
    char text[256];
    char elapsed[16];
    char last_clock[16];
    char start_clock[16];
    char wifi_ssid[33];
    char wifi_ip[16];
    uint16_t feed_count, pee_count, poop_count, sleep_count, sleep_minutes;
    uint16_t bath_count, tummy_count, timer_count;
    uint32_t milk_total;
    event_totals(&snapshot.data, &feed_count, &milk_total, &pee_count, &poop_count,
                 &sleep_count, &sleep_minutes, &bath_count, &tummy_count,
                 &timer_count);
    xigua_wifi_status(wifi_ssid, sizeof(wifi_ssid), wifi_ip, sizeof(wifi_ip));
    switch (s_page) {
    case X_PAGE_OVERVIEW:
        ui_set_title("西瓜助手");
        {
            snprintf(text, sizeof(text), "> %s\n\n奶量 %u 毫升（%u 次）\n睡眠 %u 次  尿便 %u 次\n进行中：%s",
                 HOME_ITEMS[s_focus],
                 snapshot.data.milk_ml, feed_count,
                 sleep_count, (unsigned)(pee_count + poop_count),
                 snapshot.data.active == X_ACTIVE_NONE ? "无" : active_name((x_active_t)snapshot.data.active));
        }
        ui_set_hint("上/下选择  确认键打开  长按上键快速喂奶");
        break;
    case X_PAGE_RECORD:
        ui_set_title("手动记录");
        snprintf(text, sizeof(text), "%c 喂养\n%c 睡眠\n%c 尿便\n%c 洗澡\n%c 趴玩\n%c 计时",
                 s_focus == 0 ? '>' : ' ', s_focus == 1 ? '>' : ' ', s_focus == 2 ? '>' : ' ',
                 s_focus == 3 ? '>' : ' ', s_focus == 4 ? '>' : ' ', s_focus == 5 ? '>' : ' ');
        ui_set_hint("上/下选择  确认键进入  长按下键返回");
        break;
    case X_PAGE_FEED:
        ui_set_title("喂养");
        snprintf(text, sizeof(text), "%s\n\n%u 毫升\n\n上次：%s %u 毫升  %s",
                 FEED_INGREDIENTS[s_feed_ingredient],
                 (unsigned)s_feed_ml,
                 FEED_INGREDIENTS[snapshot.data.milk_ingredient],
                 (unsigned)snapshot.data.milk_ml,
                 format_clock(snapshot.data.last_milk_epoch, last_clock, sizeof(last_clock)));
        ui_set_hint("上键减量  下键加量  长按上键选食材  确认键保存");
        break;
    case X_PAGE_DIAPER:
        ui_set_title("尿便");
        snprintf(text, sizeof(text), "%c 尿\n%c 便\n\n上次：%s",
                 s_focus == 0 ? '>' : ' ', s_focus == 1 ? '>' : ' ',
                 format_clock(snapshot.data.last_diaper_epoch, last_clock, sizeof(last_clock)));
        ui_set_hint("上/下选择  确认键保存  长按下键返回");
        break;
    case X_PAGE_ACTIVE:
        ui_set_title(active_name((x_active_t)snapshot.data.active));
        format_elapsed(snapshot.active_time_known ? snapshot.active_started_us : 0,
                       elapsed, sizeof(elapsed));
        if (s_confirm_abort) {
            snprintf(text, sizeof(text), "放弃%s？\n\n%s继续\n%s放弃",
                     active_name((x_active_t)snapshot.data.active),
                     s_abort_choice ? "  " : "> ",
                     s_abort_choice ? "> " : "  ");
            ui_set_hint("上/下选择  确认键确定  长按下键取消");
        } else {
            snprintf(text, sizeof(text), "进行中\n\n%s\n开始：%s\n确认键结束", elapsed,
                     format_clock(snapshot.data.sleep_start_epoch, start_clock, sizeof(start_clock)));
            ui_set_hint("确认键结束并保存  长按下键放弃");
        }
        break;
    case X_PAGE_TIMER:
        ui_set_title("计时");
        snprintf(text, sizeof(text), "倒计时\n\n%u 分钟\n\n预设 %u/%u",
                 (unsigned)TIMER_OPTIONS[s_timer_focus], (unsigned)(s_timer_focus + 1),
                 (unsigned)(sizeof(TIMER_OPTIONS) / sizeof(TIMER_OPTIONS[0])));
        ui_set_hint("上键上一个  下键下一个  确认键开始  长按下键返回");
        break;
    case X_PAGE_VOICE:
    case X_PAGE_STORY:
        text[0] = '\0';
        break;
    case X_PAGE_SOUND:
        ui_set_title("儿歌/白噪音");
        snprintf(text, sizeof(text), "%c 儿歌\n%c 白噪音\n%c 停止\n\n服务端流式待接入",
                 s_focus == 0 ? '>' : ' ', s_focus == 1 ? '>' : ' ', s_focus == 2 ? '>' : ' ');
        ui_set_hint("上/下选择  确认键播放/暂停  长按下键返回");
        break;
    case X_PAGE_TODAY:
        ui_set_title("今天");
        if (!wall_clock_known()) {
            snprintf(text, sizeof(text), "时间待校准\n\n校时后显示今日统计\n本地记录仍可继续保存");
        } else {
            snprintf(text, sizeof(text), "喂养 %u 次（%lu 毫升）\n睡眠 %u 次（%u 分钟）\n尿 %u  便 %u\n洗澡 %u\n趴玩 %u\n计时 %u",
                     feed_count, (unsigned long)milk_total,
                     sleep_count, sleep_minutes,
                     pee_count, poop_count,
                     bath_count, tummy_count, timer_count);
        }
        ui_set_hint("上/下查看  长按下键返回");
        break;
    case X_PAGE_SETTINGS:
        ui_set_title("设置");
        snprintf(text, sizeof(text), "%c Wi-Fi配网：%s\n  自检：%s\n%c 时区：%s\n%c 亮度：%u%%\n%c 清除Wi-Fi",
                 s_focus == 0 ? '>' : ' ', wifi_state_name(xigua_wifi_state()),
                 ai_health_name(xigua_ai_health()),
                 s_focus == 1 ? '>' : ' ', TIMEZONE_NAMES[snapshot.data.timezone_index],
                 s_focus == 2 ? '>' : ' ', snapshot.data.brightness,
                 s_focus == 3 ? '>' : ' ');
        ui_set_hint("上/下选择  确认键进入/修改  长按下键返回");
        break;
    case X_PAGE_WIFI:
        if (s_wifi_ui_mode == X_WIFI_UI_MENU) {
            const size_t item_count = 1;
            ui_set_title("Wi-Fi设置");
            int used = snprintf(text, sizeof(text), "状态：%s\n自检：%s\n\n",
                                wifi_state_name(xigua_wifi_state()),
                                ai_health_name(xigua_ai_health()));
            const char *labels[] = { "扫描附近 Wi-Fi" };
            for (size_t i = 0; i < item_count && used > 0 && (size_t)used < sizeof(text); i++) {
                used += snprintf(text + used, sizeof(text) - (size_t)used, "%c %s\n",
                                 s_focus == i ? '>' : ' ', labels[i]);
            }
            ui_set_hint("上/下选择  确认键进入  长按下键返回");
        } else if (s_wifi_ui_mode == X_WIFI_UI_SCAN) {
            ui_set_title("选择 Wi-Fi");
            if (xigua_wifi_scan_in_progress()) {
                snprintf(text, sizeof(text), "正在扫描附近网络…\n请等待");
            } else if (xigua_wifi_scan_count() == 0) {
                snprintf(text, sizeof(text), "无可用 Wi-Fi\n请重新扫描");
            } else {
                size_t first = (s_wifi_scan_index / 5) * 5;
                int used = snprintf(text, sizeof(text), "网络数 %u  页 %u/%u\n",
                                    (unsigned)xigua_wifi_scan_count(),
                                    (unsigned)(first / 5) + 1,
                                    (unsigned)((xigua_wifi_scan_count() + 4) / 5));
                for (size_t i = first; i < xigua_wifi_scan_count() && i < first + 5 &&
                     used > 0 && (size_t)used < sizeof(text); ++i) {
                    used += snprintf(text + used, sizeof(text) - (size_t)used,
                                     "%c %s (%d)\n", s_wifi_scan_index == i ? '>' : ' ',
                                     xigua_wifi_scan_ssid(i), xigua_wifi_scan_rssi(i));
                }
            }
            ui_set_hint("上/下选择  确认输入密码  长按下键返回");
        } else if (s_wifi_ui_mode == X_WIFI_UI_EDIT_PASSWORD) {
            ui_set_title("输入 Wi-Fi 密码");
            snprintf(text, sizeof(text), "SSID：%s", s_wifi_input_ssid);
            if (!s_wifi_keyboard) wifi_ui_create_keyboard();
            wifi_ui_render_keyboard();
            ui_set_hint("上/下移动 2次换行\n确认选择 长按确认完成");
        } else {
            ui_set_title("Wi-Fi状态");
            if (xigua_wifi_state() == XIGUA_WIFI_CONNECTED) {
                snprintf(text, sizeof(text), "已连接\n\nSSID：%s\nIP：%s", wifi_ssid, wifi_ip);
            } else {
                snprintf(text, sizeof(text), "%s\n\nSSID：%s\nIP：%s",
                         wifi_state_name(xigua_wifi_state()), wifi_ssid[0] ? wifi_ssid : "--",
                         wifi_ip[0] ? wifi_ip : "--");
            }
            ui_set_hint("确认返回 Wi-Fi 设置  长按下键返回设置");
        }
        break;
    }
    if (s_page == X_PAGE_OVERVIEW && undo_available()) {
        ui_set_hint("确认键撤销最近记录  上/下选择");
    }
    bool editing_wifi = s_page == X_PAGE_WIFI &&
        s_wifi_ui_mode == X_WIFI_UI_EDIT_PASSWORD;
    lv_obj_set_size(s_body, 204, editing_wifi ? 22 : 190);
    lv_label_set_long_mode(s_body, editing_wifi ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
    if (editing_wifi) {
        lv_obj_remove_flag(s_wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (s_wifi_keyboard) {
            lv_obj_delete(s_wifi_keyboard);
            s_wifi_keyboard = s_wifi_password = NULL;
            memset(s_wifi_key_labels, 0, sizeof(s_wifi_key_labels));
        }
        lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    }
    ui_set_body_font(s_page == X_PAGE_WIFI ? 16 : 20);
    lv_obj_remove_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_body, text);
    if (snapshot.battery_soc >= 0) lv_label_set_text_fmt(s_battery, "%d%%", snapshot.battery_soc);
    if (s_status) lv_label_set_text(s_status, s_feedback);
    if (s_page == X_PAGE_OVERVIEW) ui_home_render(snapshot.data.milk_ml, sleep_count,
                                               (unsigned)(pee_count + poop_count));
    else if (s_home_panel) {
        lv_obj_delete(s_home_panel);
        s_home_panel = s_home_summary = NULL;
        memset(s_home_cards, 0, sizeof(s_home_cards));
    }
    if (s_page == X_PAGE_VOICE || s_page == X_PAGE_STORY) ui_ai_render();
    else if (s_ai_panel) {
        lv_obj_delete(s_ai_panel);
        s_ai_panel = s_ai_text = NULL;
        memset(s_ai_cards, 0, sizeof(s_ai_cards));
        s_ai_rendered_view = (x_ai_view_t)-1;
    }
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    bool had_undo = s_undo.valid;
    bool undo_still_available = undo_available();
    if (s_page == X_PAGE_ACTIVE && !s_confirm_abort) ui_refresh_page();
    else if (had_undo && !undo_still_available && s_page == X_PAGE_OVERVIEW) ui_refresh_page();
    else if (s_page == X_PAGE_SETTINGS ||
             (s_page == X_PAGE_WIFI && (s_wifi_ui_mode == X_WIFI_UI_STATUS ||
                                        s_wifi_ui_mode == X_WIFI_UI_MENU ||
                                        s_wifi_ui_mode == X_WIFI_UI_SCAN))) ui_refresh_page();
    if (s_ai_request_pending && (s_page == X_PAGE_VOICE || s_page == X_PAGE_STORY)) {
        switch (xigua_ai_voice_phase()) {
        case XIGUA_AI_VOICE_RECORDING:
            snprintf(s_feedback, sizeof(s_feedback), "正在录音，松开确认键结束（最长60秒）");
            break;
        case XIGUA_AI_VOICE_TRANSCRIBING:
            s_ai_ui.view = X_AI_WAITING;
            snprintf(s_feedback, sizeof(s_feedback), "正在识别语音");
            break;
        case XIGUA_AI_VOICE_THINKING:
            s_ai_ui.view = X_AI_WAITING;
            snprintf(s_feedback, sizeof(s_feedback), "正在请求 Mimo");
            break;
        default:
            break;
        }
        ui_refresh_page();
    }
    static char ai_text[XIGUA_AI_REPLY_BYTES];
    esp_err_t ai_error = ESP_FAIL;
    bool truncated = false;
    if (xigua_ai_take_text(ai_text, sizeof(ai_text), &ai_error, &truncated)) {
        s_ai_request_pending = false;
        if (ai_error == ESP_OK) {
            s_ai_truncated = xigua_text_copy(s_ai_reply, sizeof(s_ai_reply), ai_text) || truncated;
            s_ai_rendered_view = (x_ai_view_t)-1;
        } else snprintf(s_ai_error, sizeof(s_ai_error), "%s", esp_err_to_name(ai_error));
        x_ai_complete(&s_ai_ui, ai_error == ESP_OK);
        snprintf(s_feedback, sizeof(s_feedback), ai_error == ESP_OK ? "Mimo 回复已收到" : "Mimo 请求失败");
        if (s_page == X_PAGE_VOICE || s_page == X_PAGE_STORY) ui_refresh_page();
    }
}

static void go_page(x_page_t page, size_t focus)
{
    s_page = page;
    s_focus = focus;
    s_confirm_abort = false;
    s_abort_choice = false;
}

static void ui_sync(void)
{
    if (bsp_lvgl_lock(100)) {
        ui_refresh_page();
        bsp_lvgl_unlock();
    }
}

static void save_simple_record(x_active_t active)
{
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
    undo_capture_locked();
    if (active == X_ACTIVE_NONE) s_state.data.diaper_count++;
    else if (active == X_ACTIVE_BATH) s_state.data.bath_count++;
    else if (active == X_ACTIVE_TUMMY) s_state.data.tummy_count++;
    if (active == X_ACTIVE_BATH) append_event_locked(X_EVENT_BATH, 0, 0, 0, (int64_t)time(NULL));
    else if (active == X_ACTIVE_TUMMY) append_event_locked(X_EVENT_TUMMY, 0, 0, 0, (int64_t)time(NULL));
    s_state.data.magic = X_STATE_MAGIC;
    xSemaphoreGive(s_mutex);
    state_save();
    if (active == X_ACTIVE_BATH) {
        snprintf(s_feedback, sizeof(s_feedback), "洗澡已记录，5秒内按确认键撤销");
    } else if (active == X_ACTIVE_TUMMY) {
        snprintf(s_feedback, sizeof(s_feedback), "趴玩已记录，5秒内按确认键撤销");
    }
}

static void save_diaper(bool poop)
{
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
    undo_capture_locked();
    s_state.data.diaper_count++;
    if (poop) s_state.data.poop_count++;
    else s_state.data.pee_count++;
    s_state.data.last_diaper_epoch = (int64_t)time(NULL);
    append_event_locked(poop ? X_EVENT_POOP : X_EVENT_PEE, 0, 0, 0, s_state.data.last_diaper_epoch);
    xSemaphoreGive(s_mutex);
    state_save();
    snprintf(s_feedback, sizeof(s_feedback), "%s已记录，5秒内按确认键撤销", poop ? "便" : "尿");
}

static void start_active(x_active_t active)
{
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
    undo_clear();
    s_state.data.active = active;
    if (active == X_ACTIVE_SLEEP) s_state.data.sleep_start_epoch = (int64_t)time(NULL);
    s_state.active_started_us = esp_timer_get_time();
    s_state.active_time_known = true;
    xSemaphoreGive(s_mutex);
    state_save();
    go_page(X_PAGE_ACTIVE, 0);
}

static void finish_active(void)
{
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
    undo_capture_locked();
    x_active_t active = (x_active_t)s_state.data.active;
    int64_t elapsed = s_state.active_time_known ?
        (esp_timer_get_time() - s_state.active_started_us) / 60000000 : 0;
    if (active == X_ACTIVE_SLEEP) {
        s_state.data.sleep_count++;
        s_state.data.sleep_end_epoch = (int64_t)time(NULL);
        if (elapsed > 0 && elapsed < 65535) s_state.data.sleep_minutes += (uint16_t)elapsed;
        append_event_locked(X_EVENT_SLEEP, 0,
                            elapsed > 0 && elapsed < 65535 ? (uint16_t)elapsed : 0, 0,
                            s_state.data.sleep_end_epoch);
    } else if (active == X_ACTIVE_TIMER) {
        s_state.data.timer_count++;
        append_event_locked(X_EVENT_TIMER, 0,
                            s_state.data.timer_minutes, 0, (int64_t)time(NULL));
    }
    s_state.data.active = X_ACTIVE_NONE;
    s_state.active_started_us = 0;
    s_state.active_time_known = false;
    xSemaphoreGive(s_mutex);
    state_save();
    snprintf(s_feedback, sizeof(s_feedback), "%s已保存，5秒内按确认键撤销", active_name(active));
}

static void save_feed(void)
{
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
    undo_capture_locked();
    s_state.data.milk_ml = (uint16_t)s_feed_ml;
    s_state.data.milk_count++;
    s_state.data.milk_ingredient = (uint8_t)s_feed_ingredient;
    s_state.data.last_milk_epoch = (int64_t)time(NULL);
    append_event_locked(X_EVENT_FEED, (uint16_t)s_feed_ml, 0, (uint8_t)s_feed_ingredient,
                        s_state.data.last_milk_epoch);
    xSemaphoreGive(s_mutex);
    state_save();
    snprintf(s_feedback, sizeof(s_feedback), "喂养已记录，5秒内按确认键撤销");
}

void xigua_app_enter(void)
{
    if (!s_xigua_font_ready) {
        s_xigua_font = xigua_font_zh16;
        s_xigua_font.fallback = &xigua_font_full20;
        s_xigua_font20 = xigua_font_zh20;
        s_xigua_font20.fallback = &xigua_font_full20;
        s_xigua_font_ready = true;
    }
    ui_shell();
    s_timer = lv_timer_create(ui_timer_cb, 1000, NULL);
    ui_refresh_page();
}

void xigua_app_exit(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_title = s_body = s_status = s_hint = s_battery = NULL;
    s_wifi_keyboard = s_wifi_password = NULL;
    s_ai_panel = s_ai_text = NULL;
    s_home_panel = s_home_summary = NULL;
    memset(s_home_cards, 0, sizeof(s_home_cards));
    memset(s_ai_cards, 0, sizeof(s_ai_cards));
    s_ai_rendered_view = (x_ai_view_t)-1;
    memset(s_wifi_key_labels, 0, sizeof(s_wifi_key_labels));
}

esp_err_t xigua_app_start(void)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    if (s_running) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK && err != ESP_ERR_NVS_NO_FREE_PAGES && err != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        return err;
    }
    if (err != ESP_OK) return err;
    err = nvs_open(X_NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) return err;
    s_nvs_open = true;
    state_load();
    load_last_command_id();
    apply_timezone();
    s_state.battery_soc = bsp_battery_soc();
    bsp_display_backlight(s_state.data.brightness);
    s_feed_ml = s_state.data.milk_ml;
    s_feed_ingredient = s_state.data.milk_ingredient;
    s_timer_focus = 0;
    s_focus = 0;
    undo_clear();
    s_feedback[0] = '\0';
    s_running = true;
    if (xigua_wifi_start() != ESP_OK) {
        snprintf(s_feedback, sizeof(s_feedback), "Wi-Fi启动失败");
    }
    if (xigua_ai_start() != ESP_OK) snprintf(s_feedback, sizeof(s_feedback), "AI服务启动失败");
    return ESP_OK;
}

esp_err_t xigua_app_stop(void)
{
    s_running = false;
    xigua_ai_stop();
    xigua_wifi_stop();
    if (s_nvs_open) {
        nvs_close(s_nvs);
        s_nvs_open = false;
    }
    return ESP_OK;
}

esp_err_t xigua_app_ai_response(const char *json)
{
    if (!json || !json[0]) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    bool applied = false;
    bool duplicate_command = false;
    bool has_command_id = false;
    char command_id[X_COMMAND_ID_MAX] = { 0 };
    cJSON *command = cJSON_GetObjectItemCaseSensitive(root, "command_id");
    if (cJSON_IsString(command) && command->valuestring && command->valuestring[0] != '\0') {
        size_t command_len = strlen(command->valuestring);
        if (command_len >= sizeof(command_id)) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(command_id, command->valuestring, command_len + 1);
        has_command_id = true;
    }
    cJSON *actions = cJSON_GetObjectItemCaseSensitive(root, "actions");
    if (!actions) actions = cJSON_GetObjectItemCaseSensitive(root, "action");
    x_undo_t undo_before = s_undo;
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
        duplicate_command = has_command_id &&
            strcmp(s_last_command_id, command_id) == 0;
        if (!duplicate_command) {
            if (cJSON_IsArray(actions) || cJSON_IsObject(actions)) undo_capture_locked();
            if (cJSON_IsArray(actions)) {
                cJSON *action = NULL;
                cJSON_ArrayForEach(action, actions) {
                    if (cJSON_IsObject(action) && apply_ai_action_locked(action)) applied = true;
                }
            } else if (cJSON_IsObject(actions)) {
                applied = apply_ai_action_locked(actions);
            }
        }
        xSemaphoreGive(s_mutex);
    }
    if (!applied) s_undo = undo_before;
    if (applied) {
        if (state_save_command_id(has_command_id ? command_id : NULL) == ESP_OK && has_command_id) {
            snprintf(s_last_command_id, sizeof(s_last_command_id), "%s", command_id);
        }
        snprintf(s_feedback, sizeof(s_feedback), "AI记录已保存，5秒内按确认键撤销");
    } else if (duplicate_command) {
        snprintf(s_feedback, sizeof(s_feedback), "命令已执行");
    }

    bool replied = false;
    cJSON *reply = cJSON_GetObjectItemCaseSensitive(root, "reply_text");
    if (!cJSON_IsString(reply)) reply = cJSON_GetObjectItemCaseSensitive(root, "tts_text");
    if (cJSON_IsString(reply) && reply->valuestring) {
        s_ai_truncated = xigua_text_copy(s_ai_reply, sizeof(s_ai_reply), reply->valuestring);
        x_ai_complete(&s_ai_ui, true);
        s_ai_rendered_view = (x_ai_view_t)-1;
        snprintf(s_feedback, sizeof(s_feedback), "%s", cJSON_IsString(
            cJSON_GetObjectItemCaseSensitive(root, "reply_text")) ? "文字回复已准备" : "语音回复已准备");
        replied = true;
    }
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (!applied && !replied && duplicate_command) {
        replied = true;
    }
    if (!applied && !replied && cJSON_IsString(error) && error->valuestring) {
        snprintf(s_feedback, sizeof(s_feedback), "AI处理失败：%.36s", error->valuestring);
        replied = true;
    }
    cJSON_Delete(root);
    if (!applied && !replied) return ESP_ERR_INVALID_RESPONSE;
    if (s_running) ui_sync();
    return ESP_OK;
}

void xigua_app_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_running) return;
    if (s_page == X_PAGE_OVERVIEW && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) &&
        ev != BSP_BTN_LONG) {
        if (ev == BSP_BTN_PRESS) {
            s_focus = xigua_menu_move(s_focus, sizeof(HOME_ITEMS) / sizeof(HOME_ITEMS[0]),
                                      btn == BSP_BTN_DOWN);
            ui_sync();
        }
        return; /* PRESS moves once, CLICK/DOUBLE must not repeat it. */
    }
    if (s_page == X_PAGE_VOICE || s_page == X_PAGE_STORY) {
        x_ai_input_t input;
        if (ev == BSP_BTN_PRESS && btn == BSP_BTN_UP) input = X_AI_UP;
        else if (ev == BSP_BTN_PRESS && btn == BSP_BTN_DOWN) input = X_AI_DOWN;
        else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) input = X_AI_OK;
        else if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) input = X_AI_HOLD_OK;
        else if (ev == BSP_BTN_RELEASE && btn == BSP_BTN_OK) input = X_AI_RELEASE_OK;
        else if (ev == BSP_BTN_LONG && btn == BSP_BTN_DOWN) input = X_AI_BACK;
        else return;
        if (!bsp_lvgl_lock(100)) return;
        x_ai_effect_t effect = x_ai_input(&s_ai_ui, input);
        if (effect == X_AI_START_VOICE && !ai_request_voice_current_mode()) {
            snprintf(s_ai_error, sizeof(s_ai_error), "%s", s_feedback);
            x_ai_complete(&s_ai_ui, false);
        } else if (effect == X_AI_STOP_VOICE) xigua_ai_stop_voice();
        else if (effect == X_AI_HOME) go_page(X_PAGE_OVERVIEW, 0);
        if (input == X_AI_OK && s_ai_ui.view == X_AI_READY && s_ai_ui.focus == 1 &&
            !s_ai_ui.has_reply) snprintf(s_feedback, sizeof(s_feedback), "暂无回复");
        ui_refresh_page();
        bsp_lvgl_unlock();
        return;
    }
    if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_UP && s_page == X_PAGE_OVERVIEW) {
            s_feed_ml = s_state.data.milk_ml;
            s_feed_ingredient = s_state.data.milk_ingredient;
            go_page(X_PAGE_FEED, 0);
            ui_sync();
        } else if (btn == BSP_BTN_UP && s_page == X_PAGE_FEED) {
            s_feed_ingredient = (s_feed_ingredient + 1) % (sizeof(FEED_INGREDIENTS) / sizeof(FEED_INGREDIENTS[0]));
            ui_sync();
        } else if (btn == BSP_BTN_UP && s_page == X_PAGE_WIFI &&
                   s_wifi_ui_mode == X_WIFI_UI_EDIT_PASSWORD) {
            s_wifi_keyboard_index = s_wifi_nav.anchor;
            s_wifi_keyboard_page = s_wifi_nav.page;
            memset(&s_wifi_nav, 0, sizeof(s_wifi_nav));
            wifi_ui_backspace();
            ui_sync();
        } else if (btn == BSP_BTN_OK && s_page == X_PAGE_WIFI &&
                   s_wifi_ui_mode == X_WIFI_UI_EDIT_PASSWORD) {
            wifi_ui_finish_edit();
            ui_sync();
        } else if (btn == BSP_BTN_DOWN && s_page == X_PAGE_WIFI) {
            if (s_wifi_ui_mode == X_WIFI_UI_MENU) {
                go_page(X_PAGE_SETTINGS, 0);
            } else {
                wifi_ui_enter_menu();
            }
            ui_sync();
        } else if (btn == BSP_BTN_DOWN && s_page != X_PAGE_OVERVIEW) {
            go_page(X_PAGE_OVERVIEW, 0);
            ui_sync();
        } else if (btn == BSP_BTN_OK) {
            if (bsp_lvgl_lock(100)) {
                if (s_page == X_PAGE_ACTIVE && !s_confirm_abort) {
                    s_confirm_abort = true;
                    s_abort_choice = false;
                    ui_refresh_page();
                } else {
                    snprintf(s_feedback, sizeof(s_feedback), "语音服务等待接入");
                    ui_refresh_page();
                }
                bsp_lvgl_unlock();
            }
        }
        return;
    }
    if (s_page == X_PAGE_WIFI && s_wifi_ui_mode == X_WIFI_UI_EDIT_PASSWORD &&
        (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        int direction = btn == BSP_BTN_UP ? -1 : 1;
        size_t characters = strlen(wifi_keyboard_chars());
        if (ev == BSP_BTN_PRESS) {
            s_wifi_keyboard_index = xigua_keyboard_press(&s_wifi_nav,
                s_wifi_keyboard_index, characters, &s_wifi_keyboard_page, direction);
            ui_sync();
        } else if (ev == BSP_BTN_DOUBLE) {
            s_wifi_keyboard_index = xigua_keyboard_double(&s_wifi_nav,
                s_wifi_keyboard_index, characters, &s_wifi_keyboard_page, direction);
            ui_sync();
        }
        return; /* CLICK must not duplicate the movement already handled on PRESS. */
    }
    if (ev != BSP_BTN_CLICK) return;

    if (btn == BSP_BTN_OK && s_page == X_PAGE_OVERVIEW && undo_available()) {
        if (undo_last()) ui_sync();
        return;
    }

    if (s_page == X_PAGE_FEED) {
        if (btn == BSP_BTN_UP && s_feed_ml > 10) s_feed_ml -= 10;
        else if (btn == BSP_BTN_DOWN && s_feed_ml < 400) s_feed_ml += 10;
        else if (btn == BSP_BTN_OK) {
            save_feed();
            go_page(X_PAGE_OVERVIEW, 0);
            ui_sync();
            return;
        }
    } else if (s_page == X_PAGE_ACTIVE) {
        if (s_confirm_abort) {
            if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) s_abort_choice = !s_abort_choice;
            else if (btn == BSP_BTN_OK) {
                if (s_abort_choice) {
                    s_state.data.active = X_ACTIVE_NONE;
                    s_state.active_started_us = 0;
                    s_state.active_time_known = false;
                    state_save();
                    go_page(X_PAGE_OVERVIEW, 0);
                } else {
                    s_confirm_abort = false;
                    ui_sync();
                }
            }
        } else if (btn == BSP_BTN_OK) {
            finish_active();
            go_page(X_PAGE_OVERVIEW, 0);
            ui_sync();
            return;
        }
    } else if (s_page == X_PAGE_TIMER) {
        const size_t count = sizeof(TIMER_OPTIONS) / sizeof(TIMER_OPTIONS[0]);
        if (btn == BSP_BTN_UP) s_timer_focus = (s_timer_focus + count - 1) % count;
        else if (btn == BSP_BTN_DOWN) s_timer_focus = (s_timer_focus + 1) % count;
        else if (btn == BSP_BTN_OK) {
            if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                s_state.data.timer_minutes = TIMER_OPTIONS[s_timer_focus];
                xSemaphoreGive(s_mutex);
            }
            start_active(X_ACTIVE_TIMER);
            go_page(X_PAGE_ACTIVE, 0);
            ui_sync();
            return;
        }
    } else if (s_page == X_PAGE_DIAPER) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) s_focus = s_focus == 0 ? 1 : 0;
        else if (btn == BSP_BTN_OK) {
            save_diaper(s_focus == 1);
            go_page(X_PAGE_OVERVIEW, 0);
            ui_sync();
            return;
        }
    } else if (s_page == X_PAGE_OVERVIEW) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            const size_t count = 7;
            s_focus = btn == BSP_BTN_UP ? (s_focus + count - 1) % count : (s_focus + 1) % count;
        } else if (btn == BSP_BTN_OK) {
            if (s_focus == 0) go_page(X_PAGE_VOICE, 0);
            else if (s_focus == 1) go_page(X_PAGE_RECORD, 0);
            else if (s_focus == 2) go_page(X_PAGE_TODAY, 0);
            else if (s_focus == 3) {
                if (s_state.data.active != X_ACTIVE_NONE) go_page(X_PAGE_ACTIVE, 0);
                else start_active(X_ACTIVE_SLEEP), go_page(X_PAGE_ACTIVE, 0);
            }
            else if (s_focus == 4) go_page(X_PAGE_SOUND, 0);
            else if (s_focus == 5) {
                (void)xigua_wifi_start();
                wifi_ui_enter_menu();
                go_page(X_PAGE_WIFI, 0);
            } else go_page(X_PAGE_SETTINGS, 0);
            ui_sync();
            return;
        }
    } else if (s_page == X_PAGE_RECORD) {
        if (btn == BSP_BTN_UP) s_focus = (s_focus + 5) % 6;
        else if (btn == BSP_BTN_DOWN) s_focus = (s_focus + 1) % 6;
        else if (btn == BSP_BTN_OK) {
            if (s_focus == 0) s_feed_ml = s_state.data.milk_ml,
                s_feed_ingredient = s_state.data.milk_ingredient, go_page(X_PAGE_FEED, 0);
            else if (s_focus == 1) start_active(X_ACTIVE_SLEEP), go_page(X_PAGE_ACTIVE, 0);
            else if (s_focus == 2) go_page(X_PAGE_DIAPER, 0);
            else if (s_focus == 3) save_simple_record(X_ACTIVE_BATH), go_page(X_PAGE_OVERVIEW, 0);
            else if (s_focus == 4) save_simple_record(X_ACTIVE_TUMMY), go_page(X_PAGE_OVERVIEW, 0);
            else go_page(X_PAGE_TIMER, 0);
            ui_sync();
            return;
        }
    } else if (s_page == X_PAGE_TODAY) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            // The first version is a single summary page; retain the buttons as a no-op.
        }
    } else if (s_page == X_PAGE_SOUND) {
        if (btn == BSP_BTN_UP) s_focus = (s_focus + 2) % 3;
        else if (btn == BSP_BTN_DOWN) s_focus = (s_focus + 1) % 3;
        else if (btn == BSP_BTN_OK) snprintf(s_feedback, sizeof(s_feedback), "音频服务等待接入");
    } else if (s_page == X_PAGE_SETTINGS) {
        const size_t count = 4;
        if (btn == BSP_BTN_UP) s_focus = (s_focus + count - 1) % count;
        else if (btn == BSP_BTN_DOWN) s_focus = (s_focus + 1) % count;
        else if (btn == BSP_BTN_OK) {
            if (s_focus == 0) {
                (void)xigua_wifi_start();
                wifi_ui_enter_menu();
                go_page(X_PAGE_WIFI, 0);
            } else if (s_focus == 1) {
                size_t next = (s_state.data.timezone_index + 1) %
                    (sizeof(TIMEZONE_VALUES) / sizeof(TIMEZONE_VALUES[0]));
                if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                    s_state.data.timezone_index = (uint8_t)next;
                    xSemaphoreGive(s_mutex);
                }
                apply_timezone();
                state_save();
                snprintf(s_feedback, sizeof(s_feedback), "时区已设为%s", TIMEZONE_NAMES[next]);
            } else if (s_focus == 2) {
                uint8_t next = s_state.data.brightness == 100 ? 40 : (s_state.data.brightness + 30);
                if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                    s_state.data.brightness = next;
                    xSemaphoreGive(s_mutex);
                }
                bsp_display_backlight(next);
                state_save();
            } else {
                xigua_wifi_clear_credentials();
                snprintf(s_feedback, sizeof(s_feedback), "Wi-Fi凭据已清除");
            }
        }
    } else if (s_page == X_PAGE_WIFI) {
        if (s_wifi_ui_mode == X_WIFI_UI_MENU) {
            const size_t item_count = 1;
            if (btn == BSP_BTN_UP) s_focus = (s_focus + item_count - 1) % item_count;
            else if (btn == BSP_BTN_DOWN) s_focus = (s_focus + 1) % item_count;
            else if (btn == BSP_BTN_OK) {
                (void)xigua_wifi_start();
                wifi_ui_start_scan();
            }
        } else if (s_wifi_ui_mode == X_WIFI_UI_SCAN) {
            size_t count = xigua_wifi_scan_count();
            if (btn == BSP_BTN_UP && count > 0) {
                s_wifi_scan_index = (s_wifi_scan_index + count - 1) % count;
            } else if (btn == BSP_BTN_DOWN && count > 0) {
                s_wifi_scan_index = (s_wifi_scan_index + 1) % count;
            } else if (btn == BSP_BTN_OK && !xigua_wifi_scan_in_progress()) {
                wifi_ui_select_scan();
            }
        } else if (s_wifi_ui_mode == X_WIFI_UI_STATUS) {
            if (btn == BSP_BTN_OK) wifi_ui_enter_menu();
        } else if (s_wifi_ui_mode == X_WIFI_UI_EDIT_PASSWORD) {
            if (btn == BSP_BTN_OK) wifi_ui_choose_key();
        }
    }

    if (bsp_lvgl_lock(100)) {
        ui_refresh_page();
        bsp_lvgl_unlock();
    }
}
