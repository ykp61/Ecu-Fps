#include "core/product_info.h"

#include <string>
#include <string_view>

#include "core/games/protection_gate.h"
#include "ef_test.h"


EF_TEST(product_name_is_ecu_fps) {
    EF_CHECK(ef::core::product_name() == L"Ecu Fps");
    EF_CHECK(ef::core::executable_name() == L"EcuFps.exe");
}

EF_TEST(version_matches_cmake_project) {
    const auto v = ef::core::product_version();
    // CMake project(VERSION) degerleri ef_core'un PUBLIC tanimlariyla gelir (surum artisinda test elle degismez).
    EF_CHECK(v.major == EF_VERSION_MAJOR);
    EF_CHECK(v.minor == EF_VERSION_MINOR);
    EF_CHECK(v.patch == EF_VERSION_PATCH);
    EF_CHECK(v.major == 0 && v.minor >= 2);  // 0.2.0: imzali zorunlu guncelleme surumu; geriye gidis yakalanir
}

// EF-X18 dogrulama bulgusu 1: kimlik her derlemede uretilen basliktan gelir; bicim "unknown" veya
// 12 hex (+ istege bagli "-dirty"). -dirty/unknown iken gate'in collector kapsami OLUSMAZ.
EF_TEST(x18v_build_id_format_and_dirty_never_observed) {
    const std::string_view id = ef::core::build_id();
    bool ok = id == "unknown";
    if (!ok) {
        std::string_view head = id;
        const bool dirty = head.size() > 6 && head.substr(head.size() - 6) == "-dirty";
        if (dirty) head.remove_suffix(6);
        ok = head.size() == 12;
        for (const char ch : head)
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) ok = false;
        const std::string scope = ef::core::games::observed_collector_commit(id);
        EF_CHECK(dirty ? scope.empty() : scope == head);
    } else {
        EF_CHECK(ef::core::games::observed_collector_commit(id).empty());
    }
    EF_CHECK(ok);
}
