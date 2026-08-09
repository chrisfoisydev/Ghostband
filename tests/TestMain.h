// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#pragma once

// A ~60-line test harness.
//
// Why not Catch2/GoogleTest: `ghostband_core` has zero dependencies by design, and adding a
// test framework would mean a network fetch at configure time plus another entry in
// THIRD_PARTY_NOTICES.md. What we actually need is "assert, count, report, exit non-zero",
// which is small enough to own. If test needs outgrow this, revisit — but not before.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ghostband::test {

inline int g_checks = 0;
inline int g_failures = 0;
inline int g_test_start_failures = 0;
inline const char* g_current_test = "";

inline void beginTest(const char* name) {
    g_current_test = name;
    g_test_start_failures = g_failures;
    std::printf("  %-58s", name);
    std::fflush(stdout);
}

inline void endTest() {
    // Compare against the count at test entry, not zero — otherwise every test after
    // the first failure would be reported as failing too.
    std::printf("%s\n", g_failures == g_test_start_failures ? "ok" : "FAIL");
}

inline void reportFailure(const char* file, int line, const std::string& what) {
    ++g_failures;
    std::printf("\n    FAIL %s:%d\n      in [%s]\n      %s\n",
                file, line, g_current_test, what.c_str());
}

inline bool nearlyEqual(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

} // namespace ghostband::test

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++::ghostband::test::g_checks;                                                    \
        if (!(cond)) {                                                                 \
            ::ghostband::test::reportFailure(__FILE__, __LINE__,                          \
                std::string("expected: ") + #cond);                                    \
        }                                                                              \
    } while (0)

#define CHECK_EQ(actual, expected)                                                     \
    do {                                                                               \
        ++::ghostband::test::g_checks;                                                    \
        const auto a_ = (actual);                                                      \
        const auto e_ = (expected);                                                    \
        if (!(a_ == e_)) {                                                             \
            ::ghostband::test::reportFailure(__FILE__, __LINE__,                          \
                std::string(#actual) + " == " + #expected                              \
                + "\n      actual:   " + std::to_string(a_)                            \
                + "\n      expected: " + std::to_string(e_));                          \
        }                                                                              \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                                              \
    do {                                                                               \
        ++::ghostband::test::g_checks;                                                    \
        const double a_ = static_cast<double>(actual);                                 \
        const double e_ = static_cast<double>(expected);                               \
        if (!::ghostband::test::nearlyEqual(a_, e_, static_cast<double>(tol))) {           \
            ::ghostband::test::reportFailure(__FILE__, __LINE__,                          \
                std::string(#actual) + " ~= " + #expected                              \
                + "\n      actual:   " + std::to_string(a_)                            \
                + "\n      expected: " + std::to_string(e_)                            \
                + " (tolerance " + std::to_string(static_cast<double>(tol)) + ")");     \
        }                                                                              \
    } while (0)

#define TEST(name)                                                                     \
    ::ghostband::test::beginTest(name);                                                   \
    for (int once_ = 0; once_ < 1; ++once_, ::ghostband::test::endTest())

#define TEST_MAIN_BEGIN(suite)                                                         \
    int main() {                                                                       \
        std::printf("\n[%s]\n", suite);

#define TEST_MAIN_END()                                                                \
        std::printf("\n  %d checks, %d failures\n\n",                                  \
                    ::ghostband::test::g_checks, ::ghostband::test::g_failures);             \
        return ::ghostband::test::g_failures == 0 ? 0 : 1;                                \
    }
