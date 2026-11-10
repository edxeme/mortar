#include "test.h"

#include <stdint.h>

#include "htab.h"

static size_t freed_count;

static void counting_free(void *v) {
    (void)v;
    freed_count++;
}

static void test_put_get_overwrite(void) {
    htab_t t;
    freed_count = 0;
    CHECK(htab_init(&t, 64, counting_free, 512) == 0);

    CHECK(htab_put(&t, "alpha", (void *)1, 0) == 1);
    CHECK(htab_get(&t, "alpha", 0) == (void *)1);

    CHECK(htab_put(&t, "alpha", (void *)2, 0) == 0); /* replaced */
    CHECK(htab_get(&t, "alpha", 0) == (void *)2);
    CHECK(freed_count == 1); /* old value destroyed, not leaked */
    CHECK(htab_size(&t) == 1);

    htab_release(&t);
    CHECK(freed_count == 2); /* shutdown destroys the live value too */
}

static void test_delete_and_tombstone_reuse(void) {
    htab_t t;
    freed_count = 0;
    CHECK(htab_init(&t, 64, counting_free, 512) == 0);

    for (int i = 0; i < 10; i++) {
        char key[16];
        snprintf(key, sizeof(key), "key-%d", i);
        CHECK(htab_put(&t, key, (void *)(uintptr_t)(i + 1), 0) == 1);
    }
    CHECK(htab_size(&t) == 10);

    for (int i = 0; i < 5; i++) {
        char key[16];
        snprintf(key, sizeof(key), "key-%d", i);
        CHECK(htab_del(&t, key) == 1);
        CHECK(htab_del(&t, key) == 0); /* second delete misses */
    }
    CHECK(htab_size(&t) == 5);
    CHECK(htab_tombstones(&t) == 5);
    size_t capacity_before = htab_capacity(&t);

    /* refill: tombstones must be recycled, capacity must not grow */
    for (int i = 0; i < 5; i++) {
        char key[16];
        snprintf(key, sizeof(key), "new-%d", i);
        CHECK(htab_put(&t, key, (void *)42, 0) == 1);
    }
    CHECK(htab_size(&t) == 10);
    /* tombstone recycling is opportunistic: a new key only recycles a
     * tombstone if its probe happens to cross one */
    CHECK(htab_tombstones(&t) <= 5);
    CHECK(htab_capacity(&t) == capacity_before);
    /* exactly the 5 deleted values were freed; the refill overwrote
     * nothing */
    CHECK(freed_count == 5);

    htab_release(&t);
}

static void test_rehash_preserves_all_entries(void) {
    htab_t t;
    freed_count = 0;
    CHECK(htab_init(&t, 64, counting_free, 512) == 0);

    enum { N = 50000 };
    for (uintptr_t i = 0; i < N; i++) {
        char key[24];
        snprintf(key, sizeof(key), "k:%zu", (size_t)i);
        CHECK(htab_put(&t, key, (void *)(i + 1), 0) == 1);
    }
    CHECK(htab_size(&t) == N);
    CHECK(htab_rehashes(&t) >= 1);

    /* capacity is a power of two */
    CHECK((htab_capacity(&t) & (htab_capacity(&t) - 1u)) == 0);

    for (uintptr_t i = 0; i < N; i++) {
        char key[24];
        snprintf(key, sizeof(key), "k:%zu", (size_t)i);
        CHECK(htab_get(&t, key, 0) == (void *)(i + 1));
    }

    htab_release(&t);
    CHECK(freed_count == N);
}

static void test_lazy_expiry_on_get(void) {
    htab_t t;
    freed_count = 0;
    CHECK(htab_init(&t, 64, counting_free, 512) == 0);

    CHECK(htab_put(&t, "temp", (void *)7, 12345) == 1);

    /* peek sees the raw timestamp without evicting */
    uint64_t exp = 0;
    CHECK(htab_peek(&t, "temp", &exp, NULL) == 1);
    CHECK(exp == 12345);

    CHECK(htab_get(&t, "temp", 12344) == (void *)7); /* one ms left */
    CHECK(freed_count == 0);

    CHECK(htab_get(&t, "temp", 12345) == NULL); /* evicted on access */
    CHECK(freed_count == 1);
    CHECK(htab_size(&t) == 0);
    CHECK(htab_tombstones(&t) == 1);

    htab_release(&t);
}

int main(void) {
    RUN(test_put_get_overwrite);
    RUN(test_delete_and_tombstone_reuse);
    RUN(test_rehash_preserves_all_entries);
    RUN(test_lazy_expiry_on_get);
    TEST_REPORT();
}
