/* Micro-benchmark for mortar. Two phases over a 65536-key space:
 *
 *   1. SET   — `ops` writes with value sizes spread across the slab
 *              size classes; the default (4x the key space) fills
 *              nearly every slot before the read phase
 *   2. GET   — `ops` reads where every 7th lookup is a guaranteed
 *              miss (~14%)
 *
 * Throughput is reported per phase, then the allocator and table
 * diagnostics are printed so allocator behavior (high-water marks,
 * fallbacks, tombstone pressure) can be compared across runs. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mem.h"
#include "store.h"
#include "util.h"

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t rng_next(void) {
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static double seconds_since(struct timespec *t0) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (double)(t1.tv_sec - t0->tv_sec) +
           (double)(t1.tv_nsec - t0->tv_nsec) / 1e9;
}

static void fill_value(char *buf, size_t *len) {
    /* spread values over the size classes: mostly <64 B, some <256 B,
     * a tail into the 4 KiB class */
    uint64_t r = rng_next();
    size_t n = 8 + (size_t)(r % 100);
    if (r % 13 == 0) {
        n = 100 + (size_t)(r % 120);
    }
    if (r % 97 == 0) {
        n = 300 + (size_t)(r % 3000);
    }
    for (size_t i = 0; i < n; i++) {
        buf[i] = (char)('a' + (char)(rng_next() % 26));
    }
    buf[n] = '\0';
    *len = n;
}

int main(int argc, char **argv) {
    size_t ops = 1u << 18; /* 262144 = 4x the key space */
    if (argc > 1) {
        char *end = NULL;
        unsigned long long v = strtoull(argv[1], &end, 10);
        if (end != argv[1] && *end == '\0' && v > 0) {
            ops = (size_t)v;
        }
    }

    size_t sizes[MEM_CLASSES] = {64, 256, 4096};
    mem_init(sizes, 8192);

    store_t store;
    store_config_t cfg = {.initial_capacity = 1u << 12,
                          .key_arena_block = 8192};
    store_init(&store, &cfg);

    static char value[8192];
    char key[32];
    const size_t keyspace = 1u << 16; /* 65536 distinct keys */
    struct timespec t0;

    printf("mortar bench: %zu set ops, %zu get ops, keyspace %zu\n\n", ops,
           ops, keyspace);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (size_t i = 0; i < ops; i++) {
        snprintf(key, sizeof(key), "key:%llu",
                 (unsigned long long)(rng_next() % keyspace));
        size_t len;
        fill_value(value, &len);
        store_set(&store, key, value, 0);
    }
    double set_s = seconds_since(&t0);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint64_t hits = 0;
    for (size_t i = 0; i < ops; i++) {
        size_t k = (size_t)(rng_next() % keyspace);
        if (i % 7 == 0) {
            k += keyspace; /* guaranteed miss, ~14% */
        }
        snprintf(key, sizeof(key), "key:%zu", k);
        if (store_get(&store, key) != NULL) {
            hits++;
        }
    }
    double get_s = seconds_since(&t0);

    printf("SET  %8.0f ops/s  (%.3fs)\n", (double)ops / set_s, set_s);
    printf("GET  %8.0f ops/s  (%.3fs, %.1f%% hit)\n", (double)ops / get_s,
           get_s, 100.0 * (double)hits / (double)ops);

    printf("\n");
    mem_class_stat_t cls[MEM_CLASSES];
    mem_totals_t totals;
    mem_stats(cls, &totals);
    for (int i = 0; i < MEM_CLASSES; i++) {
        printf("slab %5zu B   %zu/%zu live, high-water %zu, fallbacks %zu\n",
               cls[i].block_size, cls[i].used, cls[i].capacity,
               cls[i].high_water, cls[i].fallbacks);
    }
    printf("key arena      %zu block(s), %zu bytes\n",
           htab_key_arena_blocks(&store.table),
           htab_key_arena_bytes(&store.table));
    printf("table          %zu entries, capacity %zu, tombstones %zu, "
           "rehashes %zu\n",
           htab_size(&store.table), htab_capacity(&store.table),
           htab_tombstones(&store.table), htab_rehashes(&store.table));

    store_release(&store);
    mem_release();
    return 0;
}
