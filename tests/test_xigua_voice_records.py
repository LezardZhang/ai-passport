#!/usr/bin/env python3
"""Compile the real record transaction with JSON-tree, NVS and clock doubles."""

import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = [
    {"actions": [{"action": "record_feeding", "amount_ml": 150, "ingredient": "FORMULA"}]},
    {"actions": [{"action": "record_feeding", "amount_ml": 5}]},
    {"actions": [{"action": "record_feeding", "amount_ml": 150.5}]},
    {"actions": [{"action": "record_feeding"}]},
    {"actions": [{"action": "erase_flash"}]},
    {"actions": [{"action": "record_bath"}, {"action": "record_tummy"}]},
    {"actions": [{"action": "record_diaper", "kind": "pee"}]},
    {"actions": [{"action": "record_sleep", "duration_min": 30}]},
    {"actions": [{"action": "record_bath"}]},
    {"actions": [{"action": "record_tummy"}]},
    {"actions": [{"action": "start_sleep"}]},
    {"actions": [{"action": "end_sleep"}]},
]


def trees():
    """Use Python's real JSON values to create the parser's public node trees.

    This tests application validation/transactions, not the upstream JSON parser.
    """
    nodes, roots = [], []

    def literal(value):
        return json.dumps(value, ensure_ascii=False)

    def node(value, name=None):
        index = len(nodes)
        nodes.append({})
        fields = {"string": literal(name) if name else "NULL"}
        if isinstance(value, (dict, list)):
            fields["type"] = "1" if isinstance(value, dict) else "2"
            items = value.items() if isinstance(value, dict) else [(None, x) for x in value]
            children = [node(v, k) for k, v in items]
            fields["child"] = f"&nodes[{children[0]}]" if children else "NULL"
            for a, b in zip(children, children[1:]):
                nodes[a]["next"] = f"&nodes[{b}]"
        elif isinstance(value, str):
            fields.update(type="3", valuestring=literal(value))
        else:
            fields.update(type="4", valuedouble=str(value), valueint=str(int(value)))
        nodes[index].update(fields)
        return index

    for fixture in FIXTURES:
        roots.append(node(fixture))
    output = "static cJSON nodes[] = {\n" + ",\n".join(
        "{" + ", ".join(f".{k}={v}" for k, v in fields.items()) + "}"
        for fields in nodes) + "\n};\n"
    output += "static cJSON *roots[] = {" + ",".join(f"&nodes[{x}]" for x in roots) + "};\n"
    output += "static const char *fixtures[] = {" + ",".join(
        json.dumps(json.dumps(x, ensure_ascii=False)) for x in FIXTURES) + "};\n"
    return output


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "xigua_sleep.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_INVALID_RESPONSE 4
#define ESP_ERR_TIMEOUT 5
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
typedef struct cJSON {
    int type, valueint;
    double valuedouble;
    char *string, *valuestring;
    struct cJSON *child, *next;
} cJSON;
'''

DOUBLES = r'''
static x_state_t s_state;
static x_undo_t s_voice_undo;
static x_persisted_t staged, disk;
static x_sleep_times_t staged_times, disk_times;
static bool s_nvs_open = true;
static int s_mutex = 1, s_nvs = 1, locked, fail_set, fail_commits, commits;
static int64_t now_us, now_epoch = 1700000000;
static time_t mock_time(time_t *out) { if (out) *out = now_epoch; return now_epoch; }
#define time mock_time
static struct tm *mock_localtime_r(const time_t *value, struct tm *out) {
    struct tm *result = gmtime(value); /* Controlled UTC calendar for date assertions. */
    if (!result) return NULL;
    *out = *result; return out;
}
#define localtime_r mock_localtime_r
static int64_t esp_timer_get_time(void) { return now_us; }
static int xSemaphoreTake(int mutex, int ticks) {
    (void)ticks; assert(mutex == 1 && !locked); locked = 1; return pdTRUE;
}
static void xSemaphoreGive(int mutex) { assert(mutex == 1 && locked); locked = 0; }
static int nvs_set_blob(int nvs, const char *key, const void *value, size_t size) {
    assert(nvs == 1 && locked);
    if (fail_set) return ESP_FAIL;
    if (!strcmp(key, "state")) {
        assert(size == sizeof(staged)); memcpy(&staged, value, size);
    } else {
        assert(!strcmp(key, "sleep_times") && size == sizeof(staged_times));
        memcpy(&staged_times, value, size);
    }
    return ESP_OK;
}
static int nvs_get_blob(int nvs, const char *key, void *value, size_t *size) {
    assert(nvs == 1 && !strcmp(key, "sleep_times") && *size == sizeof(disk_times));
    memcpy(value, &disk_times, *size); return ESP_OK;
}
static int nvs_set_str(int nvs, const char *key, const char *value) {
    (void)nvs; (void)key; (void)value; return ESP_OK;
}
static int nvs_commit(int nvs) {
    assert(nvs == 1 && locked); ++commits;
    if (fail_commits) { --fail_commits; return ESP_FAIL; }
    disk = staged; disk_times = staged_times; return ESP_OK;
}
static bool cJSON_IsObject(const cJSON *x) { return x && x->type == 1; }
static bool cJSON_IsArray(const cJSON *x) { return x && x->type == 2; }
static bool cJSON_IsString(const cJSON *x) { return x && x->type == 3; }
static bool cJSON_IsNumber(const cJSON *x) { return x && x->type == 4; }
static cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *x, const char *key) {
    for (cJSON *child = x ? x->child : NULL; child; child = child->next)
        if (child->string && !strcmp(child->string, key)) return child;
    return NULL;
}
static int cJSON_GetArraySize(const cJSON *x) {
    int n = 0; for (cJSON *p = x ? x->child : NULL; p; p = p->next) ++n; return n;
}
static cJSON *cJSON_GetArrayItem(const cJSON *x, int n) {
    cJSON *p = x ? x->child : NULL; while (p && n--) p = p->next; return p;
}
static cJSON *cJSON_ParseWithOpts(const char *text, const char **end, bool strict) {
    (void)end; assert(strict);
    for (size_t i = 0; i < sizeof(fixtures)/sizeof(fixtures[0]); ++i)
        if (!strcmp(text, fixtures[i])) return roots[i];
    return NULL;
}
static void cJSON_Delete(cJSON *x) { (void)x; }
'''

CHECKS = r'''
static void reset(void) {
    assert(!locked);
    memset(&s_state, 0, sizeof(s_state)); memset(&s_voice_undo, 0, sizeof(s_voice_undo));
    memset(&disk, 0, sizeof(disk)); staged = disk;
    memset(&disk_times, 0, sizeof(disk_times)); staged_times = disk_times;
    fail_set = fail_commits = commits = 0; now_epoch = 1700000000; now_us = 1000000;
    (void)X_STATE_MAGIC; (void)X_NVS_NAMESPACE;
}
static int run(size_t fixture, bool clipped) {
    char reply[4096]; strcpy(reply, fixtures[fixture]);
    int err = xigua_app_process_ai_reply(reply, sizeof(reply), clipped);
    assert(!locked);
    if (!err) assert(reply[0] != '{'); /* Never show executable JSON as prose. */
    return err;
}
int main(void) {
    reset(); assert(run(0, false) == ESP_OK);
    assert(s_state.data.milk_count == 1 && disk.milk_ml == 150 && disk.milk_ingredient == 0);
    assert(s_state.data.event_count == 1 && s_voice_undo.valid);
    assert(s_state.data.last_milk_epoch == 1700000000 && disk.events[0].epoch == 1700000000);
    assert(feed_record_slot(&disk, 0) == 0 && feed_record_count(&disk) == 1);
    char feed_detail[128];
    format_feed_record(&s_state, 0, feed_detail, sizeof(feed_detail));
    assert(strstr(feed_detail, "时间 ") && strstr(feed_detail, "150") && strstr(feed_detail, "奶粉"));
    for (size_t i = 1; i <= 5; ++i) {
        reset(); assert(run(i, false) != ESP_OK);
        assert(commits == 0 && s_state.data.event_count == 0 && !s_voice_undo.valid);
    }
    reset(); assert(run(0, true) == ESP_ERR_INVALID_SIZE && commits == 0);
    char prose[] = "How much milk should the baby drink?";
    assert(xigua_app_process_ai_reply(prose, sizeof(prose), false) == ESP_OK && commits == 0);
    char bad[] = "{broken command}";
    assert(xigua_app_process_ai_reply(bad, sizeof(bad), false) == ESP_ERR_INVALID_RESPONSE);
    reset(); fail_commits = 1;
    assert(run(0, false) == ESP_FAIL && commits == 2);
    assert(!s_state.data.milk_count && !disk.milk_count && !s_voice_undo.valid);
    reset(); fail_set = 1;
    assert(run(0, false) == ESP_FAIL && !commits && !s_state.data.event_count);
    for (size_t i = 6; i <= 9; ++i) {
        reset(); assert(run(i, false) == ESP_OK && disk.event_count == 1);
    }
    reset(); s_state.data.active = X_ACTIVE_TIMER;
    assert(run(10, false) == ESP_OK && xigua_sleep_running(disk.sleep_end_epoch));
    assert(disk.active == X_ACTIVE_TIMER && !disk.event_count);
    assert(run(10, false) == ESP_ERR_INVALID_ARG); /* Duplicate start is harmless. */
    assert(run(0, false) == ESP_OK && xigua_sleep_running(disk.sleep_end_epoch));
    now_us += 120LL * 60000000; now_epoch += 7200;
    assert(run(11, false) == ESP_OK && !xigua_sleep_running(disk.sleep_end_epoch));
    assert(disk.sleep_count == 1 && disk.sleep_minutes == 120 && disk.active == X_ACTIVE_TIMER);
    assert(disk_times.entries[1].start_epoch == 1700000000);
    assert(disk_times.entries[1].end_epoch == 1700007200);
    assert(sleep_record_slot(&disk, 0) == 1 && sleep_page_count(&disk) == 2);
    char detail[192];
    format_sleep_record(&s_state, 1, detail, sizeof(detail));
    assert(strstr(detail, "开始 ") && strstr(detail, "结束 ") && strstr(detail, "120"));
    x_persisted_t preserved = disk;
    memset(&s_state, 0, sizeof(s_state)); s_state.data = preserved;
    load_sleep_times();
    assert(s_state.sleep_times.entries[1].start_epoch == 1700000000); /* Reboot restores exact start. */
    assert(run(11, false) == ESP_ERR_INVALID_ARG && disk.sleep_count == 1);
    reset(); assert(run(10, false) == ESP_OK);
    s_state.sleep_time_known = false; now_epoch += 1800; /* Reboot / page independent. */
    assert(run(11, false) == ESP_OK && disk.sleep_minutes == 30);
    reset(); assert(run(10, false) == ESP_OK);
    now_us += 60LL * 60000000; fail_commits = 1;
    assert(run(11, false) == ESP_FAIL && xigua_sleep_running(disk.sleep_end_epoch));
    assert(s_state.sleep_time_known && disk.sleep_count == 0 && disk.event_count == 0);
    assert(disk_times.entries[0].start_epoch == 0);
    /* Two sessions keep their own exact seconds, including a midnight crossing. */
    reset(); now_epoch = 1700006390; assert(run(10, false) == ESP_OK);
    assert(sleep_page_count(&disk) == 2);
    now_epoch += 125; now_us += 125000000;
    assert(run(11, false) == ESP_OK);
    format_sleep_record(&s_state, 0, detail, sizeof(detail));
    assert(strstr(detail, "11-14 23:59") && strstr(detail, "11-15 00:01"));
    int64_t first_start = disk_times.entries[0].start_epoch;
    now_epoch += 3600; assert(run(10, false) == ESP_OK);
    now_epoch += 185; now_us += 185000000;
    assert(run(11, false) == ESP_OK && disk_times.entries[0].start_epoch == first_start);
    assert(disk_times.entries[1].start_epoch == 1700010115);
    assert(disk_times.entries[1].end_epoch == 1700010300);
    assert(sleep_record_slot(&disk, 0) == 1 && sleep_record_slot(&disk, 1) == 0);
    /* Wrapping the general event ring clears stale times even for another event type. */
    for (size_t i = 0; i < 31; ++i) assert(run(8, false) == ESP_OK);
    assert(disk.events[0].type == X_EVENT_BATH && disk_times.entries[0].start_epoch == 0);
    /* Old duration-only records never fabricate a start equal to the end. */
    reset(); now_epoch = 1700100000; assert(run(7, false) == ESP_OK);
    disk_times.magic = 0; s_state.data.sleep_start_epoch = s_state.data.sleep_end_epoch;
    load_sleep_times();
    assert(s_state.sleep_times.entries[0].start_epoch == 0);
    /* Backfill the latest old real session, leaving other missing starts unknown. */
    s_state.data.sleep_start_epoch = s_state.data.sleep_end_epoch - 1800;
    load_sleep_times();
    assert(s_state.sleep_times.entries[0].start_epoch == 1700098200);
    /* Untrusted wall clock keeps duration but never claims a valid date/time. */
    reset(); now_epoch = 0; assert(run(10, false) == ESP_OK);
    now_us += 60000000; now_epoch = 60;
    assert(run(11, false) == ESP_OK && disk.sleep_minutes == 1);
    assert(disk_times.entries[0].start_epoch == 0 && disk_times.entries[0].end_epoch == 0);
    puts("Voice records, exact sleep times, reboot/migration, ring paging and NVS rollback: PASS");
}
'''


def main():
    source = (ROOT / "main/xigua_app.c").read_text(encoding="utf-8")
    types = source[source.index("typedef enum {\n    X_ACTIVE_NONE"):source.index("static lv_obj_t *s_screen;")]
    food = re.search(r"static const char \*const FEED_INGREDIENTS\[\] = .*?;", source).group(0)
    functions = []
    for name in ("state_save_locked", "append_event_locked", "append_sleep_event_locked",
                 "json_event_time", "ingredient_from_json", "format_sleep_time", "event_is_today",
                 "sleep_record_slot",
                 "feed_record_slot", "feed_record_count", "format_feed_record",
                 "sleep_page_count", "format_sleep_record", "load_sleep_times",
                 "json_number_in_range", "apply_sleep_action_locked", "apply_ai_action_locked",
                 "xigua_app_process_ai_reply"):
        match = re.search(rf"^(?:static )?[^\n]+\b{name}\([^;]*?\)\n\{{.*?^\}}", source, re.M | re.S)
        assert match, name
        functions.append(match.group(0))
    with tempfile.TemporaryDirectory(prefix="xigua-voice-records-") as directory:
        path = Path(directory)
        test = path / "records.c"
        exe = path / ("records.exe" if os.name == "nt" else "records")
        test.write_text(PREFIX + types + food + trees() + DOUBLES + "\n".join(functions) + CHECKS,
                        encoding="utf-8")
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                       ["-std=c11", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "main"),
                        str(test), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
