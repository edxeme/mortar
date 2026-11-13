#ifndef MORTAR_STORE_H
#define MORTAR_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "htab.h"
#include "timers.h"

/* mortar's public API: an in-memory key-value store with per-key TTL.
 *
 * Both keys and values are copied on store_set(); the caller's buffers
 * are never retained. Values live in the slab layer (mem.h), keys in
 * the table's interning arena.
 *
 * TTL works in two layers: a timer wheel evicts expired keys
 * aggressively on store_tick() (single-threaded — no background
 * thread; the caller drives the clock), and lazy eviction on
 * store_get() remains the correctness backstop for anything the wheel
 * has not reached yet. */

typedef struct {
    size_t initial_capacity; /* entry slots, rounded to a power of two */
    size_t key_arena_block;  /* key interning arena block size */
} store_config_t;

typedef struct {
    htab_t table;
    timer_wheel_t wheel; /* active expiry: timers per TTL'd key */
    uint64_t sets;
    uint64_t gets;
    uint64_t hits;
    uint64_t misses;
    uint64_t deletes;
    uint64_t expirations; /* reclaimations by the wheel, not by reads */
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

/* advances the expiry wheel and evicts every TTL'd key whose deadline
 * has passed. Call this periodically (the REPL does it once per
 * command); lazy eviction on store_get remains the correctness
 * backstop between ticks. Returns the number of keys evicted. */
size_t store_tick(store_t *s);

uint64_t store_uptime_ms(const store_t *s);

#endif
