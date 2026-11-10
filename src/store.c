#include "store.h"

#include "mem.h"
#include "util.h"

#include <string.h>

static void value_free(void *v) {
    mem_free(v);
}

int store_init(store_t *s, const store_config_t *cfg) {
    store_config_t c;
    if (cfg != NULL) {
        c = *cfg;
    } else {
        c.initial_capacity = 1024;
        c.key_arena_block = 4096;
    }
    if (c.initial_capacity < HT_MIN_CAPACITY) {
        c.initial_capacity = HT_MIN_CAPACITY;
    }
    if (c.key_arena_block < 512) {
        c.key_arena_block = 512;
    }

    memset(s, 0, sizeof(*s));
    s->started_ms = now_ms();
    return htab_init(&s->table, c.initial_capacity, value_free,
                     c.key_arena_block);
}

void store_release(store_t *s) {
    htab_release(&s->table);
}

int store_set(store_t *s, const char *key, const char *value, uint64_t ttl_ms) {
    size_t n = strlen(value) + 1u;
    char *vcopy = mem_alloc(n);
    memcpy(vcopy, value, n);

    uint64_t expires_at = ttl_ms != 0 ? now_ms() + ttl_ms : 0;
    htab_put(&s->table, key, vcopy, expires_at);
    s->sets++;
    return 0;
}

const char *store_get(store_t *s, const char *key) {
    s->gets++;
    void *v = htab_get(&s->table, key, now_ms());
    if (v != NULL) {
        s->hits++;
        return v;
    }
    s->misses++;
    return NULL;
}

int store_del(store_t *s, const char *key) {
    int removed = htab_del(&s->table, key);
    if (removed) {
        s->deletes++;
    }
    return removed;
}

int64_t store_ttl(store_t *s, const char *key) {
    uint64_t expires_at = 0;
    if (!htab_peek(&s->table, key, &expires_at, NULL)) {
        return -2;
    }
    if (expires_at == 0) {
        return -1;
    }
    uint64_t now = now_ms();
    if (now >= expires_at) {
        return -2; /* logically expired, awaiting lazy eviction */
    }
    return (int64_t)(expires_at - now);
}

uint64_t store_uptime_ms(const store_t *s) {
    return now_ms() - s->started_ms;
}
