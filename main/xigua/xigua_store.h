#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XG_STORE_CAPACITY 24576u
#define XG_STORE_HEADER 24u
/* read: 0 found, 1 absent, -1 I/O or oversized. write succeeds only after commit. */
typedef struct {
    void *context;
    int (*read)(void *, unsigned slot, uint8_t *, size_t, size_t *);
    bool (*write)(void *, unsigned slot, const uint8_t *, size_t);
} xigua_store_backend_t;
typedef bool (*xigua_payload_valid_t)(const uint8_t *, size_t, void *);
typedef struct { uint64_t generation; unsigned slot; bool writable; } xigua_store_t;
typedef enum { XG_STORE_EMPTY, XG_STORE_OK, XG_STORE_RECOVERED, XG_STORE_ERROR } xigua_store_result_t;
xigua_store_result_t xigua_store_load(xigua_store_t *, const xigua_store_backend_t *,
    uint8_t *payload, size_t capacity, size_t *length,
    uint8_t *scratch, size_t scratch_size, xigua_payload_valid_t, void *);
bool xigua_store_save(xigua_store_t *, const xigua_store_backend_t *,
    const uint8_t *payload, size_t length, uint8_t *scratch, size_t scratch_size);
