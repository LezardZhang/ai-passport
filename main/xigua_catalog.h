#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define XIGUA_CATALOG_CAPACITY 16
#define XIGUA_CATALOG_CATEGORY_CAPACITY 16
typedef struct {
    char title[96];
    char url[512];
    char category[XIGUA_CATALOG_CATEGORY_CAPACITY];
    bool white;
} xigua_catalog_track_t;
/* Validate and copy a server category while keeping the old boolean field for
 * firmware callers that only distinguish white noise from other audio. */
static inline bool xigua_catalog_track_category(xigua_catalog_track_t *out, const char *base,
                                                 const char *title, const char *url,
                                                 const char *mime, const char *category)
{
    if (!category || !category[0]) return false;
    bool white = !strcmp(category, "white_noise");
    if (strcmp(category, "song") && strcmp(category, "story") &&
        strcmp(category, "classical") && !white) return false;
    if (!out || !base || !base[0] || !title || !title[0] || !url || !mime || strcmp(mime,"audio/wav")) return false;
    size_t prefix=strlen(base), length=strlen(url);
    if (length>=sizeof(out->url) || length<prefix+7 || strncmp(url,base,prefix) ||
        strncmp(url+prefix,"/media/",7)) return false;
    memset(out,0,sizeof(*out));
    memcpy(out->url,url,length+1);
    size_t n=strlen(title);
    if (n>=sizeof(out->title)) {
        n=sizeof(out->title)-1;
        while (n && ((unsigned char)title[n]&0xc0)==0x80) --n;
    }
    memcpy(out->title,title,n);
    snprintf(out->category, sizeof(out->category), "%s", category);
    out->white=white;
    return true;
}
/* Reject unsupported media and credential-bearing requests to another origin. */
static inline bool xigua_catalog_track(xigua_catalog_track_t *out, const char *base,
                                       const char *title, const char *url,
                                       const char *mime, bool white)
{
    return xigua_catalog_track_category(out, base, title, url, mime,
                                        white ? "white_noise" : "song");
}
