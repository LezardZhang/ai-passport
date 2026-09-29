#include "xigua_ui.h"
#include "xigua_keyboard.h"

#include <stdio.h>
#include <string.h>
#include "lvgl.h"

LV_FONT_DECLARE(xigua_font_16);

/* One LVGL owner, no UI callbacks or timers. All work is driven by the app task. */
typedef enum {
    PAGE_HOME, PAGE_FEED, PAGE_SLEEP, PAGE_DIAPER, PAGE_TODAY,
    PAGE_MORE, PAGE_BATH, PAGE_TUMMY, PAGE_TIMER, PAGE_SETTINGS, PAGE_LATER,
    PAGE_HISTORY, PAGE_SOUND, PAGE_VOLUME, PAGE_WIFI, PAGE_WIFI_EDIT, PAGE_KEYBOARD
} page_t;

static lv_obj_t *s_screen;
static page_t s_page;
static unsigned s_home, s_more, s_diaper;
static uint32_t s_feed_ml, s_timer_min;
static unsigned s_history_page;
static bool s_feed_edit;
static bool s_stats_all_time;
static unsigned s_sound, s_volume;
static unsigned s_wifi_choice, s_wifi_field;
static char s_wifi_ssid[33], s_wifi_password[65];
static xigua_keyboard_t s_keyboard;
static bool s_keyboard_password, s_wifi_request_failed, s_keyboard_full;
static lv_font_t s_chinese_font;
/* Bounded widget allocation: polling and navigation never recreate a tree. */
#define BOX_COUNT 10
#define LABEL_COUNT 36
static lv_obj_t *s_boxes[BOX_COUNT], *s_labels[LABEL_COUNT];
static unsigned s_box_used, s_label_used;
static bool s_dirty = true;
static uint64_t s_render_signature;
static const char *const home_names[] = {
    "概览", "喂奶", "睡眠", "尿布", "声音", "今天", "更多"
};
static const char *const more_names[] = { "洗澡", "趴玩", "计时器", "设置", "后续功能" };
static const char *const diaper_names[] = { "小便", "大便", "都有" };
static const char *const sound_names[] = { "白噪声", "雨声", "海浪" };

static lv_color_t color(uint32_t rgb) { return lv_color_hex(rgb); }

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h,
                     uint32_t fill, int radius)
{
    (void)parent;
    if (s_box_used >= BOX_COUNT) return NULL;
    lv_obj_t *obj = s_boxes[s_box_used++];
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, color(fill), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       int w, int h, uint32_t ink, bool large)
{
    (void)parent;
    if (s_label_used >= LABEL_COUNT) return NULL;
    lv_obj_t *obj = s_labels[s_label_used++];
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, large ? &lv_font_montserrat_20 : &s_chinese_font, 0);
    lv_obj_set_style_text_color(obj, color(ink), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    if (strcmp(lv_label_get_text(obj), text) != 0) lv_label_set_text(obj, text);
    return obj;
}

static void text_line(const char *text, int y, uint32_t ink)
{
    label(s_screen, text, 24, y, 192, 28, ink, false);
}

static void title(const char *text, const char *hint)
{
    label(s_screen, text, 22, 43, 200, 28, 0xF4F0E8, false);
    label(s_screen, hint, 22, 72, 200, 24, 0x9BAAA7, false);
}

static void footer(const char *text)
{
    box(s_screen, 12, 277, 216, 31, 0x202E35, 12);
    label(s_screen, text, 21, 282, 198, 24, 0xD5E6DD, false);
}

static void feedback(const xigua_ui_view_t *v)
{
    if (s_page == PAGE_HOME && v->undo_available && v->state && v->state->undo.valid &&
        !v->read_only && (v->feedback == XIGUA_UI_IDLE || v->feedback == XIGUA_UI_SAVED)) return;
    const char *msg = NULL;
    switch (v->feedback) {
    case XIGUA_UI_SAVING: msg = "保存中..."; break;
    case XIGUA_UI_SAVED: msg = "记好了"; break;
    case XIGUA_UI_ERROR: msg = "保存失败 请重试"; break;
    case XIGUA_UI_STORAGE_FULL: msg = "记录已满 无法保存"; break;
    case XIGUA_UI_READ_ONLY: msg = "存储只读 无法保存"; break;
    default: break;
    }
    if (v->read_only) msg = "存储只读 无法保存";
    if (msg) {
        box(s_screen, 15, 246, 210, 28,
            v->feedback == XIGUA_UI_SAVED ? 0x174A3C : 0x703D35, 9);
        label(s_screen, msg, 24, 250, 194, 24, 0xFFFFFF, false);
    }
}

static void status_bar(const xigua_ui_view_t *v)
{
    char buf[24];
    const bool time_known = v->now.quality != XIGUA_TIME_UNKNOWN &&
                            v->now.unix_ms > 0;
    if (time_known && v->service.timezone_configured) {
        snprintf(buf, sizeof(buf), "%s  %s", v->service.local_time,
                 v->service.wifi_connected ? "联网" : "离线");
    } else if (time_known) {
        snprintf(buf, sizeof(buf), "时区待配置");
    } else {
        snprintf(buf, sizeof(buf), "时间待校准");
    }
    label(s_screen, buf, 18, 11, 142, 23, 0xB8D3C9, false);
    if (v->battery_pct >= 0 && v->battery_pct <= 100)
        snprintf(buf, sizeof(buf), "%d%%", v->battery_pct);
    else
        snprintf(buf, sizeof(buf), "--%%");
    lv_obj_t *battery = label(s_screen, buf, 165, 11, 58, 23, 0xB8D3C9, false);
    lv_obj_set_style_text_align(battery, LV_TEXT_ALIGN_RIGHT, 0);
    box(s_screen, 18, 37, 204, 1, 0x405158, 0);
}

