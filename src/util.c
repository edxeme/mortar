#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (p == NULL && n != 0) {
        fprintf(stderr, "fatal: out of memory (%zu bytes)\n", n);
        abort();
    }
    return p;
}

void *xcalloc(size_t n, size_t size) {
    void *p = calloc(n, size);
    if (p == NULL && n != 0 && size != 0) {
        fprintf(stderr, "fatal: out of memory (%zu x %zu bytes)\n", n, size);
        abort();
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (q == NULL && n != 0) {
        fprintf(stderr, "fatal: out of memory (%zu bytes)\n", n);
        abort();
    }
    return q;
}

char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1u;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}
