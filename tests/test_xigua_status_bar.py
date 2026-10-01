#!/usr/bin/env python3
"""Exercise the real top-bar refresh with clock, radio and LVGL doubles."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/xigua_app.c").read_text(encoding="utf-8")
START = SOURCE.index("static void ui_refresh_top_bar(void)")
FUNCTION = SOURCE[START:SOURCE.index("\nstatic void ui_set_title", START)]

HARNESS = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "xigua_wifi.h"
#include "xigua_ai.h"
#define X_MIN_VALID_EPOCH 1700000000LL
#define pdTRUE 1
typedef struct { char text[32]; uint32_t color; } lv_obj_t;
static lv_obj_t clock_label, network_label, battery_label;
static lv_obj_t *s_clock = &clock_label, *s_network = &network_label, *s_battery = &battery_label;
static struct { int battery_soc; } s_state;
static int s_mutex = 1, locked, busy;
static xigua_wifi_state_t radio;
static xigua_ai_health_t health;
static time_t epoch;
static time_t mock_time(time_t *out) { if (out) *out = epoch; return epoch; }
#define time mock_time
static int xSemaphoreTake(int mutex, int timeout) {
    assert(mutex == 1 && timeout == 0 && !locked);
    if (busy) return 0;
    return locked = 1;
}
static void xSemaphoreGive(int mutex) { assert(mutex == 1 && locked); locked = 0; }
static uint32_t lv_color_hex(uint32_t color) { return color; }
static void lv_obj_set_style_text_color(lv_obj_t *obj, uint32_t color, int part) {
    assert(part == 0); obj->color = color;
}
static void lv_label_set_text(lv_obj_t *obj, const char *text) {
    assert(strlen(text) < sizeof(obj->text)); strcpy(obj->text, text);
}
static void lv_label_set_text_fmt(lv_obj_t *obj, const char *fmt, ...) {
    va_list args; va_start(args, fmt); vsnprintf(obj->text, sizeof(obj->text), fmt, args); va_end(args);
}
xigua_wifi_state_t xigua_wifi_state(void) { return radio; }
xigua_ai_health_t xigua_ai_health(void) { return health; }
'''

CASES = r'''
int main(void) {
    setenv("TZ", "UTC0", 1); tzset();
    s_state.battery_soc = -1;
    ui_refresh_top_bar();
    assert(!strcmp(clock_label.text, "--:--") && !strcmp(battery_label.text, "--%"));
    epoch = 1790812800; /* 2026-10-01 00:00 UTC */
    setenv("TZ", "CST-8", 1); tzset();
    s_state.battery_soc = 100; radio = XIGUA_WIFI_CONNECTED; health = XIGUA_AI_HEALTH_OFFLINE;
    ui_refresh_top_bar();
    assert(!strcmp(clock_label.text, "08:00") && !strcmp(battery_label.text, "100%"));
    assert(!strcmp(network_label.text, "已连接")); /* IP alone is not Internet proof. */
    health = XIGUA_AI_HEALTH_NETWORK_FAILED; ui_refresh_top_bar();
    assert(!strcmp(network_label.text, "联网失败"));
    health = XIGUA_AI_HEALTH_MODEL_FAILED; ui_refresh_top_bar();
    assert(!strcmp(network_label.text, "已联网")); /* Model failure still has a good HTTPS probe. */
    health = XIGUA_AI_HEALTH_READY; radio = XIGUA_WIFI_CONNECTING; ui_refresh_top_bar();
    assert(!strcmp(network_label.text, "连接中")); /* Ignore stale health after disconnect. */
    radio = XIGUA_WIFI_OFF; ui_refresh_top_bar(); assert(!strcmp(network_label.text, "未连接"));
    radio = XIGUA_WIFI_FAILED; ui_refresh_top_bar(); assert(!strcmp(network_label.text, "连接失败"));
    s_state.battery_soc = -1; ui_refresh_top_bar(); assert(!strcmp(battery_label.text, "--%"));
    epoch += 60; busy = 1; ui_refresh_top_bar();
    assert(!strcmp(clock_label.text, "08:01") && !locked); /* No wait for a storage worker. */
    busy = 0; s_state.battery_soc = 101; ui_refresh_top_bar(); assert(!strcmp(battery_label.text, "--%"));
    setenv("TZ", "UTC0", 1); tzset(); ui_refresh_top_bar(); assert(!strcmp(clock_label.text, "00:01"));
    epoch = 0; ui_refresh_top_bar(); assert(!strcmp(clock_label.text, "--:--"));
    s_clock = NULL; ui_refresh_top_bar(); /* Safe after teardown. */
    puts("Top bar: clock calibration/timezone, network transitions and unavailable battery: PASS");
}
'''

# Idle pages and reply readers need the refresh before page-specific branches.
timer = SOURCE.split("static void ui_timer_cb(lv_timer_t *timer)", 1)[1]
assert re.match(r"\s*\{\s*\(void\)timer;\s*ui_refresh_top_bar\(\);", timer)

# Check the actual selected 16 px font: no larger fallback glyphs in this row.
font = (ROOT / "main/xigua_font_zh16.c").read_text(encoding="utf-8")
unicode_list = re.search(r"unicode_list_1\[\] = \{(.*?)\};", font, re.S).group(1)
points = [int(value, 0) for value in re.findall(r"0x[0-9a-fA-F]+|\d+", unicode_list)]
advances = [int(value) for value in re.findall(r"\.adv_w = (\d+)", font)]
glyphs = {cp: cp - 31 for cp in range(32, 127)}
glyphs.update({8212 + offset: 96 + index for index, offset in enumerate(points)})
for text in re.findall(r'network = "([^"]+)"', FUNCTION):
    assert all(ord(ch) in glyphs for ch in text), f"16 px font missing glyph: {text}"
    assert sum((advances[glyphs[ord(ch)]] + 8) // 16 for ch in text) <= 92, text
assert re.search(r"\.line_height = 19,", font)

with tempfile.TemporaryDirectory(prefix="xigua-status-") as directory:
    (Path(directory) / "esp_err.h").write_text("typedef int esp_err_t;\n")
    path = Path(directory) / "status.c"
    binary = Path(directory) / "status"
    path.write_text(HARNESS + FUNCTION + CASES, encoding="utf-8")
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
        "-I" + directory, "-I" + str(ROOT / "main"),
        str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
