#ifndef MORTAR_TEST_H
#define MORTAR_TEST_H

/* A deliberately tiny test harness. Each test file is its own binary
 * with a main() that registers cases via RUN(). CHECK returns early on
 * failure so a broken precondition cannot cascade into noise. */

#include <stdio.h>
#include <string.h>

static int run_count;
static int fail_count;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "    %s:%d: FAIL %s\n", __FILE__, __LINE__,      \
                    #cond);                                                  \
            fail_count++;                                                    \
            return;                                                          \
        }                                                                    \
    } while (0)

#define CHECK_STR_EQ(a, b)                                                   \
    do {                                                                     \
        const char *_ta = (a);                                               \
        const char *_tb = (b);                                               \
        if (_ta == NULL || _tb == NULL || strcmp(_ta, _tb) != 0) {           \
            fprintf(stderr, "    %s:%d: FAIL \"%s\" != \"%s\"\n", __FILE__,  \
                    __LINE__, _ta ? _ta : "(null)", _tb ? _tb : "(null)");   \
            fail_count++;                                                    \
            return;                                                          \
        }                                                                    \
    } while (0)

#define RUN(fn)                                                              \
    do {                                                                     \
        run_count++;                                                         \
        printf("  -> %s\n", #fn);                                            \
        fn();                                                                \
    } while (0)

#define TEST_REPORT()                                                        \
    do {                                                                     \
        printf("  %d case(s), %d failure(s)\n", run_count, fail_count);      \
        return fail_count == 0 ? 0 : 1;                                      \
    } while (0)

#endif