static void home(const xigua_ui_view_t *v)
{
    const xigua_state_t *state = v->state;
    char buf[70];
    const xigua_event_t *feed = state ? xigua_latest(state, XIGUA_FEED) : NULL;
    const xigua_event_t *sleep = state ? xigua_active_sleep(state) : NULL;
    uint32_t remaining;
    const bool due = state && xigua_timer_remaining(state, &v->now, &remaining) && remaining == 0;
    title("西瓜助手", due ? "计时已到 请查看" : "本地记录 离线可用");
    box(s_screen, 14, 102, 212, 139, 0x25413D, 18);
    label(s_screen, home_names[s_home], 26, 112, 170, 25, 0x8DD7AE, false);
    switch (s_home) {
    case 0:
        if (feed) snprintf(buf, sizeof(buf), "上次奶瓶 %lu ml", (unsigned long)feed->value);
        else snprintf(buf, sizeof(buf), "还没有奶瓶记录");
        text_line(buf, 147, 0xFFFFFF);
        if (sleep) text_line("正在睡眠", 181, 0xF6D884);
        else text_line("睡眠未开始", 181, 0xB9CBC5);
        if (state && xigua_active_tummy(state)) text_line("正在趴玩", 211, 0xF6D884);
        break;
    case 1: text_line("记一次奶瓶", 155, 0xFFFFFF); break;
    case 2: text_line(sleep ? "结束本次睡眠" : "开始睡眠", 155, 0xFFFFFF); break;
    case 3: text_line("小便 大便", 155, 0xFFFFFF); break;
    case 4:
        text_line(v->audio.playing ? "声音正在播放" : "本地声音", 145, 0xFFFFFF);
        text_line("白噪声 雨声 海浪", 183, 0xB9CBC5);
        break;
    case 5: text_line("查看已保存记录", 155, 0xFFFFFF); break;
    default: text_line("洗澡 趴玩 计时", 155, 0xFFFFFF); break;
    }
    snprintf(buf, sizeof(buf), "%u / 7", s_home + 1);
    label(s_screen, buf, 176, 214, 45, 20, 0xB9CBC5, false);
    if (v->undo_available && state && state->undo.valid && !v->read_only &&
        (v->feedback == XIGUA_UI_IDLE || v->feedback == XIGUA_UI_SAVED)) {
        box(s_screen, 14, 244, 212, 30, 0x7B5733, 10);
        label(s_screen, "已保存 OK撤销", 24, 249, 195, 23, 0xFFFFFF, false);
        footer("上下切换 OK撤销");
    } else {
        footer("上下切换 OK进入");
    }
}

static void editor_number(const char *unit, uint32_t value, const char *description)
{
    char buf[40];
    snprintf(buf, sizeof(buf), "%lu %s", (unsigned long)value, unit);
    box(s_screen, 15, 112, 210, 118, 0x25413D, 18);
    lv_obj_t *big = label(s_screen, buf, 26, 134, 188, 38, 0xFFFFFF, true);
    lv_obj_set_style_text_align(big, LV_TEXT_ALIGN_CENTER, 0);
    label(s_screen, description, 29, 184, 184, 30, 0xAAC9BA, false);
}

static void feed_page(const xigua_ui_view_t *v)
{
    const xigua_event_t *last = v->state ? xigua_latest(v->state, XIGUA_FEED) : NULL;
    title(s_feed_edit ? "修正上次奶量" : "记奶瓶", "10-400 ml 每次10 ml");
    editor_number("ml", s_feed_ml, s_feed_edit ? "修改最近一次记录" : "确认后写入本地记录");
    footer(v->read_only ? "存储只读 长按下返回" :
           last ? "OK保存 长OK修正" : "上下调整 OK保存");
}

static void sleep_page(const xigua_ui_view_t *v)
{
    const xigua_event_t *active = v->state ? xigua_active_sleep(v->state) : NULL;
    title("睡眠", active ? "本次睡眠正在记录" : "睡眠尚未开始");
    box(s_screen, 15, 112, 210, 118, 0x25413D, 18);
    text_line(active ? "正在睡眠" : "准备开始睡眠", 143, 0xFFFFFF);
    if (active) {
        uint64_t elapsed;
        if (xigua_elapsed(active, &v->now, &elapsed)) {
            char buf[40];
            snprintf(buf, sizeof(buf), "已过 %lu 分钟", (unsigned long)(elapsed / 60000));
            text_line(buf, 180, 0xA5DAC0);
        } else text_line("时长待确认", 180, 0xE7C889);
    }
    footer(v->read_only ? "存储只读 长按下返回" :
           active ? "OK结束 长按下返回" : "OK开始 长按下返回");
}

static void diaper_page(const xigua_ui_view_t *v)
{
    title("尿布", "选择本次记录的情况");
    box(s_screen, 15, 105, 210, 140, 0x25413D, 18);
    for (unsigned i = 0; i < 3; ++i) {
        if (i == s_diaper) box(s_screen, 24, 115 + (int)i * 42, 192, 35, 0x3D7562, 10);
        label(s_screen, diaper_names[i], 36, 122 + (int)i * 42, 165, 24, 0xFFFFFF, false);
    }
    footer(v->read_only ? "存储只读 长按下返回" : "上下选择 OK保存");
}

static bool today_available(const xigua_ui_view_t *v)
{
    return v->service.day_valid && v->now.quality != XIGUA_TIME_UNKNOWN &&
        v->now.unix_ms >= v->service.day_begin_ms && v->now.unix_ms < v->service.day_end_ms;
}

