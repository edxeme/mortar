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

static void test_ttl_expiry_lazy_backstop(void) {
    CHECK(mem_init(NULL, 1024) == 0);

    store_t s;
    CHECK(store_init(&s, NULL) == 0);

    CHECK(store_set(&s, "session", "abc123", 40) == 0); /* 40 ms */
    CHECK_STR_EQ(store_get(&s, "session"), "abc123");

    int64_t ttl = store_ttl(&s, "session");
    CHECK(ttl > 0 && ttl <= 40);

    msleep(70);
    /* no tick yet: lazy eviction on read is still the backstop */
    CHECK(store_get(&s, "session") == NULL);
    CHECK(store_ttl(&s, "session") == -2);
    CHECK(s.expirations == 1); /* counted as an expiration, not a miss */

    store_release(&s);
    mem_release();
}

static void test_active_expiry_without_reads(void) {
    CHECK(mem_init(NULL, 1024) == 0);

    store_t s;
    CHECK(store_init(&s, NULL) == 0);

    CHECK(store_set(&s, "ephemeral", "value", 30) == 0); /* 30 ms */
    CHECK_STR_EQ(store_get(&s, "ephemeral"), "value");

    /* sleep past one full wheel round so the tick deterministically
     * reaches the timer's bucket regardless of clock phase */
    msleep(300);
    /* nobody reads the key: the wheel must reclaim it */
    size_t evicted = store_tick(&s);
    CHECK(evicted == 1);
    CHECK(s.expirations == 1);
    CHECK(store_get(&s, "ephemeral") == NULL);

    mem_class_stat_t cls[MEM_CLASSES];
    mem_totals_t tot;
    mem_stats(cls, &tot);
    CHECK(tot.pooled == 0); /* the slab block was actually released */

    store_release(&s);
    mem_release();
}

static void test_overwrite_without_ttl_cancels_old_timer(void) {
    CHECK(mem_init(NULL, 1024) == 0);

    store_t s;
    CHECK(store_init(&s, NULL) == 0);

    CHECK(store_set(&s, "k", "with-ttl", 30) == 0);
    CHECK(store_set(&s, "k", "forever", 0) == 0); /* TTL revoked */

    msleep(300);
    store_tick(&s);
    CHECK_STR_EQ(store_get(&s, "k"), "forever"); /* survived the wheel */
    CHECK(s.expirations == 0);

    store_release(&s);
    mem_release();
}

static void test_overwrite_with_new_ttl_suppresses_stale_fire(void) {
    CHECK(mem_init(NULL, 1024) == 0);

    store_t s;
    CHECK(store_init(&s, NULL) == 0);

    CHECK(store_set(&s, "k", "short", 30) == 0);
    CHECK(store_set(&s, "k", "longer", 60000) == 0); /* new deadline */

    msleep(300);
    /* the 30 ms timer fires but must be discarded as stale: the entry's
     * current deadline no longer matches */
    store_tick(&s);
    CHECK(s.expirations == 0);
    CHECK_STR_EQ(store_get(&s, "k"), "longer");

    store_release(&s);
    mem_release();
}

static void test_del_cancels_timer_eagerly(void) {
    CHECK(mem_init(NULL, 1024) == 0);

    store_t s;
    CHECK(store_init(&s, NULL) == 0);

    CHECK(store_set(&s, "k", "v", 20) == 0);
    CHECK(store_del(&s, "k") == 1);

    msleep(300);
    store_tick(&s); /* timer was cancelled at DEL: nothing to fire */
    CHECK(s.expirations == 0);

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
    CHECK(s.expirations == 0);

    store_release(&s);
    mem_release();
}

int main(void) {
    RUN(test_set_get_del_roundtrip);
    RUN(test_overwrite_frees_old_value);
    RUN(test_ttl_expiry_lazy_backstop);
    RUN(test_active_expiry_without_reads);
    RUN(test_overwrite_without_ttl_cancels_old_timer);
    RUN(test_overwrite_with_new_ttl_suppresses_stale_fire);
    RUN(test_del_cancels_timer_eagerly);
    RUN(test_ttl_conventions);
    RUN(test_counters);
    TEST_REPORT();
}
