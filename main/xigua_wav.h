#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
static inline uint32_t xigua_wav_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static inline bool xigua_wav_header(const uint8_t *p, uint32_t *bytes)
{
    if (memcmp(p,"RIFF",4) || memcmp(p+8,"WAVEfmt ",8) || memcmp(p+36,"data",4) ||
        xigua_wav_u32(p+16)!=16 || p[20]!=1 || p[21]!=0 || p[22]!=1 || p[23]!=0 ||
        xigua_wav_u32(p+24)!=12000 || xigua_wav_u32(p+28)!=24000 || p[32]!=2 || p[33]!=0 || p[34]!=16 || p[35]!=0) return false;
    *bytes=xigua_wav_u32(p+40);
    return *bytes>0 && *bytes%2==0 && *bytes<=25U*1024U*1024U-44 && xigua_wav_u32(p+4)==*bytes+36;
}

/* Range responses must continue the exact canonical PCM payload. */
static inline bool xigua_wav_range(const char *range, uint32_t offset, uint32_t total)
{
    unsigned first,last,size; char extra;
    return offset<total && offset%2==0 &&
        sscanf(range,"bytes %u-%u/%u%c",&first,&last,&size,&extra)==3 &&
        first==44U+offset && size==44U+total && last+1U==size;
}
