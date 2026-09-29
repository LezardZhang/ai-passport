#include "xigua_store.h"
#include <string.h>

static uint64_t get_le(const uint8_t *p, unsigned n)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i) v |= (uint64_t)p[i] << (i * 8);
    return v;
}
static void put_le(uint8_t *p, uint64_t v, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) p[i] = (uint8_t)(v >> (i * 8));
}
static uint32_t checksum(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        /* Header checksum itself is treated as zeros. */
        uint8_t byte = i >= 20 && i < 24 ? 0 : data[i];
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

xigua_store_result_t xigua_store_load(xigua_store_t *store, const xigua_store_backend_t *backend,
    uint8_t *payload, size_t capacity, size_t *length, uint8_t *scratch, size_t scratch_size,
    xigua_payload_valid_t valid, void *context)
{
    if (!store || !backend || !backend->read || !payload || !length || !scratch || !valid)
        return XG_STORE_ERROR;
    *store = (xigua_store_t){0};
    *length = 0;
    bool found = false, damaged = false;
    for (unsigned slot = 0; slot < 2; ++slot) {
        size_t size = 0;
        int result = backend->read(backend->context, slot, scratch, scratch_size, &size);
        if (result == 1) continue;
        if (result != 0 || size < XG_STORE_HEADER || size > scratch_size ||
            memcmp(scratch, "XGST", 4) || get_le(scratch + 4, 4) != 1 ||
            get_le(scratch + 16, 4) != size - XG_STORE_HEADER ||
            get_le(scratch + 20, 4) != checksum(scratch, size) ||
            size - XG_STORE_HEADER > capacity ||
            !valid(scratch + XG_STORE_HEADER, size - XG_STORE_HEADER, context)) {
            damaged = true;
            continue;
        }
        uint64_t generation = get_le(scratch + 8, 8);
        if (!generation || (found && generation == store->generation)) {
            damaged = true;
            continue;
        }
        if (!found || generation > store->generation) {
            *length = size - XG_STORE_HEADER;
            memcpy(payload, scratch + XG_STORE_HEADER, *length);
            store->generation = generation;
            store->slot = slot;
            found = true;
        }
    }
    store->writable = !damaged;
    if (!found) return damaged ? XG_STORE_ERROR : XG_STORE_EMPTY;
    return damaged ? XG_STORE_RECOVERED : XG_STORE_OK;
}

bool xigua_store_save(xigua_store_t *store, const xigua_store_backend_t *backend,
    const uint8_t *payload, size_t length, uint8_t *scratch, size_t scratch_size)
{
    if (!store || !backend || !backend->write || !payload || !scratch || !store->writable ||
        !length || length > XG_STORE_CAPACITY || scratch_size < XG_STORE_HEADER ||
        length > scratch_size - XG_STORE_HEADER || store->generation == UINT64_MAX) return false;
    memset(scratch, 0, XG_STORE_HEADER);
    memcpy(scratch, "XGST", 4);
    put_le(scratch + 4, 1, 4);
    put_le(scratch + 8, store->generation + 1, 8);
    put_le(scratch + 16, length, 4);
    memcpy(scratch + XG_STORE_HEADER, payload, length);
    put_le(scratch + 20, checksum(scratch, XG_STORE_HEADER + length), 4);
    unsigned slot = store->generation ? 1u - store->slot : 0;
    if (!backend->write(backend->context, slot, scratch, XG_STORE_HEADER + length)) {
        /* Commit outcome can be ambiguous. Require reload before another write. */
        store->writable = false;
        return false;
    }
    store->slot = slot;
    store->generation++;
    return true;
}
