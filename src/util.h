#ifndef MORTAR_UTIL_H
#define MORTAR_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Monotonic milliseconds. TTL arithmetic and benchmarks both use the
 * monotonic clock so a wall-clock jump cannot expire or resurrect keys. */
uint64_t now_ms(void);

/* Allocation helpers that treat OOM as fatal. A data-structure library
 * that suddenly returns NULL from every third call is a footgun; the
 * callers below (hash table, store) are not written to thread error
 * handling through every path, so allocation failure aborts loudly. */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

#endif
