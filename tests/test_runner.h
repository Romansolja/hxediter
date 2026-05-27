#pragma once

// Bare-bones test harness — no framework dependency, no fixtures, no discovery.
// Each test binary is one TU with its own main() that calls test functions and
// returns tests::run_all(). Failures print "FAIL file:line: expression".
// Per-binary granularity is enough for ctest to surface which group failed.

#include <cstdio>

namespace tests {

inline int g_failures   = 0;
inline int g_assertions = 0;

inline int run_all() {
    if (g_failures == 0) {
        std::printf("OK: %d assertions passed\n", g_assertions);
        return 0;
    }
    std::printf("FAIL: %d/%d assertions failed\n", g_failures, g_assertions);
    return 1;
}

} // namespace tests

#define HX_TEST_ASSERT(expr) do {                                            \
    ++tests::g_assertions;                                                   \
    if (!(expr)) {                                                           \
        std::fprintf(stderr, "  FAIL %s:%d: %s\n",                           \
                     __FILE__, __LINE__, #expr);                             \
        ++tests::g_failures;                                                 \
    }                                                                        \
} while (0)

#define HX_TEST_ASSERT_EQ(a, b) do {                                         \
    ++tests::g_assertions;                                                   \
    auto _hx_a = (a); auto _hx_b = (b);                                      \
    if (!(_hx_a == _hx_b)) {                                                 \
        std::fprintf(stderr, "  FAIL %s:%d: %s == %s\n",                     \
                     __FILE__, __LINE__, #a, #b);                            \
        ++tests::g_failures;                                                 \
    }                                                                        \
} while (0)
