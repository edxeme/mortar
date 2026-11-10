#ifndef MORTAR_ARENA_H
#define MORTAR_ARENA_H

#include <stddef.h>

/* A bump arena chained over fixed-size blocks.
 *
 * Individual allocations can never be freed: memory is reclaimed one of
 * two ways — arena_release() drops everything, or the owner swaps the
 * whole arena for a fresh one (mortar's hash table does exactly that
 * whenever it rehashes, so interned keys are compacted generationaly).
 *
 * All allocations are 16-byte aligned. New bytes are NOT zeroed. */

typedef struct arena_block {
    struct arena_block *next;
    size_t capacity; /* usable bytes in data[] */
    size_t used;
    _Alignas(16) unsigned char data[];
} arena_block_t;

typedef struct arena {
    arena_block_t *head; /* newest block; allocations bump from here */
    size_t block_size;   /* default capacity of freshly carved blocks */
    size_t blocks;
    size_t bytes; /* total capacity across all blocks */
} arena_t;

int arena_init(arena_t *a, size_t block_size);
void *arena_alloc(arena_t *a, size_t size);
char *arena_strdup(arena_t *a, const char *s);
size_t arena_blocks(const arena_t *a);
size_t arena_bytes(const arena_t *a);
void arena_release(arena_t *a);

#endif
