#include "arena.h"

#include "util.h"

#include <stdlib.h>
#include <string.h>

#define ARENA_ALIGN 16u
#define ARENA_MIN_BLOCK 256u

static size_t align_up(size_t n) {
    return (n + (ARENA_ALIGN - 1u)) & ~(size_t)(ARENA_ALIGN - 1u);
}

int arena_init(arena_t *a, size_t block_size) {
    if (block_size < ARENA_MIN_BLOCK) {
        block_size = ARENA_MIN_BLOCK;
    }
    a->head = NULL;
    a->block_size = block_size;
    a->blocks = 0;
    a->bytes = 0;
    return 0;
}

static arena_block_t *block_new(size_t capacity) {
    arena_block_t *b = xmalloc(sizeof *b + capacity);
    b->next = NULL;
    b->capacity = capacity;
    b->used = 0;
    return b;
}

void *arena_alloc(arena_t *a, size_t size) {
    if (size == 0) {
        size = 1;
    }
    size = align_up(size);

    arena_block_t *b = a->head;
    if (b == NULL || b->capacity - b->used < size) {
        size_t cap = a->block_size;
        if (size > cap) {
            cap = size; /* oversize allocations get a dedicated block */
        }
        b = block_new(cap);
        b->next = a->head;
        a->head = b;
        a->blocks++;
        a->bytes += cap;
    }

    void *p = b->data + b->used;
    b->used += size;
    return p;
}

char *arena_strdup(arena_t *a, const char *s) {
    size_t n = strlen(s) + 1u;
    char *p = arena_alloc(a, n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

size_t arena_blocks(const arena_t *a) {
    return a->blocks;
}

size_t arena_bytes(const arena_t *a) {
    return a->bytes;
}

void arena_release(arena_t *a) {
    arena_block_t *b = a->head;
    while (b != NULL) {
        arena_block_t *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
    a->blocks = 0;
    a->bytes = 0;
}
