// A dependency-free micro test harness. Register cases with CW_TEST, assert
// with CHECK / CHECK_NEAR, run them all from a single main (test_main.cpp).
#ifndef CINDERWAVE_TEST_CHECK_H
#define CINDERWAVE_TEST_CHECK_H

#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include <functional>

namespace cwtest {

struct Case { std::string name; std::function<void(int&)> fn; };

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, std::function<void(int&)> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline int runAll() {
    int failures = 0;
    for (auto& c : registry()) {
        int before = failures;
        c.fn(failures);
        std::printf("[%s] %s\n", failures == before ? "PASS" : "FAIL", c.name.c_str());
    }
    std::printf("\n%zu cases, %d failed assertion(s)\n", registry().size(), failures);
    return failures == 0 ? 0 : 1;
}

} // namespace cwtest

#define CW_TEST(name)                                                          \
    static void name(int& cw_failures_);                                       \
    static ::cwtest::Registrar cw_reg_##name(#name, name);                     \
    static void name(int& cw_failures_)

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++cw_failures_;                                                     \
            std::printf("    CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                  \
    do {                                                                       \
        double cw_d_ = std::fabs((double)(a) - (double)(b));                   \
        if (cw_d_ > (double)(tol)) {                                           \
            ++cw_failures_;                                                     \
            std::printf("    CHECK_NEAR failed: |%g - %g| = %g > %g (%s:%d)\n", \
                        (double)(a), (double)(b), cw_d_, (double)(tol),        \
                        __FILE__, __LINE__);                                   \
        }                                                                      \
    } while (0)

#endif // CINDERWAVE_TEST_CHECK_H
