#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define XIGUA_CATALOG_CAPACITY 8
typedef struct { char title[96]; char url[512]; bool white; } xigua_catalog_track_t;
/* Reject unsupported media and credential-bearing requests to another origin. */
static inline bool xigua_catalog_track(xigua_catalog_track_t *out, const char *base,
                                       const char *title, const char *url,
                                       const char *mime, bool white)
{
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
    out->white=white;
    return true;
}
