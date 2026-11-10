#include "test.h"

#include <string.h>
#include <time.h>

#include "mem.h"
#include "store.h"

static void msleep(int ms) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = (long)ms * 1000000L};
    nanosleep(&ts, NULL);
}

static void test_set_get_del_roundtrip(void) {
    size_t sizes[MEM_CLASSES] = {64, 256, 4096};
    CHECK(mem_init(sizes, 1024) == 0);

    store_t s;
    CHECK(store_init(&s, NULL) == 0);

    char scratch[8] = {0};
    CHECK(store_set(&s, "lang", "C11", 0) == 0);

    const char *v = store_get(&s, "lang");
    CHECK(v != NULL);
    CHECK_STR_EQ(v, "C11");

    /* the store copied the value: mutating our buffer must not leak in */
    memset(scratch, 'x', sizeof(scratch) - 1);
    CHECK_STR_EQ(store_get(&s, "lang"), "C11");

    CHECK(store_del(&s, "lang") == 1);
    CHECK(store_get(&s, "lang") == NULL);
    CHECK(store_del(&s, "lang") == 0);

    store_release(&s);
    mem_release();
}

static void test_overwrite_frees_old_value(void) {
    CHECK(mem_init(NULL, 1024) == 0);

    store_t s;
    CHECK(store_init(&s, NULL) == 0);

    CHECK(store_set(&s, "k", "first", 0) == 0);
    CHECK(store_set(&s, "k", "second", 0) == 0);
    CHECK_STR_EQ(store_get(&s, "k"), "second");

    mem_class_stat_t cls[MEM_CLASSES];
    mem_totals_t tot;
    mem_stats(cls, &tot);
    CHECK(tot.pooled == 1); /* exactly one live value */

    store_release(&s);
    mem_release();
}

static void test_ttl_expiry(void) {
    CHECK(mem_init(NULL, 1024) == 0);

    store_t s;
    CHECK(store_init(&s, NULL) == 0);

    CHECK(store_set(&s, "session", "abc123", 40) == 0); /* 40 ms */
    CHECK_STR_EQ(store_get(&s, "session"), "abc123");

    int64_t ttl = store_ttl(&s, "session");
    CHECK(ttl > 0 && ttl <= 40);

    msleep(70);
    CHECK(store_get(&s, "session") == NULL); /* lazily evicted */
    CHECK(store_ttl(&s, "session") == -2);

    store_release(&s);
    mem_release();
}

static void test_ttl_conventions(void) {
    CHECK(mem_init(NULL, 1024) == 0);

    store_t s;
    CHECK(store_init(&s, NULL) == 0);

    CHECK(store_ttl(&s, "nope") == -2);  /* missing */
    CHECK(store_set(&s, "k", "v", 0) == 0);
    CHECK(store_ttl(&s, "k") == -1);     /* no expiry */

    store_release(&s);
    mem_release();
}

static void test_counters(void) {
    CHECK(mem_init(NULL, 1024) == 0);

    store_t s;
    CHECK(store_init(&s, NULL) == 0);

    store_set(&s, "a", "1", 0);
    store_set(&s, "b", "2", 0);
    store_get(&s, "a");
    store_get(&s, "missing");
    store_del(&s, "b");

    CHECK(s.sets == 2);
    CHECK(s.gets == 2);
    CHECK(s.hits == 1);
    CHECK(s.misses == 1);
    CHECK(s.deletes == 1);

    store_release(&s);
    mem_release();
}

int main(void) {
    RUN(test_set_get_del_roundtrip);
    RUN(test_overwrite_frees_old_value);
    RUN(test_ttl_expiry);
    RUN(test_ttl_conventions);
    RUN(test_counters);
    TEST_REPORT();
}