static void today_page(const xigua_ui_view_t *v)
{
    char buf[65];
    xigua_stats_t stats = {0};
    const bool day_valid = today_available(v);
    if (!s_stats_all_time && !day_valid) {
        title("今天", v->service.timezone_configured ? "时间待校准" : "时区待配置");
        box(s_screen, 15, 103, 210, 141, 0x25413D, 18);
        text_line("今日统计尚不可用", 122, 0xF0CD83);
        text_line("上下可看所有累计", 159, 0xFFFFFF);
        snprintf(buf, sizeof(buf), "已保存 %u 条记录", v->state ? v->state->event_count : 0);
        text_line(buf, 198, 0xB9CBC5);
        footer("上下切换 OK看记录");
        return;
    }
    if (v->state) xigua_stats(v->state, &v->now, s_stats_all_time,
        v->service.day_begin_ms, v->service.day_end_ms, &stats);
    if (stats.pending_count)
        snprintf(buf, sizeof(buf), "时间待确认 %lu 条", (unsigned long)stats.pending_count);
    else snprintf(buf, sizeof(buf), "%s", s_stats_all_time ? "所有已保存记录" : v->service.local_date);
    title(s_stats_all_time ? "累计" : "今天", buf);
    box(s_screen, 15, 103, 210, 141, 0x25413D, 18);
    snprintf(buf, sizeof(buf), "奶瓶 %lu 次 %lu ml",
             (unsigned long)stats.feed_count, (unsigned long)stats.feed_ml);
    text_line(buf, 119, 0xFFFFFF);
    snprintf(buf, sizeof(buf), "尿布 %lu 洗澡 %lu",
             (unsigned long)stats.diaper_count, (unsigned long)stats.bath_count);
    text_line(buf, 148, 0xFFFFFF);
    if (stats.sleep_ms / 60000 > 99999999)
        snprintf(buf, sizeof(buf), "睡眠 >99999999 分钟");
    else snprintf(buf, sizeof(buf), "睡眠 %llu 分钟", (unsigned long long)(stats.sleep_ms / 60000));
    text_line(buf, 177, 0xFFFFFF);
    snprintf(buf, sizeof(buf), "趴玩 %lu 分钟", (unsigned long)stats.tummy_minutes);
    text_line(buf, 206, 0xFFFFFF);
    footer("上下切换 OK看记录");
}

static void history_page(const xigua_ui_view_t *v)
{
    const unsigned count = v->state ? v->state->event_count : 0;
    const unsigned pages = count ? (count + 2) / 3 : 1;
    if (s_history_page >= pages) s_history_page = pages - 1;
    char buf[96];
    snprintf(buf, sizeof(buf), "累计 %u 条 第%u/%u页", count, s_history_page + 1, pages);
    title("记录列表", buf);
    if (!count) text_line("还没有记录", 137, 0xB9CBC5);
    for (unsigned row = 0; row < 3 && s_history_page * 3 + row < count; ++row) {
        const unsigned index = count - 1 - (s_history_page * 3 + row);
        const xigua_event_t *e = &v->state->events[index];
        const int y = 104 + (int)row * 46;
        box(s_screen, 15, y, 210, 43, 0x25413D, 9);
        switch (e->type) {
        case XIGUA_FEED: snprintf(buf, sizeof(buf), "#%lu 奶瓶 %lu ml", (unsigned long)e->id, (unsigned long)e->value); break;
        case XIGUA_DIAPER:
            snprintf(buf, sizeof(buf), "#%lu 尿布 %s", (unsigned long)e->id,
                     e->value >= 1 && e->value <= 3 ? diaper_names[e->value - 1] : "待确认"); break;
        case XIGUA_BATH: snprintf(buf, sizeof(buf), "#%lu 洗澡", (unsigned long)e->id); break;
        default: {
            const char *name = e->type == XIGUA_SLEEP ? "睡眠" : "趴玩";
            if (e->active) snprintf(buf, sizeof(buf), "#%lu %s 进行中", (unsigned long)e->id, name);
            else if (e->duration_quality != XIGUA_DURATION_UNKNOWN)
                snprintf(buf, sizeof(buf), "#%lu %s %lu分", (unsigned long)e->id, name, (unsigned long)(e->duration_ms / 60000));
            else snprintf(buf, sizeof(buf), "#%lu %s 时长待确认", (unsigned long)e->id, name);
            break;
        }
        }
        label(s_screen, buf, 24, y + 1, 192, 21, 0xFFFFFF, false);
        if (e->start.quality == XIGUA_TIME_UNKNOWN || e->start.unix_ms <= 0)
            snprintf(buf, sizeof(buf), "时间待校准");
        else snprintf(buf, sizeof(buf), "时间已校准");
        label(s_screen, buf, 24, y + 22, 192, 20, 0xAAC9BA, false);
    }
    footer("上下翻页 OK看统计");
}

static void more_page(void)
{
    title("更多", "本地工具与设备信息");
    box(s_screen, 15, 99, 210, 145, 0x25413D, 18);
    for (unsigned i = 0; i < 5; ++i) {
        if (i == s_more) box(s_screen, 24, 105 + (int)i * 27, 192, 27, 0x3D7562, 8);
        label(s_screen, more_names[i], 35, 109 + (int)i * 27, 170, 22, 0xFFFFFF, false);
    }
    footer("上下选择 OK进入");
}

static void bath_page(const xigua_ui_view_t *v)
{
    title("洗澡", "记录一次已完成的洗澡");
    box(s_screen, 15, 112, 210, 118, 0x25413D, 18);
    text_line("洗澡已完成？", 145, 0xFFFFFF);
    footer(v->read_only ? "存储只读 长按下返回" : "OK保存 长按下返回");
}

