#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#define XIGUA_CLOUD_CAPACITY 32
#define XIGUA_CLOUD_MAGIC 0x58435331U
/* Separate NVS blob: original state and sleep_times retain their layout. */
typedef struct {
    uint32_t magic;
    uint64_t next_seq;
    uint64_t revision;
    uint64_t ids[XIGUA_CLOUD_CAPACITY];
} xigua_cloud_state_t;
static inline void xigua_cloud_init(xigua_cloud_state_t *s, size_t head, size_t count)
{
    memset(s, 0, sizeof(*s));
    s->magic = XIGUA_CLOUD_MAGIC;
    s->next_seq = 1;
    for (size_t i = 0; i < count && i < XIGUA_CLOUD_CAPACITY; ++i) {
        size_t slot = (head + XIGUA_CLOUD_CAPACITY - count + i) % XIGUA_CLOUD_CAPACITY;
        s->ids[slot] = s->next_seq++;
    }
}
static inline uint64_t xigua_cloud_floor(const xigua_cloud_state_t *s, size_t head, size_t count)
{
    uint64_t floor = s->next_seq > XIGUA_CLOUD_CAPACITY ? s->next_seq - XIGUA_CLOUD_CAPACITY : 1;
    if (count) {
        floor = UINT64_MAX;
        for (size_t i = 0; i < count; ++i) {
            uint64_t id = s->ids[(head + XIGUA_CLOUD_CAPACITY - count + i) % XIGUA_CLOUD_CAPACITY];
            if (id && id < floor) floor = id;
        }
        if (floor == UINT64_MAX) floor = 1;
    }
    return floor;
}
