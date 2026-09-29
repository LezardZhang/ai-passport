#include "xigua_app.h"

#include "bsp_battery.h"
#include "bsp_display.h"
#include "xigua_wifi.h"

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

static const uint32_t X_STATE_MAGIC = 0x58494741U;
static const char *X_NVS_NAMESPACE = "xigua";

static lv_obj_t *s_screen;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;
static lv_obj_t *s_battery;
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
static uint8_t s_ai_mode;
static bool s_running;
static char s_feedback[64];
static char s_ai_reply[192];

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

static bool valid_timer_minutes(uint16_t minutes)
{
    for (size_t i = 0; i < sizeof(TIMER_OPTIONS) / sizeof(TIMER_OPTIONS[0]); i++) {
        if (minutes == TIMER_OPTIONS[i]) return true;
    }
    return false;
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

static void state_save(void)
{
    if (!s_nvs_open || !s_mutex) return;
    x_persisted_t data;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
    data = s_state.data;
    xSemaphoreGive(s_mutex);
    esp_err_t err = nvs_set_blob(s_nvs, "state", &data, sizeof(data));
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGW(TAG, "state save failed: %s", esp_err_to_name(err));
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
                         uint16_t *sleep_count, uint16_t *sleep_minutes)
{
    *feed_count = 0;
    *milk_ml = 0;
    *pee = 0;
    *poop = 0;
    *sleep_count = 0;
    *sleep_minutes = 0;
    time_t now = time(NULL);
    struct tm today = { 0 };
    bool have_today = now >= 1700000000 && localtime_r(&now, &today) != NULL;
    for (uint8_t i = 0; i < data->event_count; i++) {
        const x_event_t *event = &data->events[i];
        if (have_today && event->epoch >= 1700000000) {
            time_t event_time = (time_t)event->epoch;
            struct tm event_day = { 0 };
            if (localtime_r(&event_time, &event_day) != NULL &&
                (event_day.tm_year != today.tm_year || event_day.tm_yday != today.tm_yday)) {
                continue;
            }
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
        default: break;
        }
    }
    if (data->event_count == 0) {
        *feed_count = data->milk_count;
        *milk_ml = (uint32_t)data->milk_count * data->milk_ml;
        *pee = data->pee_count;
        *poop = data->poop_count;
        *sleep_count = data->sleep_count;
        *sleep_minutes = data->sleep_minutes;
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
    case XIGUA_WIFI_ADVERTISING: return "等待配网";
    case XIGUA_WIFI_BLE_CONNECTED: return "手机已连接";
    case XIGUA_WIFI_STARTING: return "启动中";
    case XIGUA_WIFI_FAILED: return "连接失败";
    default: return "未连接";
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
    (void)size;
    lv_obj_set_style_text_font(label, &xigua_font_zh16, LV_PART_MAIN);
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
    s_hint = make_label(s_screen, 14, 278, 212, 32, "上/下选择  确认键打开", 0xB9C7D1, 14);
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
    uint32_t milk_total;
    event_totals(&snapshot.data, &feed_count, &milk_total, &pee_count, &poop_count,
                 &sleep_count, &sleep_minutes);
    xigua_wifi_status(wifi_ssid, sizeof(wifi_ssid), wifi_ip, sizeof(wifi_ip));
    switch (s_page) {
    case X_PAGE_OVERVIEW:
        ui_set_title("西瓜助手");
        {
            static const char *const HOME_ITEMS[] = {
                "AI助手", "手动记录", "今天", "睡眠", "声音", "设置"
            };
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
        ui_set_title("AI助手");
        snprintf(text, sizeof(text), "%s\n\n长按确认键说话\n%s",
                 s_ai_mode == 0 ? "问答/记录" : "讲故事/语音",
                 s_ai_reply[0] ? s_ai_reply : "可记录喂养、尿便、睡眠等事项");
        ui_set_hint("上/下切换模式  长按确认键说话  长按下键返回");
        break;
    case X_PAGE_STORY:
        ui_set_title("讲故事");
        snprintf(text, sizeof(text), "说出故事要求\n\n长按确认键说话\n回复将使用语音播放");
        ui_set_hint("长按确认键输入  确认键播放/暂停  长按下键取消");
        break;
    case X_PAGE_SOUND:
        ui_set_title("儿歌/白噪音");
        snprintf(text, sizeof(text), "%c 儿歌\n%c 白噪音\n%c 停止\n\n服务端流式待接入",
                 s_focus == 0 ? '>' : ' ', s_focus == 1 ? '>' : ' ', s_focus == 2 ? '>' : ' ');
        ui_set_hint("上/下选择  确认键播放/暂停  长按下键返回");
        break;
    case X_PAGE_TODAY:
        ui_set_title("今天");
        snprintf(text, sizeof(text), "喂养 %u 次（%lu 毫升）\n睡眠 %u 次（%u 分钟）\n尿 %u  便 %u\n洗澡 %u\n趴玩 %u\n计时 %u",
                 feed_count, (unsigned long)milk_total,
                 sleep_count, sleep_minutes,
                 pee_count, poop_count,
                 snapshot.data.bath_count, snapshot.data.tummy_count,
                 snapshot.data.timer_count);
        ui_set_hint("上/下查看  长按下键返回");
        break;
    case X_PAGE_SETTINGS:
        ui_set_title("设置");
        snprintf(text, sizeof(text), "%c Wi-Fi配网：%s\n%c 时区：%s\n%c 亮度：%u%%\n%c 清除Wi-Fi",
                 s_focus == 0 ? '>' : ' ', wifi_state_name(xigua_wifi_state()),
                 s_focus == 1 ? '>' : ' ', TIMEZONE_NAMES[snapshot.data.timezone_index],
                 s_focus == 2 ? '>' : ' ', snapshot.data.brightness,
                 s_focus == 3 ? '>' : ' ');
        ui_set_hint("上/下选择  确认键进入/修改  长按下键返回");
        break;
    case X_PAGE_WIFI:
        ui_set_title("Wi-Fi配网");
        if (xigua_wifi_state() == XIGUA_WIFI_CONNECTED) {
            snprintf(text, sizeof(text), "已连接\n\nSSID：%s\nIP：%s", wifi_ssid, wifi_ip);
        } else if (xigua_wifi_state() == XIGUA_WIFI_BLE_CONNECTED) {
            snprintf(text, sizeof(text), "手机已连接\n\n请在配网小程序中选择\n%s", xigua_wifi_device_name());
        } else if (xigua_wifi_state() == XIGUA_WIFI_ADVERTISING) {
            snprintf(text, sizeof(text), "等待配网\n\n打开蓝牙配网小程序\n设备：%s", xigua_wifi_device_name());
        } else {
            snprintf(text, sizeof(text), "%s\n\nSSID：%s\nIP：%s",
                     wifi_state_name(xigua_wifi_state()), wifi_ssid[0] ? wifi_ssid : "--",
                     wifi_ip[0] ? wifi_ip : "--");
        }
        ui_set_hint("上/下刷新  长按下键返回设置");
        break;
    }
    lv_label_set_text(s_body, text);
    if (snapshot.battery_soc >= 0) lv_label_set_text_fmt(s_battery, "%d%%", snapshot.battery_soc);
    if (s_status) lv_label_set_text(s_status, s_feedback);
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_page == X_PAGE_ACTIVE && !s_confirm_abort) ui_refresh_page();
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
    if (active == X_ACTIVE_NONE) s_state.data.diaper_count++;
    else if (active == X_ACTIVE_BATH) s_state.data.bath_count++;
    else if (active == X_ACTIVE_TUMMY) s_state.data.tummy_count++;
    if (active == X_ACTIVE_BATH) append_event_locked(X_EVENT_BATH, 0, 0, 0, (int64_t)time(NULL));
    else if (active == X_ACTIVE_TUMMY) append_event_locked(X_EVENT_TUMMY, 0, 0, 0, (int64_t)time(NULL));
    s_state.data.magic = X_STATE_MAGIC;
    xSemaphoreGive(s_mutex);
    state_save();
}

static void save_diaper(bool poop)
{
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
    s_state.data.diaper_count++;
    if (poop) s_state.data.poop_count++;
    else s_state.data.pee_count++;
    s_state.data.last_diaper_epoch = (int64_t)time(NULL);
    append_event_locked(poop ? X_EVENT_POOP : X_EVENT_PEE, 0, 0, 0, s_state.data.last_diaper_epoch);
    xSemaphoreGive(s_mutex);
    state_save();
    snprintf(s_feedback, sizeof(s_feedback), "%s已记录", poop ? "便" : "尿");
}

static void start_active(x_active_t active)
{
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
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
}

static void save_feed(void)
{
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
    s_state.data.milk_ml = (uint16_t)s_feed_ml;
    s_state.data.milk_count++;
    s_state.data.milk_ingredient = (uint8_t)s_feed_ingredient;
    s_state.data.last_milk_epoch = (int64_t)time(NULL);
    append_event_locked(X_EVENT_FEED, (uint16_t)s_feed_ml, 0, (uint8_t)s_feed_ingredient,
                        s_state.data.last_milk_epoch);
    xSemaphoreGive(s_mutex);
    state_save();
}

void xigua_app_enter(void)
{
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
    apply_timezone();
    s_state.battery_soc = bsp_battery_soc();
    bsp_display_backlight(s_state.data.brightness);
    s_feed_ml = s_state.data.milk_ml;
    s_feed_ingredient = s_state.data.milk_ingredient;
    s_timer_focus = 0;
    s_focus = 0;
    s_feedback[0] = '\0';
    s_running = true;
    if (xigua_wifi_start() != ESP_OK) {
        snprintf(s_feedback, sizeof(s_feedback), "Wi-Fi启动失败");
    }
    return ESP_OK;
}

esp_err_t xigua_app_stop(void)
{
    s_running = false;
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
    cJSON *actions = cJSON_GetObjectItemCaseSensitive(root, "actions");
    if (!actions) actions = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
        if (cJSON_IsArray(actions)) {
            cJSON *action = NULL;
            cJSON_ArrayForEach(action, actions) {
                if (cJSON_IsObject(action) && apply_ai_action_locked(action)) applied = true;
            }
        } else if (cJSON_IsObject(actions)) {
            applied = apply_ai_action_locked(actions);
        }
        xSemaphoreGive(s_mutex);
    }
    if (applied) state_save();

    bool replied = false;
    cJSON *reply = cJSON_GetObjectItemCaseSensitive(root, "reply_text");
    if (!cJSON_IsString(reply)) reply = cJSON_GetObjectItemCaseSensitive(root, "tts_text");
    if (cJSON_IsString(reply) && reply->valuestring) {
        snprintf(s_ai_reply, sizeof(s_ai_reply), "%s", reply->valuestring);
        snprintf(s_feedback, sizeof(s_feedback), "%s", cJSON_IsString(
            cJSON_GetObjectItemCaseSensitive(root, "reply_text")) ? "文字回复已准备" : "语音回复已准备");
        replied = true;
    }
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
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
    if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_UP && s_page == X_PAGE_OVERVIEW) {
            s_feed_ml = s_state.data.milk_ml;
            s_feed_ingredient = s_state.data.milk_ingredient;
            go_page(X_PAGE_FEED, 0);
            ui_sync();
        } else if (btn == BSP_BTN_UP && s_page == X_PAGE_FEED) {
            s_feed_ingredient = (s_feed_ingredient + 1) % (sizeof(FEED_INGREDIENTS) / sizeof(FEED_INGREDIENTS[0]));
            ui_sync();
        } else if (btn == BSP_BTN_DOWN && s_page == X_PAGE_WIFI) {
            go_page(X_PAGE_SETTINGS, 0);
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
    if (ev != BSP_BTN_CLICK) return;

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
            const size_t count = 6;
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
            else go_page(X_PAGE_SETTINGS, 0);
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
    } else if (s_page == X_PAGE_VOICE || s_page == X_PAGE_STORY) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            s_ai_mode = s_ai_mode == 0 ? 1 : 0;
            snprintf(s_feedback, sizeof(s_feedback), "已切换到%s模式",
                     s_ai_mode == 0 ? "问答" : "故事");
        } else if (btn == BSP_BTN_OK) {
            snprintf(s_feedback, sizeof(s_feedback), "请长按确认键开始%s语音",
                     s_ai_mode == 0 ? "问答" : "故事");
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
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            (void)xigua_wifi_start();
        }
    }

    if (bsp_lvgl_lock(100)) {
        ui_refresh_page();
        bsp_lvgl_unlock();
    }
}
