#include "xigua_config.h"
#include <string.h>

static bool terminated(const char *s, size_t n)
{
    for (size_t i = 0; i < n; ++i) if (!s[i]) return true;
    return false;
}
static bool letters(const char **p)
{
    unsigned n = 0;
    while ((**p >= 'A' && **p <= 'Z') || (**p >= 'a' && **p <= 'z')) { ++*p; ++n; }
    return n >= 3 && n <= 10;
}
static bool number(const char **p, unsigned low, unsigned high)
{
    unsigned n = 0, digits = 0;
    while (**p >= '0' && **p <= '9') {
        if (++digits > 3) return false;
        n = n * 10 + (unsigned)(*(*p)++ - '0');
    }
    return digits && n >= low && n <= high;
}
static bool offset(const char **p)
{
    if (**p == '+' || **p == '-') ++*p;
    if (!number(p, 0, 24)) return false;
    if (**p == ':') { ++*p; if (!number(p, 0, 59)) return false; }
    return true;
}
static bool rule(const char **p)
{
    if (*(*p)++ != 'M' || !number(p, 1, 12) || *(*p)++ != '.' ||
        !number(p, 1, 5) || *(*p)++ != '.' || !number(p, 0, 6)) return false;
    if (**p == '/') { ++*p; if (!offset(p)) return false; }
    return true;
}
bool xigua_config_timezone_valid(const char *s)
{
    if (!s || !terminated(s, XG_TZ_MAX)) return false;
    if (!*s) return true;
    if (!letters(&s) || !offset(&s)) return false;
    if (!*s) return true;
    if (!letters(&s)) return false;
    if (*s != ',' && !offset(&s)) return false;
    if (*s++ != ',' || !rule(&s) || *s++ != ',' || !rule(&s)) return false;
    return !*s;
}
bool xigua_config_ai_valid(const xigua_ai_config_t *ai, bool require_key)
{
    if (!ai || !terminated(ai->endpoint, sizeof(ai->endpoint)) ||
        !terminated(ai->model, sizeof(ai->model)) || !terminated(ai->key, sizeof(ai->key))) return false;
    if (strncmp(ai->endpoint, "https://", 8) || !ai->model[0] || (require_key && !ai->key[0])) return false;
    const char *host = ai->endpoint + 8, *path = strchr(host, '/');
    if (!path || path == host || !path[1]) return false;
    bool port = false, port_digit = false;
    for (const char *p = host; p < path; ++p) {
        if (*p == ':') { if (port || p == host) return false; port = true; }
        else if (port) { if (*p < '0' || *p > '9') return false; port_digit = true; }
        else if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                   (*p >= '0' && *p <= '9') || *p == '.' || *p == '-')) return false;
    }
    if (port && !port_digit) return false;
    for (const char *p = path; *p; ++p)
        if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 || strchr("?#@\\", *p)) return false;
    for (const char *p = ai->model; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || strchr("._:/-", *p))) return false;
    for (const char *p = ai->key; *p; ++p)
        if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127) return false;
    return true;
}
void xigua_config_defaults(xigua_config_t *c)
{
    memset(c, 0, sizeof(*c));
    strcpy(c->ai.endpoint, "https://api.xiaomimimo.com/v1/chat/completions");
    strcpy(c->ai.model, "mimo-v2.6-flash");
}
bool xigua_config_valid(const xigua_config_t *c)
{
    if (!c || !xigua_config_ai_valid(&c->ai, false) || !xigua_config_timezone_valid(c->timezone)) return false;
    for (unsigned i = 0; i < XG_WIFI_PROFILES; ++i) {
        const xigua_wifi_profile_t *w = &c->wifi[i];
        if (!terminated(w->ssid, sizeof(w->ssid)) || !terminated(w->password, sizeof(w->password))) return false;
        size_t n = strlen(w->password);
        if ((w->enabled && !w->ssid[0]) || (n && n < 8)) return false;
        for (const char *p = w->ssid; *p; ++p) if ((unsigned char)*p < 32 || *p == 127) return false;
        for (const char *p = w->password; *p; ++p)
            if ((unsigned char)*p < 32 || (unsigned char)*p > 126 ||
                (n == 64 && !((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F') || (*p >= '0' && *p <= '9')))) return false;
        for (unsigned j = 0; j < i; ++j)
            if (w->ssid[0] && !strcmp(w->ssid, c->wifi[j].ssid)) return false;
    }
    return true;
}
