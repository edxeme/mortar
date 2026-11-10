#include "htab.h"

#include "util.h"

#include <stdlib.h>
#include <string.h>

/* occupied = live + tombstones; grow / sweep when it crosses 70%:
 * occupied * 10 >= capacity * 7  <=>  occupied / capacity >= 0.7 */
#define HT_LOAD_NUM 10u
#define HT_LOAD_DEN 7u

static uint64_t fnv1a64(const char *s) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
        h ^= *p;
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static size_t round_pow2(size_t n) {
    if (n < HT_MIN_CAPACITY) {
        n = HT_MIN_CAPACITY;
    }
    if ((n & (n - 1u)) != 0) {
        size_t p = HT_MIN_CAPACITY;
        while (p < n) {
            p *= 2u;
        }
        n = p;
    }
    return n;
}

int htab_init(htab_t *t, size_t initial_capacity, htab_free_fn free_value,
              size_t key_arena_block) {
    t->capacity = round_pow2(initial_capacity);
    t->entries = xcalloc(t->capacity, sizeof(*t->entries));
    t->size = 0;
    t->tombstones = 0;
    t->rehashes = 0;
    t->free_value = free_value;
    t->key_arena_block = key_arena_block < 512 ? 512 : key_arena_block;
    arena_init(&t->key_arena, t->key_arena_block);
    return 0;
}

/* Rebuild into fresh memory: tombstones vanish, dead keys are dropped,
 * and the old key arena is released wholesale. This is the *only* place
 * interned key memory is reclaimed — individual keys are never freed. */
static void rehash(htab_t *t, size_t new_capacity) {
    htab_entry_t *old = t->entries;
    size_t old_capacity = t->capacity;

    arena_t fresh_arena;
    arena_init(&fresh_arena, t->key_arena_block);

    htab_entry_t *fresh = xcalloc(new_capacity, sizeof(*fresh));
    size_t mask = new_capacity - 1u;

    for (size_t i = 0; i < old_capacity; i++) {
        htab_entry_t *e = &old[i];
        if (e->state != HT_USED) {
            continue;
        }
        char *kcopy = arena_strdup(&fresh_arena, e->key);
        size_t j = (size_t)(e->hash & mask);
        while (fresh[j].state == HT_USED) {
            j = (j + 1u) & mask;
        }
        fresh[j].key = kcopy;
        fresh[j].value = e->value;
        fresh[j].hash = e->hash;
        fresh[j].expires_at = e->expires_at;
        fresh[j].state = HT_USED;
    }

    free(old);
    arena_release(&t->key_arena);
    t->key_arena = fresh_arena;
    t->entries = fresh;
    t->capacity = new_capacity;
    t->tombstones = 0;
    t->rehashes++;
}

int htab_put(htab_t *t, const char *key, void *value, uint64_t expires_at) {
    uint64_t h = fnv1a64(key);
    size_t mask = t->capacity - 1u;
    size_t i = (size_t)(h & mask);
    htab_entry_t *recycle = NULL;

    for (;;) {
        htab_entry_t *e = &t->entries[i];
        if (e->state == HT_EMPTY) {
            break;
        }
        if (e->state == HT_TOMBSTONE) {
            if (recycle == NULL) {
                recycle = e;
            }
        } else if (e->hash == h && strcmp(e->key, key) == 0) {
            if (t->free_value != NULL) {
                t->free_value(e->value);
            }
            e->value = value;
            e->expires_at = expires_at;
            return 0;
        }
        i = (i + 1u) & mask;
    }

    char *kcopy = arena_strdup(&t->key_arena, key);
    htab_entry_t *slot = recycle != NULL ? recycle : &t->entries[i];
    if (recycle != NULL) {
        t->tombstones--;
    }
    slot->key = kcopy;
    slot->value = value;
    slot->hash = h;
    slot->expires_at = expires_at;
    slot->state = HT_USED;
    t->size++;

    if ((t->size + t->tombstones) * HT_LOAD_NUM >= t->capacity * HT_LOAD_DEN) {
        /* grow only when live data alone justifies it; a same-size
         * rehash is enough when the pressure is mostly tombstones */
        size_t new_capacity = t->capacity;
        if (t->size * HT_LOAD_NUM >= t->capacity * HT_LOAD_DEN) {
            new_capacity = t->capacity * 2u;
        }
        rehash(t, new_capacity);
    }
    return 1;
}

void *htab_get(htab_t *t, const char *key, uint64_t now_ms) {
    uint64_t h = fnv1a64(key);
    size_t mask = t->capacity - 1u;

    for (size_t i = (size_t)(h & mask);; i = (i + 1u) & mask) {
        htab_entry_t *e = &t->entries[i];
        if (e->state == HT_EMPTY) {
            return NULL;
        }
        if (e->state == HT_USED && e->hash == h && strcmp(e->key, key) == 0) {
            if (e->expires_at != 0 && now_ms >= e->expires_at) {
                /* lazy eviction on access; tombstoning here is safe
                 * because we return immediately and tombstones keep
                 * probe chains intact */
                if (t->free_value != NULL) {
                    t->free_value(e->value);
                }
                e->key = NULL;
                e->value = NULL;
                e->expires_at = 0;
                e->state = HT_TOMBSTONE;
                t->size--;
                t->tombstones++;
                return NULL;
            }
            return e->value;
        }
    }
}

int htab_del(htab_t *t, const char *key) {
    uint64_t h = fnv1a64(key);
    size_t mask = t->capacity - 1u;

    for (size_t i = (size_t)(h & mask);; i = (i + 1u) & mask) {
        htab_entry_t *e = &t->entries[i];
        if (e->state == HT_EMPTY) {
            return 0;
        }
        if (e->state == HT_USED && e->hash == h && strcmp(e->key, key) == 0) {
            if (t->free_value != NULL) {
                t->free_value(e->value);
            }
            e->key = NULL;
            e->value = NULL;
            e->expires_at = 0;
            e->state = HT_TOMBSTONE;
            t->size--;
            t->tombstones++;
            return 1;
        }
    }
}

int htab_peek(htab_t *t, const char *key, uint64_t *expires_at, void **value) {
    uint64_t h = fnv1a64(key);
    size_t mask = t->capacity - 1u;

    for (size_t i = (size_t)(h & mask);; i = (i + 1u) & mask) {
        htab_entry_t *e = &t->entries[i];
        if (e->state == HT_EMPTY) {
            return 0;
        }
        if (e->state == HT_USED && e->hash == h && strcmp(e->key, key) == 0) {
            if (expires_at != NULL) {
                *expires_at = e->expires_at;
            }
            if (value != NULL) {
                *value = e->value;
            }
            return 1;
        }
    }
}

void htab_release(htab_t *t) {
    if (t->free_value != NULL) {
        for (size_t i = 0; i < t->capacity; i++) {
            if (t->entries[i].state == HT_USED) {
                t->free_value(t->entries[i].value);
            }
        }
    }
    free(t->entries);
    arena_release(&t->key_arena);
    memset(t, 0, sizeof(*t));
}

size_t htab_size(const htab_t *t) {
    return t->size;
}

size_t htab_capacity(const htab_t *t) {
    return t->capacity;
}

size_t htab_tombstones(const htab_t *t) {
    return t->tombstones;
}

size_t htab_rehashes(const htab_t *t) {
    return t->rehashes;
}

size_t htab_key_arena_bytes(const htab_t *t) {
    return arena_bytes(&t->key_arena);
}

size_t htab_key_arena_blocks(const htab_t *t) {
    return arena_blocks(&t->key_arena);
}
