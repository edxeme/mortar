#include "store.h"

#include "mem.h"
#include "util.h"

#include <string.h>

static void value_free(void *v) {
    mem_free(v);
}

/* Runs when the wheel reaches a timer's deadline. The timer may be
 * stale: the key may have been overwritten with a new TTL (or none) or
 * deleted after the timer was scheduled. Compare the fired deadline
 * against the entry's current one and drop mismatches — no eager
 * cancellation chase on the write path. htab_del routes the value
 * through the table's destructor, which is the slab pool's free. */
static void wheel_fire(const char *key, uint64_t fired_expires_at,
                       void *userdata) {
    store_t *s = userdata;
    uint64_t current = 0;
    if (!htab_peek(&s->table, key, &current, NULL)) {
        return; /* already gone via DEL; nothing to reclaim */
    }
    if (current != fired_expires_at) {
        return; /* stale timer: the entry has a different deadline now */
    }
    htab_del(&s->table, key);
    s->expirations++;
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
    tw_init(&s->wheel);
    tw_start(&s->wheel, s->started_ms);
    return htab_init(&s->table, c.initial_capacity, value_free,
                     c.key_arena_block);
}

/* Replaces a key's timer with one for the new deadline. Eager cancel
 * would walk 256 buckets on every overwrite; instead the old timer is
 * left to fire and stale-fire is discarded by deadline comparison in
 * wheel_fire. Only schedule when there IS a deadline: a plain SET must
 * cancel the previous timer so a prior TTL cannot outlive the
 * overwrite — that needs the sweep, so it is the one eager walk. */
static void reschedule_timer(store_t *s, const char *key,
                             uint64_t expires_at) {
    if (expires_at == 0) {
        tw_cancel(&s->wheel, key);
        return;
    }
    tw_add(&s->wheel, key, expires_at);
}

void store_release(store_t *s) {
    htab_release(&s->table);
    tw_release(&s->wheel);
}

int store_set(store_t *s, const char *key, const char *value, uint64_t ttl_ms) {
    size_t n = strlen(value) + 1u;
    char *vcopy = mem_alloc(n);
    memcpy(vcopy, value, n);

    uint64_t expires_at = ttl_ms != 0 ? now_ms() + ttl_ms : 0;
    reschedule_timer(s, key, expires_at);
    htab_put(&s->table, key, vcopy, expires_at);
    s->sets++;
    return 0;
}

const char *store_get(store_t *s, const char *key) {
    s->gets++;
    int evicted = 0;
    void *v = htab_get_ex(&s->table, key, now_ms(), &evicted);
    if (v != NULL) {
        s->hits++;
        return v;
    }
    if (evicted) {
        /* the lazy backstop just reclaimed this key */
        s->expirations++;
    }
    s->misses++;
    return NULL;
}

int store_del(store_t *s, const char *key) {
    /* eager timer cancel: the key is gone, its pending timer is pure
     * garbage and DEL is the rare, cold path — worth the sweep */
    tw_cancel(&s->wheel, key);
    int removed = htab_del(&s->table, key);
    if (removed) {
        s->deletes++;
    }
    return removed;
}

size_t store_tick(store_t *s) {
    return tw_tick(&s->wheel, now_ms(), wheel_fire, s);
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
