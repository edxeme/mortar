#ifndef MORTAR_HTAB_H
#define MORTAR_HTAB_H

#include <stddef.h>
#include <stdint.h>

#include "arena.h"

/* Open-addressing hash table, linear probing, FNV-1a hashing.
 *
 * Ownership contract:
 *   - Keys are INTERNED: the table strdups every new key into an
 *     internal arena and keeps it until the next rehash rebuilds the
 *     arena generationally. Callers may free (or reuse) their key
 *     buffer immediately after put().
 *   - Values are OWNED BY THE CALLER-SUPPLIED DESTRUCTOR: whenever the
 *     table drops a value (overwrite, delete, TTL eviction, shutdown)
 *     it calls free_value(). Passing NULL means values are never freed
 *     by the table.
 *
 * Deletion uses tombstones so a probe chain is never broken. When the
 * tombstone population climbs, the next put() sweeps them with a
 * same-capacity rehash; growth only happens when live entries alone
 * justify it. */

typedef void (*htab_free_fn)(void *value);

enum {
    HT_EMPTY = 0,
    HT_USED = 1,
    HT_TOMBSTONE = 2,
};

typedef struct {
    const char *key; /* interned in the table's key arena */
    void *value;
    uint64_t hash;       /* cached FNV-1a of key */
    uint64_t expires_at; /* absolute ms from now_ms(), 0 = no expiry */
    uint8_t state;
} htab_entry_t;

typedef struct {
    htab_entry_t *entries;
    size_t capacity; /* power of two */
    size_t size;     /* live entries */
    size_t tombstones;
    size_t rehashes;
    htab_free_fn free_value;
    arena_t key_arena;
    size_t key_arena_block;
} htab_t;

#define HT_MIN_CAPACITY 64u

int htab_init(htab_t *t, size_t initial_capacity, htab_free_fn free_value,
              size_t key_arena_block);
void htab_release(htab_t *t);

/* returns 1 if inserted, 0 if an existing key was replaced */
int htab_put(htab_t *t, const char *key, void *value, uint64_t expires_at);

/* lazily evicts the entry if now_ms() has passed its expiry */
void *htab_get(htab_t *t, const char *key, uint64_t now_ms);

/* returns 1 if the entry existed and was removed */
int htab_del(htab_t *t, const char *key);

/* pure lookup that never evicts: exposes the raw expiry timestamp */
int htab_peek(htab_t *t, const char *key, uint64_t *expires_at, void **value);

size_t htab_size(const htab_t *t);
size_t htab_capacity(const htab_t *t);
size_t htab_tombstones(const htab_t *t);
size_t htab_rehashes(const htab_t *t);
size_t htab_key_arena_bytes(const htab_t *t);
size_t htab_key_arena_blocks(const htab_t *t);

#endif
