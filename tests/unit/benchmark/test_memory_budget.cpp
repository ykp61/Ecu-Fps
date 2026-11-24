// BENCH-04: RAM butce hesabi ve tampon plani (saf fonksiyonlar).
#include <optional>
#include <string>

#include "benchmark/memory/memory_bench.h"
#include "benchmark/memory/memory_budget.h"
#include "ef_test.h"

using namespace ef::benchmark::memory;

EF_TEST(mem_budget_keeps_os_reserve) {
    BudgetPolicy p;  // varsayilan: en az 2 GiB veya toplamin %10'u pay
    const MemorySnapshot s{32 * kGiB, 16 * kGiB};
    const Budget b = compute_budget(s, p);
    EF_CHECK(!b.skipped);
    EF_CHECK(b.reserve_bytes == static_cast<std::uint64_t>(32.0 * kGiB * 0.10));
    EF_CHECK(b.bytes + b.reserve_bytes <= s.avail_phys);
    EF_CHECK(b.bytes <= p.cap_bytes);
    EF_CHECK(b.bytes == p.cap_bytes);  // bol bellekte ust sinira kirpilir
    EF_CHECK(b.bytes % (64 * kKiB) == 0);
}

EF_TEST(mem_budget_fraction_of_free_above_reserve) {
    BudgetPolicy p;
    p.cap_bytes = 64 * kGiB;  // sinir devre disi: oran hesabi gorunsun
    const MemorySnapshot s{8 * kGiB, 4 * kGiB};
    const Budget b = compute_budget(s, p);
    EF_CHECK(!b.skipped);
    EF_CHECK(b.reserve_bytes == 2 * kGiB);           // %10 (0.8 GiB) < 2 GiB sabit pay
    EF_CHECK(b.bytes == (2 * kGiB) / 4);             // (4-2) GiB * 0.25
    EF_CHECK(b.bytes + b.reserve_bytes <= s.avail_phys);
}

EF_TEST(mem_budget_too_low_is_skipped_with_reason) {
    BudgetPolicy p;
    const Budget below = compute_budget({8 * kGiB, 1 * kGiB}, p);
    EF_CHECK(below.skipped && below.bytes == 0 && !below.reason.empty());
    const Budget equal = compute_budget({8 * kGiB, 2 * kGiB}, p);
    EF_CHECK(equal.skipped && equal.bytes == 0);
    // Pay ustunde 100 MiB kalir: %25 = 25 MiB < 32 MiB en kucuk -> atlandi
    const Budget tiny = compute_budget({8 * kGiB, 2 * kGiB + 100 * kMiB}, p);
    EF_CHECK(tiny.skipped && tiny.bytes == 0 && !tiny.reason.empty());
    const Budget unread = compute_budget({0, 0}, p);
    EF_CHECK(unread.skipped && !unread.reason.empty());
}

EF_TEST(mem_budget_never_exceeds_available_sweep) {
    BudgetPolicy p;
    p.cap_bytes = 1024 * kGiB;
    p.usable_fraction = 1.0;  // en kotu durum: tum fazlalik
    for (std::uint64_t avail = 0; avail <= 64 * kGiB; avail += 37 * kMiB) {
        const Budget b = compute_budget({64 * kGiB, avail}, p);
        if (!b.skipped) EF_CHECK(b.bytes + b.reserve_bytes <= avail);
        else EF_CHECK(b.bytes == 0);
    }
}

EF_TEST(mem_plan_buffers_span_cache_levels) {
    const CacheSizes c{48 * kKiB, 2 * kMiB, 32 * kMiB};
    const auto plan = plan_buffers(c, 512 * kMiB);
    EF_CHECK(plan.size() == 4);
    if (plan.size() == 4) {
        EF_CHECK(plan[0].level_hint == "L1" && plan[0].bytes == 24 * kKiB);
        EF_CHECK(plan[1].level_hint == "L2" && plan[1].bytes == 1 * kMiB);
        EF_CHECK(plan[2].level_hint == "L3" && plan[2].bytes == 16 * kMiB);
        // Buyuk tampon 16*L3 = 512 MiB, butceye sigar -> DRAM.
        EF_CHECK(plan[3].level_hint == "DRAM" && plan[3].bytes == 512 * kMiB);
    }
    for (const auto& b : plan) EF_CHECK(b.bytes <= 512 * kMiB);
}

