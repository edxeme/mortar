#include "test.h"

#include <stdint.h>

#include "arena.h"

static void test_alloc_is_16_byte_aligned(void) {
    arena_t a;
    CHECK(arena_init(&a, 1024) == 0);

    char *p = arena_alloc(&a, 1);
    CHECK(p != NULL);
    CHECK(((uintptr_t)p & 15u) == 0);

    char *q = arena_alloc(&a, 3);
    CHECK(q != NULL);
    CHECK(((uintptr_t)q & 15u) == 0);
    CHECK(q == p + 16); /* odd sizes still advance by aligned steps */

    arena_release(&a);
}

static void test_strdup_roundtrip(void) {
    arena_t a;
    CHECK(arena_init(&a, 256) == 0);

    char *s1 = arena_strdup(&a, "hola mundo");
    char *s2 = arena_strdup(&a, "hola mundo");
    char *s3 = arena_strdup(&a, "");
    CHECK(s1 != NULL && s2 != NULL && s3 != NULL);
    CHECK_STR_EQ(s1, "hola mundo");
    CHECK_STR_EQ(s3, "");
    CHECK(s1 != s2); /* no interning at this layer */

    arena_release(&a);
}

static void test_grows_across_blocks(void) {
    arena_t a;
    CHECK(arena_init(&a, 512) == 0);

    /* 33-byte strings round up to 48; ~64 of them need ~3 KiB */
    for (int i = 0; i < 64; i++) {
        char *s = arena_strdup(&a, "0123456789abcdef0123456789abcdefX");
        CHECK(s != NULL);
    }
    CHECK(arena_blocks(&a) >= 3);
    CHECK(arena_bytes(&a) >= 64u * 48u);

    arena_release(&a);
}

static void test_oversize_gets_dedicated_block(void) {
    arena_t a;
    CHECK(arena_init(&a, 256) == 0);

    char *big = arena_alloc(&a, 5000);
    CHECK(big != NULL);
    CHECK(((uintptr_t)big & 15u) == 0);
    CHECK(arena_blocks(&a) == 1);
    CHECK(arena_bytes(&a) >= 5000);

    arena_release(&a);
}

static void test_release_resets_counters(void) {
    arena_t a;
    CHECK(arena_init(&a, 256) == 0);
    CHECK(arena_strdup(&a, "x") != NULL);
    arena_release(&a);
    CHECK(arena_blocks(&a) == 0);
    CHECK(arena_bytes(&a) == 0);
}

int main(void) {
    RUN(test_alloc_is_16_byte_aligned);
    RUN(test_strdup_roundtrip);
    RUN(test_grows_across_blocks);
    RUN(test_oversize_gets_dedicated_block);
    RUN(test_release_resets_counters);
    TEST_REPORT();
}
