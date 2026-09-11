#pragma once

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*onion_test_fn_t)(void);

int onion_test_run(const char *name, onion_test_fn_t fn);
int onion_test_fail(const char *file, int line, const char *fmt, ...);

#define TEST_ASSERT_TRUE(expr)                                                \
  do {                                                                        \
    if (!(expr)) {                                                            \
      return onion_test_fail(__FILE__, __LINE__,                              \
                             "assertion failed: %s", #expr);                  \
    }                                                                         \
  } while (0)

#define TEST_ASSERT_EQ_INT(expected, actual)                                  \
  do {                                                                        \
    int expected_value__ = (expected);                                        \
    int actual_value__ = (actual);                                            \
    if (expected_value__ != actual_value__) {                                 \
      return onion_test_fail(__FILE__, __LINE__,                              \
                             "expected %d, got %d", expected_value__,         \
                             actual_value__);                                 \
    }                                                                         \
  } while (0)

#define TEST_ASSERT_EQ_U64(expected, actual)                                  \
  do {                                                                        \
    unsigned long long expected_value__ =                                     \
        (unsigned long long)(expected);                                       \
    unsigned long long actual_value__ =                                       \
        (unsigned long long)(actual);                                         \
    if (expected_value__ != actual_value__) {                                 \
      return onion_test_fail(__FILE__, __LINE__,                              \
                             "expected %llu, got %llu", expected_value__,     \
                             actual_value__);                                 \
    }                                                                         \
  } while (0)

/* NULL-safe: NULL matches NULL, and NULL vs a non-NULL string is a clean
 * failure rather than a strcmp() crash that takes down the whole binary. */
#define TEST_ASSERT_STREQ(expected, actual)                                   \
  do {                                                                        \
    const char *expected_value__ = (expected);                                \
    const char *actual_value__ = (actual);                                    \
    const int equal__ =                                                       \
        (expected_value__ == NULL)                                            \
            ? (actual_value__ == NULL)                                        \
            : (actual_value__ != NULL &&                                      \
               strcmp(expected_value__, actual_value__) == 0);                \
    if (!equal__) {                                                           \
      return onion_test_fail(__FILE__, __LINE__,                              \
                             "expected \"%s\", got \"%s\"",                   \
                             expected_value__ ? expected_value__ : "(null)",   \
                             actual_value__ ? actual_value__ : "(null)");     \
    }                                                                         \
  } while (0)

#define TEST_ASSERT_MEMEQ(expected, actual, len)                              \
  do {                                                                        \
    if (memcmp((expected), (actual), (len)) != 0) {                           \
      return onion_test_fail(__FILE__, __LINE__,                              \
                             "memory mismatch for %zu bytes",                 \
                             (size_t)(len));                                  \
    }                                                                         \
  } while (0)

#ifdef __cplusplus
}
#endif
