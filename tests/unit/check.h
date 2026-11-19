/* Minimal test assertions for host unit tests. Each test program returns
 * non-zero if any check failed; tests/unit/test_unit.py runs them. */
#ifndef UD_TEST_CHECK_H
#define UD_TEST_CHECK_H

#include <stdio.h>
#include <string.h>

static int check_failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            check_failures++;                                                                      \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        long long check_a_ = (long long)(a), check_b_ = (long long)(b);                            \
        if (check_a_ != check_b_) {                                                                \
            fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s (%lld != %lld)\n", __FILE__,         \
                    __LINE__, #a, #b, check_a_, check_b_);                                         \
            check_failures++;                                                                      \
        }                                                                                          \
    } while (0)

#define CHECK_STR(a, b)                                                                            \
    do {                                                                                           \
        const char *check_a_ = (a), *check_b_ = (b);                                               \
        if (strcmp(check_a_, check_b_) != 0) {                                                     \
            fprintf(stderr, "%s:%d: CHECK_STR failed: \"%s\" != \"%s\"\n", __FILE__, __LINE__,     \
                    check_a_, check_b_);                                                           \
            check_failures++;                                                                      \
        }                                                                                          \
    } while (0)

#define CHECK_DONE() (check_failures ? (fprintf(stderr, "%d failure(s)\n", check_failures), 1) : 0)

#endif
