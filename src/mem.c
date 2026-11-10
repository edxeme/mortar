#include "mem.h"

#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEM_MAGIC UINT32_C(0x4D4F5254) /* 'MORT' */

typedef struct header {
    uint32_t magic; /* cleared on free so a double free aborts loudly */
    uint8_t cls;    /* pool index, 0xFF = heap fallback */
    uint8_t pad[3];
    uint32_t size;
} header_t;

static struct pool {
    unsigned char *data;
    size_t block_size;
    size_t stride; /* MEM_HEADER + payload, 16-byte multiple */
    size_t capacity;
    size_t used;
    size_t high_water;
    size_t fallbacks;
    unsigned char *freelist; /* singly linked through free blocks */
} pools[MEM_CLASSES];

static size_t live_pooled;
static size_t live_heap;

static void freelist_push(struct pool *p, unsigned char *blk) {
    memcpy(blk, &p->freelist, sizeof(void *));
    p->freelist = blk;
}

static unsigned char *freelist_pop(struct pool *p) {
    unsigned char *blk = p->freelist;
    if (blk != NULL) {
        void *next;
        memcpy(&next, blk, sizeof(next));
        p->freelist = next;
    }
    return blk;
}

int mem_init(const size_t block_sizes[MEM_CLASSES], size_t blocks_per_class) {
    static const size_t defaults[MEM_CLASSES] = {64, 256, 4096};

    if (blocks_per_class == 0) {
        blocks_per_class = 4096;
    }

    mem_release();

    for (int i = 0; i < MEM_CLASSES; i++) {
        struct pool *p = &pools[i];
        size_t block = block_sizes != NULL ? block_sizes[i] : defaults[i];
        if (block == 0) {
            block = defaults[i];
        }
        size_t payload = (block + 15u) & ~(size_t)15u;
        p->block_size = block;
        p->stride = MEM_HEADER + payload;
        p->capacity = blocks_per_class;
        p->data = xmalloc(p->stride * blocks_per_class);
        memset(p->data, 0, p->stride * blocks_per_class);

        for (size_t j = 0; j < blocks_per_class; j++) {
            unsigned char *blk = p->data + j * p->stride;
            if (j + 1 < blocks_per_class) {
                unsigned char *next = p->data + (j + 1) * p->stride;
                memcpy(blk, &next, sizeof(next));
            } else {
                void *end = NULL;
                memcpy(blk, &end, sizeof(end));
            }
        }
        p->freelist = p->data;
        p->used = 0;
        p->high_water = 0;
        p->fallbacks = 0;
    }
    live_pooled = 0;
    live_heap = 0;
    return 0;
}

void *mem_alloc(size_t size) {
    for (int i = 0; i < MEM_CLASSES; i++) {
        struct pool *p = &pools[i];
        if (size > p->block_size) {
            continue;
        }
        unsigned char *blk = freelist_pop(p);
        if (blk == NULL) {
            p->fallbacks++; /* pool exhausted for this size class */
            continue;
        }
        header_t *h = (header_t *)blk;
        h->magic = MEM_MAGIC;
        h->cls = (uint8_t)i;
        h->size = (uint32_t)size;
        p->used++;
        live_pooled++;
        if (p->used > p->high_water) {
            p->high_water = p->used;
        }
        return blk + MEM_HEADER;
    }

    /* oversized or every fitting pool exhausted */
    unsigned char *blk = xmalloc(MEM_HEADER + size);
    header_t *h = (header_t *)blk;
    h->magic = MEM_MAGIC;
    h->cls = 0xFF;
    h->size = (uint32_t)size;
    live_heap++;
    return blk + MEM_HEADER;
}

void mem_free(void *payload) {
    if (payload == NULL) {
        return;
    }
    unsigned char *blk = (unsigned char *)payload - MEM_HEADER;
    header_t *h = (header_t *)blk;
    if (h->magic != MEM_MAGIC) {
        fprintf(stderr, "fatal: mem_free() on a foreign or corrupted block\n");
        abort();
    }
    h->magic = 0;

    if (h->cls == 0xFF) {
        free(blk);
        live_heap--;
        return;
    }
    struct pool *p = &pools[h->cls];
    freelist_push(p, blk);
    p->used--;
    live_pooled--;
}

void mem_stats(mem_class_stat_t out[MEM_CLASSES], mem_totals_t *totals) {
    for (int i = 0; i < MEM_CLASSES; i++) {
        out[i].block_size = pools[i].block_size;
        out[i].capacity = pools[i].capacity;
        out[i].used = pools[i].used;
        out[i].high_water = pools[i].high_water;
        out[i].fallbacks = pools[i].fallbacks;
    }
    totals->pooled = live_pooled;
    totals->heap = live_heap;
}

void mem_release(void) {
    for (int i = 0; i < MEM_CLASSES; i++) {
        free(pools[i].data);
    }
    memset(pools, 0, sizeof(pools));
    live_pooled = 0;
    live_heap = 0;
}
