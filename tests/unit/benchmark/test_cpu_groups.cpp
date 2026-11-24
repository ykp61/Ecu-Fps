// > 64 mantiksal CPU: isci -> islemci grubu dagitimi (saf hesap) ve sayim.
#include <array>
#include <cstdint>
#include <thread>
#include <vector>

#include "benchmark/cpu/cpu_groups.h"
#include "ef_test.h"

using namespace ef::benchmark;

EF_TEST(cpu_groups_single_group_is_zero) {
    const std::array<std::uint32_t, 1> one{16};
    for (std::uint32_t w = 0; w < 16; ++w) EF_CHECK(group_for_worker(w, 16, one) == 0);
    EF_CHECK(group_for_worker(0, 0, one) == 0);
    EF_CHECK(group_for_worker(3, 8, std::span<const std::uint32_t>{}) == 0);
    const std::array<std::uint32_t, 2> zero{0, 0};
    EF_CHECK(group_for_worker(1, 4, zero) == 0);
}

EF_TEST(cpu_groups_spread_proportionally) {
    // 128 mantiksal CPU, iki esit grup: 128 iscinin yarisi her grupta; sira korunur (once grup 0).
    const std::array<std::uint32_t, 2> eq{64, 64};
    std::array<std::uint32_t, 2> count{};
    for (std::uint32_t w = 0; w < 128; ++w) ++count[group_for_worker(w, 128, eq)];
    EF_CHECK(count[0] == 64 && count[1] == 64);
    EF_CHECK(group_for_worker(0, 128, eq) == 0 && group_for_worker(63, 128, eq) == 0 && group_for_worker(64, 128, eq) == 1);
    // Az isci da iki gruba yayilir (hepsi grup 0'a yigilmaz).
    EF_CHECK(group_for_worker(0, 2, eq) == 0 && group_for_worker(1, 2, eq) == 1);
    // Esit olmayan gruplar (ornek 96 CPU: 64 + 32): pay 2/3 - 1/3.
    const std::array<std::uint32_t, 2> uneq{64, 32};
    count = {};
    for (std::uint32_t w = 0; w < 96; ++w) ++count[group_for_worker(w, 96, uneq)];
    EF_CHECK(count[0] == 64 && count[1] == 32);
    // Uc grup, gecerli dizin disina cikmaz.
    const std::array<std::uint32_t, 3> three{40, 40, 40};
    for (std::uint32_t w = 0; w < 300; ++w) EF_CHECK(group_for_worker(w, 300, three) < 3);
    EF_CHECK(group_for_worker(119, 120, three) == 2);
}

EF_TEST(cpu_groups_count_covers_this_group) {
    // Tum gruplarin toplami en az surecin kendi grubunun sayisi kadardir.
    const unsigned hw = std::thread::hardware_concurrency();
    EF_CHECK(logical_processor_count() >= 1);
    EF_CHECK(logical_processor_count() >= hw);
    // Tek grupta dokunmaz; cok grupta yalniz bu is parcacigini baglar. Ikisi de cokmeden doner.
    std::thread t([] { spread_current_thread(1, 2); });
    t.join();
}
