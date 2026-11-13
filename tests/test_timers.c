#include "test.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "timers.h"

/* recording callback: log fired (key, expires_at) pairs */
#define LOG_MAX 64
static char fired_keys[LOG_MAX][32];
static uint64_t fired_at[LOG_MAX];
static size_t fired_n;

static void record_fire(const char *key, uint64_t expires_at, void *ud) {
    (void)ud;
    if (fired_n < LOG_MAX) {
        snprintf(fired_keys[fired_n], sizeof(fired_keys[0]), "%s", key);
        fired_at[fired_n] = expires_at;
    }
    fired_n++;
}

static void reset_log(void) {
    fired_n = 0;
    memset(fired_keys, 0, sizeof(fired_keys));
    memset(fired_at, 0, sizeof(fired_at));
}

static void test_fires_in_deadline_order(void) {
    timer_wheel_t w;
    reset_log();
    CHECK(tw_init(&w) == 0);

    /* schedule out of deadline order on purpose */
    CHECK(tw_add(&w, "late", 5000) == 0);
    CHECK(tw_add(&w, "soon", 300) == 0);
    CHECK(tw_add(&w, "mid", 1200) == 0);
    CHECK(tw_pending(&w) == 3);

    /* the wheel fires with TW_STEP (234 ms) resolution: the 5000 ms
     * deadline lands in round 22, which completes at 5148 ms */
    CHECK(tw_tick(&w, 5200, record_fire, NULL) == 3);
    CHECK(fired_n == 3);
    CHECK_STR_EQ(fired_keys[0], "soon");
    CHECK_STR_EQ(fired_keys[1], "mid");
    CHECK_STR_EQ(fired_keys[2], "late");
    CHECK(tw_pending(&w) == 0);

    tw_release(&w);
}

static void test_stale_timer_fires_but_reports_old_deadline(void) {
    /* the wheel does not chase overwrites: the old timer still fires,
     * but the consumer sees the ORIGINAL expires_at and can drop it */
    timer_wheel_t w;
    reset_log();
    CHECK(tw_init(&w) == 0);

    CHECK(tw_add(&w, "k", 1000) == 0);
    /* key rescheduled with a much later deadline; old timer stays */
    CHECK(tw_add(&w, "k", 90000) == 0);
    CHECK(tw_pending(&w) == 2);

    /* stale timer fires when its round completes at 1170 ms */
    CHECK(tw_tick(&w, 1200, record_fire, NULL) == 1);
    CHECK(fired_n == 1);
    CHECK_STR_EQ(fired_keys[0], "k");
    CHECK(fired_at[0] == 1000); /* stale deadline exposed to consumer */

    tw_release(&w);
}

static void test_cancel_drops_pending_timers(void) {
    timer_wheel_t w;
    reset_log();
    CHECK(tw_init(&w) == 0);

    CHECK(tw_add(&w, "gone", 500) == 0);
    tw_cancel(&w, "gone");
    CHECK(tw_pending(&w) == 0);

    CHECK(tw_tick(&w, 600, record_fire, NULL) == 0);
    CHECK(fired_n == 0);

    tw_release(&w);
}

static void test_fast_forward_many_rounds(void) {
    timer_wheel_t w;
    reset_log();
    CHECK(tw_init(&w) == 0);

    /* a TTL longer than a full wheel turn (60 s): it must wait in its
     * bucket across turns and fire when the round arrives */
    CHECK(tw_add(&w, "long", 3 * TW_SPAN + 700) == 0);
    CHECK(tw_tick(&w, TW_SPAN, record_fire, NULL) == 0);
    CHECK(tw_tick(&w, 2 * TW_SPAN, record_fire, NULL) == 0);
    CHECK(fired_n == 0);
    CHECK(tw_tick(&w, 3 * TW_SPAN + 700, record_fire, NULL) == 1);
    CHECK_STR_EQ(fired_keys[0], "long");

    tw_release(&w);
}

static void test_release_frees_everything(void) {
    timer_wheel_t w;
    CHECK(tw_init(&w) == 0);
    for (int i = 0; i < 100; i++) {
        char key[16];
        snprintf(key, sizeof(key), "t-%d", i);
        CHECK(tw_add(&w, key, 1000u + (uint64_t)i * 10u) == 0);
    }
    CHECK(tw_pending(&w) == 100);
    tw_release(&w); /* leak-checked by the ASan suite */
    CHECK(tw_pending(&w) == 0);
}

int main(void) {
    RUN(test_fires_in_deadline_order);
    RUN(test_stale_timer_fires_but_reports_old_deadline);
    RUN(test_cancel_drops_pending_timers);
    RUN(test_fast_forward_many_rounds);
    RUN(test_release_frees_everything);
    TEST_REPORT();
}