static void tummy_page(const xigua_ui_view_t *v)
{
    const xigua_event_t *active = v->state ? xigua_active_tummy(v->state) : NULL;
    title("趴玩", active ? "本次趴玩正在记录" : "趴玩尚未开始");
    box(s_screen, 15, 112, 210, 118, 0x25413D, 18);
    text_line(active ? "正在趴玩" : "准备开始趴玩", 143, 0xFFFFFF);
    if (active) {
        uint64_t elapsed;
        if (xigua_elapsed(active, &v->now, &elapsed)) {
            char buf[40];
            snprintf(buf, sizeof(buf), "已过 %lu 分钟", (unsigned long)(elapsed / 60000));
            text_line(buf, 180, 0xA5DAC0);
        } else text_line("时长待确认", 180, 0xE7C889);
    }
    footer(v->read_only ? "存储只读 长按下返回" :
           active ? "OK结束 长按下返回" : "OK开始 长按下返回");
}

static void timer_page(const xigua_ui_view_t *v)
{
    const bool active = v->state && v->state->timer.active;
    title("计时器", active ? "计时正在进行" : "设置倒计时");
    if (active) {
        uint32_t remaining;
        if (xigua_timer_remaining(v->state, &v->now, &remaining)) {
            editor_number("min", (remaining + 59999) / 60000,
                          remaining ? "OK取消本次计时" : "计时已到 OK清除");
        } else {
            box(s_screen, 15, 112, 210, 118, 0x25413D, 18);
            text_line("计时状态待确认", 145, 0xF0CD83);
            text_line("OK 清除旧计时", 181, 0xFFFFFF);
        }
        footer(v->read_only ? "存储只读 长按下返回" : "OK取消 长按下返回");
    } else {
        editor_number("min", s_timer_min, "每次调整 5 分钟");
        footer(v->read_only ? "存储只读 长按下返回" : "上下调整 OK开始");
    }
}

static void settings_page(const xigua_ui_view_t *v)
{
    char buf[50];
    title("设置与设备", "OK 配置 Wi-Fi 网络");
    box(s_screen, 15, 103, 210, 142, 0x25413D, 18);
    text_line(!v->service.available ? "配置服务未启动" :
        v->service.wifi_connected ? "网络：已连接" :
        v->service.wifi_configured ? "网络：连接中" : "网络：待配置", 116, 0xFFFFFF);
    snprintf(buf, sizeof(buf), "时间：%s", v->now.quality == XIGUA_TIME_UNKNOWN ? "待校准" :
             v->service.timezone_configured ? v->service.local_time : "时区待配置");
    text_line(buf, 150, 0xFFFFFF);
    text_line(v->service.ai_busy ? "AI：请求中" : !v->service.ai_configured ? "AI：待配置" :
        v->service.ai_status < 0 ? "AI：待测试" : v->service.ai_status == 0 ? "AI：测试成功" : "AI：请求失败", 184, 0xFFFFFF);
    snprintf(buf, sizeof(buf), "记录：%u/%u %s", v->state ? v->state->event_count : 0,
             XIGUA_EVENT_CAPACITY, v->service.config_read_only ? "配置只读" : "");
    text_line(buf, 217, 0xFFFFFF);
    footer("OK配网 长按下返回");
}

/* Dynamic SSIDs are bytes, not a promise of arbitrary CJK font coverage.
 * Escape non-ASCII bytes explicitly; preserve the original bytes for saving. */
static void wifi_name(char *out, size_t capacity, const char *ssid)
{
    size_t used = 0;
    for (size_t i = 0; ssid[i] && used + 1 < capacity; ++i) {
        const unsigned char c = (unsigned char)ssid[i];
        if (c >= 32 && c <= 126) out[used++] = (char)c;
        else {
            if (used + 4 >= capacity) break;
            snprintf(out + used, capacity - used, "\\x%02X", c);
            used += 4;
        }
    }
    out[used] = 0;
}

static const char *wifi_result(const xigua_ui_view_t *v)
{
    if (s_wifi_request_failed) return "服务忙 请重试";
    if (v->service.wifi_config_busy) return "正在保存网络";
    switch (v->service.wifi_config_result) {
    case XG_WIFI_CONFIG_SAVED:
        return v->service.wifi_connected &&
            (s_page != PAGE_WIFI_EDIT || !strcmp(v->service.wifi_ssid, s_wifi_ssid)) ? "已保存 网络已连接" :
            v->service.wifi_reason ? "连接失败 检查密码" : "已保存 正在连接";
    case XG_WIFI_CONFIG_INVALID: return "密码长度或格式错误";
    case XG_WIFI_CONFIG_READ_ONLY: return "配置只读 无法保存";
    case XG_WIFI_CONFIG_FULL: return "网络已满 USB管理";
    case XG_WIFI_CONFIG_SAVE_FAILED: return "保存失败 请重试";
    case XG_WIFI_CONFIG_UNAVAILABLE: return "配置服务未启动";
    default: return "选择网络或手动输入";
    }
}

