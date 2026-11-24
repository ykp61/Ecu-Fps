#pragma once
// Minimal yerel test cercevesi: ucuncu taraf bagimlilik ve lisans yuku yok.
// Kullanim:
//   EF_TEST(grup_ad) { EF_CHECK(a == b); }

#include <cstdio>
#include <exception>
#include <typeinfo>
#include <functional>
#include <string_view>
#include <vector>

namespace ef::test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void report_failure(const char* expr, const char* file, int line) {
    ++failures();
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, expr);
}

inline int run_all() {
    int failed_cases = 0;
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // asili testte son satir gorunsun
    for (const auto& c : registry()) {
        const int before = failures();
        std::printf("[ RUN] %s\n", c.name);
        try {
            c.fn();
        } catch (const std::exception& e) {
            ++failures();
            std::fprintf(stderr, "  FAIL uncaught exception: %s (%s)\n", e.what(), typeid(e).name());
        } catch (...) {
            ++failures();
            std::fprintf(stderr, "  FAIL uncaught non-std exception\n");
        }
        const bool ok = failures() == before;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
        if (!ok) ++failed_cases;
    }
    std::printf("%zu test, %d basarisiz\n", registry().size(), failed_cases);
    return failed_cases == 0 ? 0 : 1;
}

}  // namespace ef::test

#define EF_CONCAT_INNER(a, b) a##b
#define EF_CONCAT(a, b) EF_CONCAT_INNER(a, b)

#define EF_TEST(name)                                                              \
    static void EF_CONCAT(ef_test_fn_, name)();                                   \
    static ::ef::test::Registrar EF_CONCAT(ef_test_reg_, name){                 \
        #name, &EF_CONCAT(ef_test_fn_, name)};                                    \
    static void EF_CONCAT(ef_test_fn_, name)()

#define EF_CHECK(expr)                                                              \
    do {                                                                             \
        if (!(expr)) ::ef::test::report_failure(#expr, __FILE__, __LINE__);         \
    } while (0)