EF_TEST(mem_plan_labels_large_buffer_below_8x_l3) {
    const CacheSizes c{32 * kKiB, 1 * kMiB, 32 * kMiB};
    // 40 MiB < 8*L3: yine olculur ama "LARGE" (buyuk tampon), asla DRAM denmez.
    const auto small = plan_buffers(c, 40 * kMiB);
    EF_CHECK(small.size() == 4);
    for (const auto& b : small) {
        EF_CHECK(b.level_hint != "DRAM");
        EF_CHECK(b.bytes <= 40 * kMiB);
    }
    if (small.size() == 4) EF_CHECK(small[3].level_hint == "LARGE" && small[3].bytes == 40 * kMiB);
    // Tam 8*L3 = 256 MiB -> DRAM; bir sayfa eksik -> LARGE.
    const auto at8 = plan_buffers(c, 256 * kMiB);
    EF_CHECK(!at8.empty() && at8.back().level_hint == "DRAM" && at8.back().bytes == 256 * kMiB);
    const auto below8 = plan_buffers(c, 256 * kMiB - 64 * kKiB);
    EF_CHECK(!below8.empty() && below8.back().level_hint == "LARGE");
    // Butce L3'ten buyuk degil: buyuk tampon eklenmez (cache olcumu olurdu).
    const auto none = plan_buffers(c, 32 * kMiB);
    EF_CHECK(none.size() == 3);
    for (const auto& b : none) EF_CHECK(b.level_hint != "DRAM" && b.level_hint != "LARGE");
    // 96 MiB L3 (X3D): 16*L3 = 1,5 GiB varsayilan 2 GiB ust sinira sigar -> DRAM.
    const CacheSizes x3d{48 * kKiB, 1 * kMiB, 96 * kMiB};
    const auto big = plan_buffers(x3d, BudgetPolicy{}.cap_bytes);
    EF_CHECK(!big.empty() && big.back().level_hint == "DRAM" && big.back().bytes == 1536 * kMiB);
}

EF_TEST(mem_buffer_l3_ratio_and_dram_rule) {
    EF_CHECK(!buffer_l3_ratio(512 * kMiB, CacheSizes{}).has_value());  // L3 bilinmiyor -> bos
    const CacheSizes c{0, 0, 32 * kMiB};
    const auto r16 = buffer_l3_ratio(512 * kMiB, c);
    EF_CHECK(r16.has_value() && *r16 == 16.0);
    EF_CHECK(is_dram_ratio(r16));
    EF_CHECK(is_dram_ratio(buffer_l3_ratio(256 * kMiB, c)));
    EF_CHECK(!is_dram_ratio(buffer_l3_ratio(255 * kMiB, c)));
    EF_CHECK(!is_dram_ratio(std::nullopt));
    static_assert(kDramMinL3Ratio == 8.0 && kDramPreferredL3Multiple == 16);
    const MtBandwidth mt{};
    EF_CHECK(!mt.dram && !mt.contended && !mt.buffer_to_l3 && !mt.min_thread_cpu_share);
    const MemoryResult mr{};
    EF_CHECK(mr.large_buffer_bytes == 0 && !mr.large_buffer_is_dram && !mr.large_buffer_to_l3);
    static_assert(kMtUncontendedShare == 0.90);
}

