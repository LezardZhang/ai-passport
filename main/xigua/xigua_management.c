#include "xigua_management.h"
#include "cJSON.h"
#include "xigua_json.h"
#include "xigua_core.h"
#include <limits.h>
#include <string.h>

static bool unique(const cJSON *root)
{
    for (const cJSON *a = root->child; a; a = a->next) {
        if (cJSON_IsObject(root)) {
            if (!a->string) return false;
            for (const cJSON *b = a->next; b; b = b->next)
                if (b->string && !strcmp(a->string, b->string)) return false;
        }
        if (!unique(a)) return false;
    }
    return true;
}
static cJSON *parse(const char *s, size_t n, size_t limit)
{
    if (n > limit || !xigua_json_valid(s, n)) return NULL;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(s, n, &end, false);
    if (j) {
        while (end < s + n && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) ++end;
        if (end != s + n || !cJSON_IsObject(j) || !unique(j)) { cJSON_Delete(j); j = NULL; }
    }
    return j;
}
static bool fields(const cJSON *j, const char *const *names, unsigned count)
{
    if (!cJSON_IsObject(j) || cJSON_GetArraySize(j) != (int)count) return false;
    for (unsigned i = 0; i < count; ++i) if (!cJSON_GetObjectItemCaseSensitive(j, names[i])) return false;
    return true;
}
static bool string(const cJSON *j, const char *key, char *out, size_t capacity)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsString(v) || !v->valuestring) return false;
    size_t n = strlen(v->valuestring);
    if (n >= capacity || !xigua_utf8_valid(v->valuestring, n)) return false;
    memcpy(out, v->valuestring, n + 1); return true;
}
static bool integer(const cJSON *j, const char *key, int low, int high, int *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < low || v->valuedouble > high ||
        v->valuedouble != (double)v->valueint) return false;
    *out = v->valueint; return true;
}
static bool uint32_field(const cJSON *j, const char *key, uint32_t *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > UINT32_MAX ||
        v->valuedouble != (double)(uint32_t)v->valuedouble) return false;
    *out = (uint32_t)v->valuedouble; return true;
}
static bool wifi(const cJSON *j, xigua_wifi_profile_t *w)
{
    int p; const cJSON *e = cJSON_GetObjectItemCaseSensitive(j, "enabled");
    if (!string(j, "ssid", w->ssid, sizeof(w->ssid)) || !string(j, "password", w->password, sizeof(w->password)) ||
        !integer(j, "priority", 0, 255, &p) || !cJSON_IsBool(e)) return false;
    w->priority = (uint8_t)p; w->enabled = cJSON_IsTrue(e); return true;
}
static bool ai(const cJSON *j, xigua_ai_config_t *a)
{
    return string(j, "endpoint", a->endpoint, sizeof(a->endpoint)) &&
        string(j, "key", a->key, sizeof(a->key)) && string(j, "model", a->model, sizeof(a->model));
}
bool xigua_management_parse(const char *s, size_t n, xigua_management_request_t *out)
{
    if (!out) return false;
    cJSON *j = parse(s, n, XG_COMMAND_MAX); if (!j) return false;
    xigua_management_request_t r = {0}; char op[20]; int slot = 0, count = 0, index = 0;
    bool ok = integer(j, "id", 1, INT_MAX, &r.id) && string(j, "op", op, sizeof(op));
    static const char *const basic[] = {"id", "op"};
    static const char *const ws[] = {"id", "op", "slot", "ssid", "password", "enabled", "priority"};
    static const char *const wc[] = {"id", "op", "slot"};
    static const char *const as[] = {"id", "op", "endpoint", "key", "model"};
    static const char *const ts[] = {"id", "op", "timezone"};
    static const char *const ask[] = {"id", "op", "text"};
    static const char *const record[] = {"id", "op", "boot_id", "revision", "count", "index"};
    if (ok && !strcmp(op, "status")) { r.op = XG_M_STATUS; ok = fields(j, basic, 2); }
    else if (ok && !strcmp(op, "wifi.set")) {
        r.op = XG_M_WIFI_SET; ok = fields(j, ws, 7) && integer(j, "slot", 0, 7, &slot) && wifi(j, &r.wifi) && r.wifi.ssid[0];
    } else if (ok && !strcmp(op, "wifi.clear")) {
        r.op = XG_M_WIFI_CLEAR; ok = fields(j, wc, 3) && integer(j, "slot", 0, 7, &slot);
    } else if (ok && !strcmp(op, "ai.set")) {
        r.op = XG_M_AI_SET; ok = fields(j, as, 5) && ai(j, &r.ai) && xigua_config_ai_valid(&r.ai, true);
    } else if (ok && !strcmp(op, "ai.clear")) { r.op = XG_M_AI_CLEAR; ok = fields(j, basic, 2); }
    else if (ok && !strcmp(op, "time.set")) {
        r.op = XG_M_TIME_SET; ok = fields(j, ts, 3) && string(j, "timezone", r.timezone, sizeof(r.timezone)) && xigua_config_timezone_valid(r.timezone);
    } else if (ok && !strcmp(op, "ai.ask")) {
        r.op = XG_M_AI_ASK; ok = fields(j, ask, 3) && string(j, "text", r.text, sizeof(r.text)) && r.text[0];
    } else if (ok && !strcmp(op, "ai.test")) { r.op = XG_M_AI_TEST; ok = fields(j, basic, 2); }
    else if (ok && !strcmp(op, "records.begin")) { r.op = XG_M_RECORDS_BEGIN; ok = fields(j, basic, 2); }
    else if (ok && (!strcmp(op, "records.item") || !strcmp(op, "records.finish"))) {
        r.op = !strcmp(op, "records.item") ? XG_M_RECORDS_ITEM : XG_M_RECORDS_FINISH;
        ok = fields(j, record, 6) && uint32_field(j, "boot_id", &r.boot_id) &&
            uint32_field(j, "revision", &r.revision) &&
            integer(j, "count", 0, XIGUA_EVENT_CAPACITY, &count) &&
            integer(j, "index", 0, XIGUA_EVENT_CAPACITY, &index);
        r.count = (uint16_t)count; r.index = (uint16_t)index;
        if (ok && r.op == XG_M_RECORDS_ITEM) ok = index < count;
        if (ok && r.op == XG_M_RECORDS_FINISH) ok = index == count;
    }
    else ok = false;
    r.slot = (unsigned)slot;
    if (ok && r.op == XG_M_WIFI_SET) {
        xigua_config_t c; xigua_config_defaults(&c); c.wifi[slot] = r.wifi; ok = xigua_config_valid(&c);
    }
    if (ok) *out = r;
    memset(&r, 0, sizeof(r)); cJSON_Delete(j); return ok;
}
bool xigua_management_apply(const xigua_management_request_t *r, xigua_config_t *c)
{
    if (!r || !c || r->slot >= XG_WIFI_PROFILES) return false;
    xigua_config_t next = *c;
    switch (r->op) {
    case XG_M_WIFI_SET: next.wifi[r->slot] = r->wifi; break;
    case XG_M_WIFI_CLEAR: memset(&next.wifi[r->slot], 0, sizeof(next.wifi[0])); break;
    case XG_M_AI_SET: next.ai = r->ai; break;
    case XG_M_AI_CLEAR: memset(next.ai.key, 0, sizeof(next.ai.key)); break;
    case XG_M_TIME_SET: memcpy(next.timezone, r->timezone, sizeof(next.timezone)); break;
    default: return false;
    }
    bool ok = xigua_config_valid(&next);
    if (ok) *c = next;
    memset(&next, 0, sizeof(next)); return ok;
}
int xigua_management_wifi_upsert(xigua_config_t *c, const char *ssid, const char *password)
{
    if (!c || !ssid || !password) return -1;
    size_t sn = 0, pn = 0;
    while (sn < 33 && ssid[sn]) ++sn;
    while (pn < 65 && password[pn]) ++pn;
    if (!sn || sn > 32 || pn > 64 || !xigua_utf8_valid(ssid, sn)) return -1;
    int slot = -1;
    for (unsigned i = 0; i < XG_WIFI_PROFILES; ++i)
        if (!strcmp(c->wifi[i].ssid, ssid)) { slot = (int)i; break; }
    if (slot < 0) for (unsigned i = 0; i < XG_WIFI_PROFILES; ++i)
        if (!c->wifi[i].ssid[0]) { slot = (int)i; break; }
    if (slot < 0) return -2;
    xigua_config_t next = *c;
    xigua_management_request_t request = {.op = XG_M_WIFI_SET, .slot = (unsigned)slot};
    memcpy(request.wifi.ssid, ssid, sn + 1);
    memcpy(request.wifi.password, password, pn + 1);
    request.wifi.enabled = true;
    /* Lower numeric priority wins. Preserve peers' ordering with saturation. */
    for (unsigned i = 0; i < XG_WIFI_PROFILES; ++i)
        if ((int)i != slot && next.wifi[i].ssid[0] && next.wifi[i].priority < UINT8_MAX)
            ++next.wifi[i].priority;
    bool ok = xigua_management_apply(&request, &next);
    if (ok) *c = next;
    memset(&request, 0, sizeof(request)); memset(&next, 0, sizeof(next));
    return ok ? slot : -1;
}
size_t xigua_config_encode(const xigua_config_t *c, char *s, size_t cap)
{
    if (!s || cap > INT_MAX || !xigua_config_valid(c)) return 0;
    cJSON *j = cJSON_CreateObject(), *a = NULL, *w = NULL; bool ok = j != NULL;
    if (ok) ok = cJSON_AddNumberToObject(j, "version", 1) && cJSON_AddStringToObject(j, "timezone", c->timezone);
    if (ok) { a = cJSON_AddObjectToObject(j, "ai"); w = cJSON_AddArrayToObject(j, "wifi"); ok = a && w; }
    if (ok) ok = cJSON_AddStringToObject(a, "endpoint", c->ai.endpoint) &&
        cJSON_AddStringToObject(a, "key", c->ai.key) && cJSON_AddStringToObject(a, "model", c->ai.model);
    for (unsigned i = 0; ok && i < XG_WIFI_PROFILES; ++i) {
        cJSON *p = cJSON_CreateObject();
        if (!p || !cJSON_AddItemToArray(w, p)) { cJSON_Delete(p); ok = false; break; }
        ok = cJSON_AddStringToObject(p, "ssid", c->wifi[i].ssid) && cJSON_AddStringToObject(p, "password", c->wifi[i].password) &&
             cJSON_AddBoolToObject(p, "enabled", c->wifi[i].enabled) && cJSON_AddNumberToObject(p, "priority", c->wifi[i].priority);
    }
    if (ok) ok = cJSON_PrintPreallocated(j, s, (int)cap, false);
    size_t n = ok ? strlen(s) : 0; cJSON_Delete(j); return n;
}
bool xigua_config_decode(xigua_config_t *out, const char *s, size_t n)
{
    if (!out) return false;
    cJSON *j = parse(s, n, XG_CONFIG_WIRE_MAX); if (!j) return false;
    static const char *const root[] = {"version", "timezone", "ai", "wifi"};
    static const char *const af[] = {"endpoint", "key", "model"};
    static const char *const wf[] = {"ssid", "password", "enabled", "priority"};
    xigua_config_t c = {0}; int v;
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(j, "ai"), *w = cJSON_GetObjectItemCaseSensitive(j, "wifi");
    bool ok = fields(j, root, 4) && integer(j, "version", 1, 1, &v) && fields(a, af, 3) && ai(a, &c.ai) &&
        string(j, "timezone", c.timezone, sizeof(c.timezone)) && cJSON_IsArray(w) && cJSON_GetArraySize(w) == XG_WIFI_PROFILES;
    for (unsigned i = 0; ok && i < XG_WIFI_PROFILES; ++i) {
        const cJSON *p = cJSON_GetArrayItem(w, (int)i); ok = fields(p, wf, 4) && wifi(p, &c.wifi[i]);
    }
    ok = ok && xigua_config_valid(&c);
    if (ok) *out = c;
    memset(&c, 0, sizeof(c)); cJSON_Delete(j); return ok;
}