static void wifi_page(const xigua_ui_view_t *v)
{
    char buf[160];
    title("Wi-Fi 配网", v->service.wifi_scan_busy ? "扫描中 请稍候" :
        v->service.wifi_scan_error ? "扫描失败 OK重新扫描" : wifi_result(v));
    const unsigned count = v->service.wifi_scan_count + 2;
    if (s_wifi_choice >= count) s_wifi_choice = 0;
    const unsigned first = s_wifi_choice / 4 * 4;
    for (unsigned row = 0; row < 4 && first + row < count; ++row) {
        const unsigned index = first + row;
        const int y = 102 + (int)row * 37;
        if (index == s_wifi_choice) box(s_screen, 15, y, 210, 35, 0x3D7562, 8);
        if (index == 0) snprintf(buf, sizeof(buf), "重新扫描");
        else if (index == 1) snprintf(buf, sizeof(buf), "手动输入网络");
        else {
            char name[133];
            const xigua_wifi_scan_entry_t *ap = &v->service.wifi_scan[index - 2];
            wifi_name(name, sizeof(name), ap->ssid);
            snprintf(buf, sizeof(buf), "%s %s", ap->secured ? "*" : "-", name);
        }
        lv_obj_t *l = label(s_screen, buf, 23, y + 6, 192, 24, 0xFFFFFF, false);
        lv_label_set_long_mode(l, index == s_wifi_choice ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
    }
    snprintf(buf, sizeof(buf), "%u/%u  2.4 GHz", s_wifi_choice + 1, count);
    label(s_screen, buf, 23, 251, 195, 23, 0xAAC9BA, false);
    footer("上下选择 OK进入");
}

static void wifi_edit_page(const xigua_ui_view_t *v)
{
    char buf[170], name[133];
    title("网络与密码", wifi_result(v));
    wifi_name(name, sizeof(name), s_wifi_ssid);
    for (unsigned i = 0; i < 3; ++i) {
        int y = 106 + (int)i * 43;
        if (i == s_wifi_field) box(s_screen, 15, y, 210, 39, 0x3D7562, 9);
        if (i == 0) snprintf(buf, sizeof(buf), "SSID: %s", name[0] ? name : "待输入");
        else if (i == 1) snprintf(buf, sizeof(buf), "密码: %s (%u)",
            s_wifi_password[0] ? "********" : "空", (unsigned)strlen(s_wifi_password));
        else snprintf(buf, sizeof(buf), "保存并连接");
        lv_obj_t *l = label(s_screen, buf, 23, y + 8, 192, 24, 0xFFFFFF, false);
        lv_label_set_long_mode(l, i == s_wifi_field ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
    }
    label(s_screen, "开放网络密码留空", 23, 242, 195, 24, 0xAAC9BA, false);
    footer("上下选择 OK确认");
}

static void keyboard_page(void)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "%s %u/%u 长OK换页 长下取消",
        xigua_keyboard_mode_name(&s_keyboard), s_keyboard.page + 1,
        xigua_keyboard_page_count(&s_keyboard));
    title(s_keyboard_password ? "输入网络密码" : "输入网络名称",
          s_keyboard_full ? "输入已满 退格删除" : buf);
    if (s_keyboard_password) {
        size_t n = strlen(s_keyboard.text);
        snprintf(buf, sizeof(buf), "%u: %s", (unsigned)n, n ? "********" : "");
    } else wifi_name(buf, sizeof(buf), s_keyboard.text);
    lv_obj_t *input = label(s_screen, buf, 20, 99, 200, 25, 0x8DD7AE, false);
    lv_label_set_long_mode(input, LV_LABEL_LONG_SCROLL_CIRCULAR);
    for (unsigned i = 0; i < 25; ++i) {
        const int x = 15 + (int)(i % 5) * 43;
        const int y = 132 + (int)(i / 5) * 27;
        if (i == s_keyboard.selection) box(s_screen, x, y, 41, 25, 0x3D7562, 5);
        lv_obj_t *l = label(s_screen, xigua_keyboard_label(&s_keyboard, i),
            x, y + 2, 41, 22, 0xFFFFFF, false);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }
    footer("上下选键 OK输入");
}

static void later_page(void)
{
    title("后续功能", "记录与联网基础已提供");
    box(s_screen, 15, 107, 210, 141, 0x25413D, 18);
    text_line("语音尚未提供", 122, 0xFFFFFF);
    text_line("USB 可测试 AI 文本", 158, 0xFFFFFF);
    text_line("现有记录仍可使用", 194, 0xA5DAC0);
    footer("长按下返回");
}

static void sound_page(const xigua_ui_view_t *v)
{
    char buf[64];
    title("本地声音", "合成声音 可离线播放");
    box(s_screen, 15, 101, 210, 132, 0x25413D, 18);
    for (unsigned i = 0; i < 3; ++i) {
        if (i == s_sound) box(s_screen, 24, 109 + (int)i * 34, 192, 31, 0x3D7562, 9);
        label(s_screen, sound_names[i], 35, 115 + (int)i * 34, 170, 23, 0xFFFFFF, false);
    }
    if (!v->audio.available) snprintf(buf, sizeof(buf), "声音暂不可用");
    else if (v->audio.busy) snprintf(buf, sizeof(buf), "声音切换中");
    else if (v->audio.error != ESP_OK) snprintf(buf, sizeof(buf), "播放失败 OK重试");
    else if (v->audio.alerting) snprintf(buf, sizeof(buf), "计时提醒 OK停止");
    else if (v->audio.playing && v->audio.track < 3)
        snprintf(buf, sizeof(buf), "播放：%s %u%%", sound_names[v->audio.track], v->audio.volume);
    else snprintf(buf, sizeof(buf), "已暂停 音量 %u%%", v->audio.volume);
    label(s_screen, buf, 24, 240, 192, 28, 0xB9CBC5, false);
    footer("OK播放暂停 长OK音量");
}

static void volume_page(void)
{
    title("声音音量", "本次开机有效 默认40%");
    editor_number("%", s_volume, "0-100% 每次调整5%");
    footer("上下调整 OK返回声音");
}

