#ifndef MORTAR_STORE_H
#define MORTAR_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "htab.h"

/* mortar's public API: an in-memory key-value store with per-key TTL.
 *
 * Both keys and values are copied on store_set(); the caller's buffers
 * are never retained. Values live in the slab layer (mem.h), keys in
 * the table's interning arena. TTL is lazy: an expired entry is
 * reclaimed the next time it is read (or the next put triggers a
 * tombstone sweep) — there is no background timer thread. */

typedef struct {
    size_t initial_capacity; /* entry slots, rounded to a power of two */
    size_t key_arena_block;  /* key interning arena block size */
} store_config_t;

typedef struct {
    htab_t table;
    uint64_t sets;
    uint64_t gets;
    uint64_t hits;
    uint64_t misses;
    uint64_t deletes;
    uint64_t started_ms;
} store_t;

int store_init(store_t *s, const store_config_t *cfg /* NULL = defaults */);
void store_release(store_t *s);

int store_set(store_t *s, const char *key, const char *value, uint64_t ttl_ms);
const char *store_get(store_t *s, const char *key);

/* returns 1 if the key existed and was removed */
int store_del(store_t *s, const char *key);

/* -1 = exists, no expiry · -2 = missing (or already expired) */
int64_t store_ttl(store_t *s, const char *key);

uint64_t store_uptime_ms(const store_t *s);

#endif
