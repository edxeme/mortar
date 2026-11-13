#include "timers.h"

#include "util.h"

#include <stdlib.h>
#include <string.h>

static void timer_free(tw_timer_t *t) {
    free(t->key);
    free(t);
}

int tw_init(timer_wheel_t *w) {
    memset(w, 0, sizeof(*w));
    return 0;
}

void tw_release(timer_wheel_t *w) {
    for (size_t i = 0; i < TW_SLOTS; i++) {
        tw_timer_t *t = w->slots[i];
        while (t != NULL) {
            tw_timer_t *next = t->next;
            timer_free(t);
            t = next;
        }
    }
    memset(w, 0, sizeof(*w));
}

int tw_add(timer_wheel_t *w, const char *key, uint64_t expires_at) {
    tw_timer_t *t = xmalloc(sizeof *t);
    t->key = xstrdup(key);
    t->expires_at = expires_at;
    t->round = expires_at / TW_STEP + 1u; /* strictly in the wheel's future */
    t->cancelled = 0;

    size_t slot = t->round % TW_SLOTS;
    t->next = w->slots[slot]; /* newest first: O(1) insert, scan on drain */
    w->slots[slot] = t;
    w->pending++;
    return 0;
}

void tw_cancel(timer_wheel_t *w, const char *key) {
    for (size_t i = 0; i < TW_SLOTS; i++) {
        for (tw_timer_t *t = w->slots[i]; t != NULL; t = t->next) {
            if (!t->cancelled && strcmp(t->key, key) == 0) {
                t->cancelled = 1;
                w->pending--;
            }
        }
    }
}

size_t tw_tick(timer_wheel_t *w, uint64_t now_ms, tw_fire_fn fire,
               void *userdata) {
    size_t fired = 0;

    /* one round at a time so timers fire in deadline order */
    while ((w->round + 1u) * TW_STEP <= now_ms) {
        w->round++;
        tw_timer_t **pp = &w->slots[w->round % TW_SLOTS];

        while (*pp != NULL) {
            tw_timer_t *t = *pp;
            if (t->cancelled) {
                *pp = t->next;
                timer_free(t);
                continue;
            }
            if (t->expires_at <= now_ms) {
                fire(t->key, t->expires_at, userdata);
                fired++;
                w->pending--;
                *pp = t->next;
                timer_free(t);
                continue;
            }
            /* deadline still ahead (wheel caught up early): leave it */
            pp = &t->next;
        }
    }
    return fired;
}

size_t tw_pending(const timer_wheel_t *w) {
    return w->pending;
}
