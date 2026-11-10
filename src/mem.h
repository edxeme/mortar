#ifndef MORTAR_MEM_H
#define MORTAR_MEM_H

#include <stddef.h>
#include <stdint.h>

/* Size-class pools for short-lived, churny allocations (values).
 *
 * Values are written and overwritten constantly, and most of them are
 * small. Carving them out of pre-allocated pools keeps that churn off
 * the general-purpose allocator. Every payload carries a 16-byte header
 * so mem_free() knows which pool owns the block without extra
 * bookkeeping at the call site. Requests larger than the biggest class,
 * or into an exhausted pool, fall back to malloc(3) and are counted so
 * callers can watch how well the pools are absorbing the workload. */

#define MEM_CLASSES 3

/* header + payload rounding keeps every payload 16-byte aligned */
#define MEM_HEADER 16u

typedef struct {
    size_t block_size; /* payload bytes served by this class */
    size_t capacity;   /* blocks carved at mem_init() */
    size_t used;       /* live blocks */
    size_t high_water;
    size_t fallbacks;  /* requests routed to malloc(3) instead */
} mem_class_stat_t;

typedef struct {
    size_t pooled; /* live blocks served from pools */
    size_t heap;   /* live malloc(3) fallback allocations */
} mem_totals_t;

/* Carve the pools. block_sizes may be NULL for the defaults
 * {64, 256, 4096}; blocks_per_class <= 0 selects 4096. Re-initing
 * requires every live allocation to be freed first. */
int mem_init(const size_t block_sizes[MEM_CLASSES], size_t blocks_per_class);
void mem_release(void);

void *mem_alloc(size_t size);
void mem_free(void *payload);

void mem_stats(mem_class_stat_t out[MEM_CLASSES], mem_totals_t *totals);

#endif
