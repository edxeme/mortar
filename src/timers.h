#ifndef MORTAR_TIMERS_H
#define MORTAR_TIMERS_H

#include <stddef.h>
#include <stdint.h>

/* A single-level timer wheel for active TTL expiry.
 *
 * TW_SLOTS buckets, each covering TW_STEP ms; a full turn spans
 * TW_SPAN. Timers carry the absolute round they fire in, so a bucket
 * may hold timers from several future rounds and long TTLs simply wait
 * across multiple turns — no cascade levels needed at mortar's
 * granularity.
 *
 * Cancellation is lazy: overwriting a key does not chase its timer
 * through the buckets. The timer fires later and the *consumer*
 * compares the fired expires_at against the entry's current one; a
 * mismatch means the timer is stale and is dropped. tw_cancel exists
 * for callers that want the memory back eagerly (e.g. DEL).
 *
 * The wheel COPIES every key it is given: hash-table rehashes move
 * interned key memory, so borrowed pointers would dangle.
 *
 * tw_tick advances the wheel toward now_ms, firing due timers in
 * deadline order. A timer whose deadline passed while its slot was
 * being skipped fires up to one turn late — acceptable because the
 * wheel is an aggressive-reclamation layer; lazy eviction on GET
 * remains the correctness backstop. */

#define TW_SLOTS 256u
#define TW_SPAN 60000u /* ms covered by one full turn */
#define TW_STEP (TW_SPAN / TW_SLOTS) /* 234 ms per round */

typedef struct timer {
    struct timer *next;
    char *key;           /* private copy, freed with the timer */
    uint64_t expires_at; /* absolute deadline, authoritative */
    uint64_t round;      /* wheel round this timer fires in */
    uint8_t cancelled;
} timer_t;

typedef struct {
    timer_t *slots[TW_SLOTS];
    uint64_t round; /* last completed round */
    size_t pending; /* live (non-cancelled) timers */
} timer_wheel_t;

/* fired(key, expires_at, userdata): expires_at is the deadline the
 * timer was scheduled with, so consumers can detect staleness. */
typedef void (*tw_fire_fn)(const char *key, uint64_t expires_at,
                           void *userdata);

int tw_init(timer_wheel_t *w);
void tw_release(timer_wheel_t *w);

int tw_add(timer_wheel_t *w, const char *key, uint64_t expires_at);

/* marks every timer for `key` cancelled; they are freed as their
 * buckets are drained */
void tw_cancel(timer_wheel_t *w, const char *key);

/* advances the wheel round by round until it reaches now_ms, firing
 * every due timer through fire(). Returns the number fired. */
size_t tw_tick(timer_wheel_t *w, uint64_t now_ms, tw_fire_fn fire,
               void *userdata);

size_t tw_pending(const timer_wheel_t *w);

#endif
