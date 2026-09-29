#include "xigua_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t slots[2][128], scratch[128], payload[100];
static size_t sizes[2];
static bool fail, ambiguous;
static int read_slot(void *ctx, unsigned slot, uint8_t *out, size_t cap, size_t *size)
{
    (void)ctx;
    if (!sizes[slot]) return 1;
    if (sizes[slot] > cap) return -1;
    *size = sizes[slot]; memcpy(out, slots[slot], *size); return 0;
}
static bool write_slot(void *ctx, unsigned slot, const uint8_t *in, size_t size)
{
    (void)ctx;
    if (!fail || ambiguous) { memcpy(slots[slot], in, size); sizes[slot] = size; }
    return !fail;
}
static bool valid(const uint8_t *in, size_t size, void *ctx)
{
    (void)ctx; return size == 3 && in[0] == 'v';
}
int main(void)
{
    xigua_store_t store;
    xigua_store_backend_t backend = {NULL, read_slot, write_slot};
    size_t length;
    assert(xigua_store_load(&store, &backend, payload, sizeof(payload), &length,
        scratch, sizeof(scratch), valid, NULL) == XG_STORE_EMPTY);
    assert(xigua_store_save(&store, &backend, (const uint8_t *)"v01", 3, scratch, sizeof(scratch)));
    assert(xigua_store_save(&store, &backend, (const uint8_t *)"v02", 3, scratch, sizeof(scratch)));
    assert(xigua_store_load(&store, &backend, payload, sizeof(payload), &length,
        scratch, sizeof(scratch), valid, NULL) == XG_STORE_OK);
    assert(length == 3 && !memcmp(payload, "v02", 3));
    fail = true;
    assert(!xigua_store_save(&store, &backend, (const uint8_t *)"v03", 3, scratch, sizeof(scratch)));
    assert(!store.writable && store.generation == 2);
    assert(xigua_store_load(&store, &backend, payload, sizeof(payload), &length,
        scratch, sizeof(scratch), valid, NULL) == XG_STORE_OK);
    assert(!memcmp(payload, "v02", 3));
    ambiguous = true;
    assert(!xigua_store_save(&store, &backend, (const uint8_t *)"v03", 3, scratch, sizeof(scratch)));
    assert(xigua_store_load(&store, &backend, payload, sizeof(payload), &length,
        scratch, sizeof(scratch), valid, NULL) == XG_STORE_OK);
    assert(!memcmp(payload, "v03", 3));
    slots[store.slot][8] ^= 1; /* Generation protected by CRC. */
    assert(xigua_store_load(&store, &backend, payload, sizeof(payload), &length,
        scratch, sizeof(scratch), valid, NULL) == XG_STORE_RECOVERED);
    assert(!store.writable && !memcmp(payload, "v02", 3));
    sizes[0] = sizes[1] = 2;
    assert(xigua_store_load(&store, &backend, payload, sizeof(payload), &length,
        scratch, sizeof(scratch), valid, NULL) == XG_STORE_ERROR);
    assert(!store.writable);
    puts("Xigua dual-slot storage fault tests: PASS");
}
