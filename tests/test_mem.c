#include "test.h"

#include <stdint.h>

#include "mem.h"

static void test_pool_recycles_blocks(void) {
    CHECK(mem_init(NULL, 4) == 0);

    void *p = mem_alloc(10);
    CHECK(p != NULL);
    CHECK(((uintptr_t)p & 15u) == 0);
    memset(p, 0xAB, 10);

    mem_free(p);
    void *q = mem_alloc(10);
    CHECK(q == p); /* recycled from the same pool, LIFO */

    mem_free(q);
    mem_release();
}

static void test_size_classes_route_correctly(void) {
    CHECK(mem_init(NULL, 8) == 0);

    void *a = mem_alloc(64);
    void *b = mem_alloc(200);
    void *c = mem_alloc(4096);
    CHECK(a != NULL && b != NULL && c != NULL);
    CHECK(a != b && b != c);

    mem_class_stat_t st[MEM_CLASSES];
    mem_totals_t tot;
    mem_stats(st, &tot);
    CHECK(st[0].used == 1);
    CHECK(st[1].used == 1);
    CHECK(st[2].used == 1);
    CHECK(tot.pooled == 3);
    CHECK(tot.heap == 0);

    mem_free(a);
    mem_free(b);
    mem_free(c);
    mem_release();
}

static void test_exhausted_pool_falls_back_to_heap(void) {
    size_t sizes[MEM_CLASSES] = {64, 256, 4096};
    CHECK(mem_init(sizes, 1) == 0); /* a single block per class */

    void *a = mem_alloc(64);
    /* pool 64 B is empty; the allocator cascades into the 256 B pool
     * before ever touching the heap, but the miss is still counted */
    void *b = mem_alloc(64);
    CHECK(a != NULL && b != NULL && a != b);

    mem_class_stat_t st[MEM_CLASSES];
    mem_totals_t tot;
    mem_stats(st, &tot);
    CHECK(st[0].fallbacks == 1);
    CHECK(st[1].used == 1);
    CHECK(tot.pooled == 2);
    CHECK(tot.heap == 0);

    /* a request above every class size lands on the heap */
    void *big = mem_alloc(9000);
    CHECK(big != NULL);
    mem_stats(st, &tot);
    CHECK(tot.heap == 1);

    mem_free(a);
    mem_free(b);
    mem_free(big);
    mem_stats(st, &tot);
    CHECK(tot.heap == 0);
    CHECK(tot.pooled == 0);

    mem_release();
}

/* mem_free() clears the magic word, so a double free aborts instead of
 * silently corrupting the pool. That path is demonstrated manually in
 * docs/DEBUGGING.md — it cannot live in the normal suite because it
 * terminates the process. */

int main(void) {
    RUN(test_pool_recycles_blocks);
    RUN(test_size_classes_route_correctly);
    RUN(test_exhausted_pool_falls_back_to_heap);
    TEST_REPORT();
}