void xigua_ui_create(void)
{
    if (s_screen) return;
    s_chinese_font = xigua_font_16;
    s_chinese_font.fallback = &lv_font_montserrat_14;
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, color(0x14242B), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    for (unsigned i = 0; i < BOX_COUNT; ++i) {
        s_boxes[i] = lv_obj_create(s_screen);
        lv_obj_add_flag(s_boxes[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (unsigned i = 0; i < LABEL_COUNT; ++i) {
        s_labels[i] = lv_label_create(s_screen);
        lv_obj_add_flag(s_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_page = PAGE_HOME;
    s_home = s_more = s_diaper = 0;
    s_feed_ml = 150;
    s_timer_min = 10;
    s_history_page = 0;
    s_stats_all_time = false;
    s_sound = 0;
    s_volume = XIGUA_AUDIO_VOLUME_DEFAULT;
    s_dirty = true;
    lv_screen_load(s_screen);
}

void xigua_ui_destroy(void)
{
    memset(s_wifi_password, 0, sizeof(s_wifi_password));
    memset(&s_keyboard, 0, sizeof(s_keyboard));
    if (s_screen) lv_obj_delete(s_screen);
    s_screen = NULL;
}

void xigua_ui_render(const xigua_ui_view_t *view)
{
    if (!s_screen || !view) return;
    /* Only state changes or visible minute ticks need LVGL updates. Do not hash
     * pointers/padding or the rapidly changing raw monotonic clock. */
    uint64_t elapsed_sleep = UINT64_MAX, elapsed_tummy = UINT64_MAX;
    uint32_t remaining = UINT32_MAX;
    if (view->state) {
        const xigua_event_t *e = xigua_active_sleep(view->state);
        if (e && xigua_elapsed(e, &view->now, &elapsed_sleep)) elapsed_sleep /= 60000;
        e = xigua_active_tummy(view->state);
        if (e && xigua_elapsed(e, &view->now, &elapsed_tummy)) elapsed_tummy /= 60000;
        if (xigua_timer_remaining(view->state, &view->now, &remaining))
            remaining = (remaining + 59999) / 60000;
    }
    xigua_stats_t visible_stats = {0};
    if (s_page == PAGE_TODAY && view->state && (s_stats_all_time || today_available(view)))
        xigua_stats(view->state, &view->now, s_stats_all_time,
            view->service.day_begin_ms, view->service.day_end_ms, &visible_stats);
    const uint64_t values[] = {
        view->state ? view->state->revision : 0,
        view->state ? view->state->boot_id : 0,
        view->now.boot_id, view->now.quality,
        view->now.unix_ms > 0, (uint64_t)(view->battery_pct + 1),
        view->read_only, view->feedback, view->undo_available,
        view->service.available, view->service.wifi_connected, view->service.wifi_configured,
        view->service.ai_configured, view->service.ai_busy, (uint64_t)(view->service.ai_status + 1),
        view->service.timezone_configured, view->service.config_read_only,
        view->service.wifi_scan_busy, view->service.wifi_scan_generation,
        view->service.wifi_scan_count, (uint64_t)view->service.wifi_scan_error,
        view->service.wifi_config_busy, view->service.wifi_config_generation,
        view->service.wifi_config_result, (uint64_t)view->service.wifi_reason,
        view->service.day_valid, (uint64_t)view->service.day_begin_ms,
        view->audio.available, view->audio.busy, view->audio.playing, view->audio.alerting,
        view->audio.track, view->audio.volume, (uint64_t)view->audio.error,
        (uint64_t)view->service.day_end_ms,
        today_available(view), visible_stats.sleep_ms / 60000,
        visible_stats.tummy_minutes, visible_stats.pending_count,
        (unsigned char)view->service.local_time[0], (unsigned char)view->service.local_time[1],
        (unsigned char)view->service.local_time[3], (unsigned char)view->service.local_time[4],
        elapsed_sleep, elapsed_tummy, remaining
    };
    uint64_t signature = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        signature ^= values[i];
        signature *= UINT64_C(1099511628211);
    }
    if (!s_dirty && signature == s_render_signature) return;
    s_render_signature = signature;
    s_dirty = false;
    s_box_used = s_label_used = 0;
    status_bar(view);
    switch (s_page) {
    case PAGE_HOME: home(view); break;
    case PAGE_FEED: feed_page(view); break;
    case PAGE_SLEEP: sleep_page(view); break;
    case PAGE_DIAPER: diaper_page(view); break;
    case PAGE_TODAY: today_page(view); break;
    case PAGE_MORE: more_page(); break;
    case PAGE_BATH: bath_page(view); break;
    case PAGE_TUMMY: tummy_page(view); break;
    case PAGE_TIMER: timer_page(view); break;
    case PAGE_SETTINGS: settings_page(view); break;
    case PAGE_LATER: later_page(); break;
    case PAGE_HISTORY: history_page(view); break;
    case PAGE_SOUND: sound_page(view); break;
    case PAGE_VOLUME: volume_page(); break;
    case PAGE_WIFI: wifi_page(view); break;
    case PAGE_WIFI_EDIT: wifi_edit_page(view); break;
    case PAGE_KEYBOARD: keyboard_page(); break;
    }
    uint32_t timer_ms;
    if (view->state && xigua_timer_remaining(view->state, &view->now, &timer_ms) && timer_ms == 0) {
        /* All pages show due in the hint slot; the timer page clears it. */
        box(s_screen, 14, 69, 212, 29, 0x7B5733, 9);
        if (strcmp(lv_label_get_text(s_labels[3]), "计时已到 请查看") != 0)
            lv_label_set_text(s_labels[3], "计时已到 请查看");
        lv_obj_set_style_text_color(s_labels[3], color(0xFFFFFF), 0);
    }
    if (s_page != PAGE_WIFI && s_page != PAGE_WIFI_EDIT && s_page != PAGE_KEYBOARD)
        feedback(view);
    for (unsigned i = 0; i < BOX_COUNT; ++i) {
        if (i < s_box_used) lv_obj_remove_flag(s_boxes[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_boxes[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (unsigned i = 0; i < LABEL_COUNT; ++i) {
        if (i < s_label_used) lv_obj_remove_flag(s_labels[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static xigua_ui_intent_t intent(xigua_action_t action, uint32_t value)
{
    return (xigua_ui_intent_t){ .valid = true, .action = action, .value = value };
}

xigua_ui_intent_t xigua_ui_key(xigua_ui_key_t key, const xigua_ui_view_t *v)
{
    xigua_ui_intent_t none = {0};
    if (!s_screen || !v) return none;
    s_dirty = true;
    /* Provisioning consumes its own gestures before global record shortcuts.
     * Service calls below only enqueue work; no networking/NVS under LVGL. */
    if (s_page == PAGE_KEYBOARD) {
        if (key == XIGUA_UI_DOWN_LONG) {
            xigua_keyboard_cancel(&s_keyboard);
            memset(&s_keyboard, 0, sizeof(s_keyboard));
            s_page = PAGE_WIFI_EDIT;
        } else if (key == XIGUA_UI_OK_LONG) xigua_keyboard_next_page(&s_keyboard);
        else if (key == XIGUA_UI_UP_CLICK) xigua_keyboard_move(&s_keyboard, -1);
        else if (key == XIGUA_UI_DOWN_CLICK) xigua_keyboard_move(&s_keyboard, 1);
        else if (key == XIGUA_UI_OK_CLICK) {
            xigua_keyboard_result_t result = xigua_keyboard_press(&s_keyboard);
            s_keyboard_full = result == XIGUA_KEYBOARD_FULL;
            if (result == XIGUA_KEYBOARD_DONE) {
                if (s_keyboard_password)
                    snprintf(s_wifi_password, sizeof(s_wifi_password), "%s", s_keyboard.text);
                else memcpy(s_wifi_ssid, s_keyboard.text, sizeof(s_wifi_ssid));
            }
            if (result == XIGUA_KEYBOARD_DONE || result == XIGUA_KEYBOARD_CANCEL) {
                memset(&s_keyboard, 0, sizeof(s_keyboard));
                s_page = PAGE_WIFI_EDIT;
            }
        }
        return none;
    }
    if (s_page == PAGE_WIFI_EDIT) {
        if (key == XIGUA_UI_DOWN_LONG) {
            memset(s_wifi_password, 0, sizeof(s_wifi_password));
            s_page = PAGE_WIFI;
        } else if (key == XIGUA_UI_UP_CLICK) s_wifi_field = (s_wifi_field + 2) % 3;
        else if (key == XIGUA_UI_DOWN_CLICK) s_wifi_field = (s_wifi_field + 1) % 3;
        else if (key == XIGUA_UI_OK_CLICK && !v->service.wifi_config_busy) {
            if (s_wifi_field == 2) {
                s_wifi_request_failed = !xigua_service_wifi_save(s_wifi_ssid, s_wifi_password);
            } else {
                s_keyboard_password = s_wifi_field == 1;
                xigua_keyboard_init(&s_keyboard,
                    s_keyboard_password ? s_wifi_password : s_wifi_ssid,
                    s_keyboard_password ? sizeof(s_wifi_password) : sizeof(s_wifi_ssid));
                s_keyboard_full = false;
                s_page = PAGE_KEYBOARD;
            }
        }
        return none;
    }
    if (s_page == PAGE_WIFI) {
        const unsigned count = v->service.wifi_scan_count + 2;
        if (key == XIGUA_UI_DOWN_LONG) s_page = PAGE_SETTINGS;
        else if (key == XIGUA_UI_UP_CLICK) s_wifi_choice = (s_wifi_choice + count - 1) % count;
        else if (key == XIGUA_UI_DOWN_CLICK) s_wifi_choice = (s_wifi_choice + 1) % count;
        else if (key == XIGUA_UI_OK_CLICK) {
            if (s_wifi_choice == 0) s_wifi_request_failed = !xigua_service_wifi_scan();
            else if (s_wifi_choice < count) {
                memset(s_wifi_ssid, 0, sizeof(s_wifi_ssid));
                memset(s_wifi_password, 0, sizeof(s_wifi_password));
                if (s_wifi_choice >= 2)
                    memcpy(s_wifi_ssid, v->service.wifi_scan[s_wifi_choice - 2].ssid, sizeof(s_wifi_ssid));
                s_wifi_field = s_wifi_choice == 1 ? 0 : 1;
                s_wifi_request_failed = false;
                s_page = PAGE_WIFI_EDIT;
            }
        }
        return none;
    }
    if (s_page == PAGE_SETTINGS && key == XIGUA_UI_OK_CLICK) {
        s_wifi_choice = 0;
        s_wifi_request_failed = !xigua_service_wifi_scan();
        s_page = PAGE_WIFI;
        return none;
    }
    if (key == XIGUA_UI_DOWN_LONG) {
        if (s_page == PAGE_HOME) return none;
        if (s_page == PAGE_VOLUME) { s_page = PAGE_SOUND; return none; }
        s_page = (s_page == PAGE_BATH || s_page == PAGE_TUMMY ||
                  s_page == PAGE_TIMER || s_page == PAGE_SETTINGS ||
                  s_page == PAGE_LATER) ? PAGE_MORE : PAGE_HOME;
        return none;
    }
    if (key == XIGUA_UI_UP_LONG && s_page != PAGE_FEED) {
        const xigua_event_t *last = v->state ? xigua_latest(v->state, XIGUA_FEED) : NULL;
        s_feed_ml = last && last->value >= 10 && last->value <= 400 ? last->value : 150;
        s_feed_edit = false;
        s_page = PAGE_FEED;
        return none;
    }
    if (s_page == PAGE_HOME) {
        if (key == XIGUA_UI_UP_CLICK) s_home = (s_home + 6) % 7;
        else if (key == XIGUA_UI_DOWN_CLICK) s_home = (s_home + 1) % 7;
        else if (key == XIGUA_UI_OK_CLICK) {
            if (v->undo_available && v->state && v->state->undo.valid && !v->read_only)
                return intent(XIGUA_UNDO, 0);
            switch (s_home) {
            case 0: case 5: s_stats_all_time = false; s_page = PAGE_TODAY; break;
            case 1: {
                const xigua_event_t *last = v->state ? xigua_latest(v->state, XIGUA_FEED) : NULL;
                s_feed_ml = last && last->value >= 10 && last->value <= 400 ? last->value : 150;
                s_feed_edit = false; s_page = PAGE_FEED; break;
            }
            case 2: s_page = PAGE_SLEEP; break;
            case 3: s_page = PAGE_DIAPER; break;
            case 4: s_page = PAGE_SOUND; break;
            default: s_page = PAGE_MORE; break;
            }
        }
        return none;
    }
    if (s_page == PAGE_SOUND) {
        if (key == XIGUA_UI_UP_CLICK) s_sound = (s_sound + 2) % 3;
        else if (key == XIGUA_UI_DOWN_CLICK) s_sound = (s_sound + 1) % 3;
        else if (key == XIGUA_UI_OK_LONG) { s_volume = v->audio.volume; s_page = PAGE_VOLUME; }
        else if (key == XIGUA_UI_OK_CLICK && v->audio.available) {
            const bool pause = v->audio.alerting ||
                (v->audio.requested_playing && v->audio.requested_track == s_sound);
            return (xigua_ui_intent_t){.audio = pause ? XIGUA_UI_AUDIO_PAUSE : XIGUA_UI_AUDIO_PLAY,
                .value = s_sound};
        }
        return none;
    }
    if (s_page == PAGE_VOLUME) {
        if (key == XIGUA_UI_UP_CLICK && s_volume < XIGUA_AUDIO_VOLUME_MAX) s_volume += XIGUA_AUDIO_VOLUME_STEP;
        else if (key == XIGUA_UI_DOWN_CLICK && s_volume >= XIGUA_AUDIO_VOLUME_STEP) s_volume -= XIGUA_AUDIO_VOLUME_STEP;
        else if (key == XIGUA_UI_OK_CLICK) { s_page = PAGE_SOUND; return none; }
        else return none;
        return (xigua_ui_intent_t){.audio = XIGUA_UI_AUDIO_VOLUME, .value = s_volume};
    }
    if (s_page == PAGE_MORE) {
        if (key == XIGUA_UI_UP_CLICK) s_more = (s_more + 4) % 5;
        else if (key == XIGUA_UI_DOWN_CLICK) s_more = (s_more + 1) % 5;
        else if (key == XIGUA_UI_OK_CLICK) {
            static const page_t pages[] = { PAGE_BATH, PAGE_TUMMY, PAGE_TIMER, PAGE_SETTINGS, PAGE_LATER };
            s_page = pages[s_more];
        }
        return none;
    }
    if (s_page == PAGE_TODAY) {
        if (key == XIGUA_UI_UP_CLICK || key == XIGUA_UI_DOWN_CLICK)
            s_stats_all_time = !s_stats_all_time;
        if (key == XIGUA_UI_OK_CLICK) { s_page = PAGE_HISTORY; s_history_page = 0; }
        return none;
    }
    if (s_page == PAGE_HISTORY) {
        const unsigned count = v->state ? v->state->event_count : 0;
        const unsigned pages = count ? (count + 2) / 3 : 1;
        if (key == XIGUA_UI_UP_CLICK && s_history_page > 0) --s_history_page;
        else if (key == XIGUA_UI_DOWN_CLICK && s_history_page + 1 < pages) ++s_history_page;
        else if (key == XIGUA_UI_OK_CLICK) s_page = PAGE_TODAY;
        return none;
    }
    if (s_page == PAGE_FEED) {
        if (key == XIGUA_UI_UP_CLICK && s_feed_ml < 400) s_feed_ml += 10;
        else if (key == XIGUA_UI_DOWN_CLICK && s_feed_ml > 10) s_feed_ml -= 10;
        else if (key == XIGUA_UI_OK_LONG && v->state && xigua_latest(v->state, XIGUA_FEED)) {
            s_feed_edit = !s_feed_edit;
            s_feed_ml = xigua_latest(v->state, XIGUA_FEED)->value;
        } else if (key == XIGUA_UI_OK_CLICK && !v->read_only)
            return intent(s_feed_edit ? XIGUA_FEED_EDIT : XIGUA_FEED_ADD, s_feed_ml);
    } else if (s_page == PAGE_SLEEP && key == XIGUA_UI_OK_CLICK && !v->read_only) {
        return intent(v->state && xigua_active_sleep(v->state) ? XIGUA_SLEEP_STOP : XIGUA_SLEEP_START, 0);
    } else if (s_page == PAGE_DIAPER) {
        if (key == XIGUA_UI_UP_CLICK) s_diaper = (s_diaper + 2) % 3;
        else if (key == XIGUA_UI_DOWN_CLICK) s_diaper = (s_diaper + 1) % 3;
        else if (key == XIGUA_UI_OK_CLICK && !v->read_only) return intent(XIGUA_DIAPER_ADD, s_diaper + 1);
    } else if (s_page == PAGE_BATH && key == XIGUA_UI_OK_CLICK && !v->read_only) {
        return intent(XIGUA_BATH_ADD, 0);
    } else if (s_page == PAGE_TUMMY && key == XIGUA_UI_OK_CLICK && !v->read_only) {
        return intent(v->state && xigua_active_tummy(v->state) ? XIGUA_TUMMY_STOP : XIGUA_TUMMY_START, 0);
    } else if (s_page == PAGE_TIMER && v->state) {
        if (v->state->timer.active && key == XIGUA_UI_OK_CLICK && !v->read_only)
            return intent(XIGUA_TIMER_CANCEL, 0);
        if (!v->state->timer.active) {
            if (key == XIGUA_UI_UP_CLICK && s_timer_min < 1440) s_timer_min += 5;
            else if (key == XIGUA_UI_DOWN_CLICK && s_timer_min > 5) s_timer_min -= 5;
            else if (key == XIGUA_UI_OK_CLICK && !v->read_only)
                return intent(XIGUA_TIMER_START, s_timer_min * 60000);
        }
    }
    return none;
}

void xigua_ui_command_result(bool saved)
{
    s_dirty = true;
    if (saved) {
        s_page = PAGE_HOME;
        s_home = 0;
        s_feed_edit = false;
    }
}