EF_TEST(mem_mt_slices_partition_buffer) {
    // 512 MiB / 16 -> 32 MiB dilim, toplam tamponu asmaz.
    const auto a = plan_mt_slices(512 * kMiB, 16);
    EF_CHECK(a.threads == 16 && a.slice_bytes == 32 * kMiB);
    // Bolunemeyen boy: 64 KiB'e asagi yuvarlanir, toplam <= tampon.
    const auto b = plan_mt_slices(100 * kMiB, 24);
    EF_CHECK(b.threads == 24);
    EF_CHECK(b.slice_bytes % (64 * kKiB) == 0 && b.slice_bytes >= kMinMtSlice);
    EF_CHECK(b.threads * b.slice_bytes <= 100 * kMiB);
    // Kucuk tampon: is parcacigi sayisi en az 1 MiB dilime gore azaltilir.
    const auto c = plan_mt_slices(8 * kMiB, 32);
    EF_CHECK(c.threads == 8 && c.slice_bytes == 1 * kMiB);
    // 0 -> 1 is parcacigi; 1 MiB alti -> olculemez.
    const auto d = plan_mt_slices(64 * kMiB, 0);
    EF_CHECK(d.threads == 1 && d.slice_bytes == 64 * kMiB);
    const auto e = plan_mt_slices(512 * kKiB, 4);
    EF_CHECK(e.threads == 0 && e.slice_bytes == 0);
    for (std::uint32_t t = 1; t <= 128; t += 7)
        for (std::uint64_t mb = 1; mb <= 600; mb += 37) {
            const auto p = plan_mt_slices(mb * kMiB, t);
            EF_CHECK(p.threads >= 1 && p.threads <= t);
            EF_CHECK(static_cast<std::uint64_t>(p.threads) * p.slice_bytes <= mb * kMiB);
            EF_CHECK(p.slice_bytes >= kMinMtSlice);
        }
}

EF_TEST(mem_presets_quick_and_full) {
    const MemoryConfig def;
    EF_CHECK(def.multi_thread && def.threads == 0);
    const MemoryConfig q = quick_memory_config();
    const MemoryConfig f = full_memory_config();
    EF_CHECK(q.min_seconds_per_measure == kQuickMinSecondsPerMeasure && q.mt_seconds_per_measure == 1.5);
    EF_CHECK(f.min_seconds_per_measure == kFullMinSecondsPerMeasure && f.mt_seconds_per_measure == 5.0);
    EF_CHECK(q.max_seconds_per_measure >= q.min_seconds_per_measure);
    EF_CHECK(f.max_seconds_per_measure >= f.min_seconds_per_measure);
    EF_CHECK(f.mt_seconds_per_measure > q.mt_seconds_per_measure);
    EF_CHECK(q.multi_thread && f.multi_thread && q.threads == 0 && f.threads == 0);
    // Butce politikasi (guvenlik) hazir ayarlarda degismez.
    const BudgetPolicy p;
    for (const auto* c : {&q, &f}) {
        EF_CHECK(c->policy.cap_bytes == p.cap_bytes && c->policy.reserve_min_bytes == p.reserve_min_bytes);
        EF_CHECK(c->policy.usable_fraction == p.usable_fraction);
    }
    // Ust sinir tahmini: 4 tampon x 4 olcum x max + 3 MT pencere (kurulum haric).
    const double quick_upper = 16 * q.max_seconds_per_measure + 3 * q.mt_seconds_per_measure;
    const double full_upper = 16 * f.max_seconds_per_measure + 3 * f.mt_seconds_per_measure;
    EF_CHECK(quick_upper <= 9.0);
    EF_CHECK(full_upper <= 32.0 && full_upper >= 20.0);
    EF_CHECK(std::string(kMemBenchVersion) == "mem_v2");
}

EF_TEST(mem_plan_unknown_cache_uses_defaults) {
    const auto plan = plan_buffers(CacheSizes{}, 512 * kMiB);
    EF_CHECK(plan.size() == 4);
    // L3 bilinmiyor: varsayilan 8 MiB ile 16 kat = 128 MiB, oran bilinmedigi icin "LARGE" (DRAM degil).
    if (!plan.empty()) EF_CHECK(plan.back().level_hint == "LARGE" && plan.back().bytes == 128 * kMiB);
}
