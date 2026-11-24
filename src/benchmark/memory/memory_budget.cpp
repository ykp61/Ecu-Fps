#include "benchmark/memory/memory_budget.h"

#include <algorithm>

namespace ef::benchmark::memory {

namespace {
std::uint64_t round_down(std::uint64_t v, std::uint64_t align) { return align ? (v / align) * align : v; }
}  // namespace

Budget compute_budget(const MemorySnapshot& snap, const BudgetPolicy& p) {
    Budget b;
    const double frac_res = std::clamp(p.reserve_total_fraction, 0.0, 1.0);
    const auto by_total = static_cast<std::uint64_t>(static_cast<double>(snap.total_phys) * frac_res);
    b.reserve_bytes = std::max(p.reserve_min_bytes, by_total);

    if (snap.total_phys == 0 || snap.avail_phys == 0) {
        b.reason = "memory_status_unavailable";
        return b;
    }
    if (snap.avail_phys <= b.reserve_bytes) {
        b.reason = "free_memory_below_reserve";
        return b;
    }
    const std::uint64_t above = snap.avail_phys - b.reserve_bytes;
    const double frac_use = std::clamp(p.usable_fraction, 0.0, 1.0);
    std::uint64_t want = static_cast<std::uint64_t>(static_cast<double>(above) * frac_use);
    want = std::min(want, p.cap_bytes);
    want = round_down(want, 64 * kKiB);  // VirtualAlloc ayirma taneciklilii
    if (want < p.min_bytes || want == 0) {
        b.reason = "budget_below_min_buffer";
        return b;
    }
    b.skipped = false;
    b.bytes = want;
    return b;
}

std::optional<double> buffer_l3_ratio(std::uint64_t buffer_bytes, const CacheSizes& c) {
    if (c.l3 == 0) return std::nullopt;
    return static_cast<double>(buffer_bytes) / static_cast<double>(c.l3);
}

bool is_dram_ratio(const std::optional<double>& ratio) { return ratio && *ratio >= kDramMinL3Ratio; }

std::vector<PlannedBuffer> plan_buffers(const CacheSizes& c, std::uint64_t budget_bytes) {
    const std::uint64_t l1 = c.l1d ? c.l1d : 32 * kKiB;
    const std::uint64_t l2 = c.l2 ? c.l2 : 512 * kKiB;
    const std::uint64_t l3 = c.l3 ? c.l3 : 8 * kMiB;

    // Cache seviyelerinde verinin rahatca sigdigi (yari) boy; buyuk tampon icin 16*L3 tercih edilir.
    std::vector<PlannedBuffer> wanted = {
        {std::max<std::uint64_t>(l1 / 2, 4 * kKiB), "L1"},
        {std::max<std::uint64_t>(l2 / 2, l1 * 2), "L2"},
        {std::max<std::uint64_t>(l3 / 2, l2 * 2), "L3"},
        {std::max<std::uint64_t>(l3 * kDramPreferredL3Multiple, 64 * kMiB), "LARGE"},
    };
    std::vector<PlannedBuffer> out;
    std::uint64_t prev = 0;
    for (auto& w : wanted) {
        w.bytes = round_down(w.bytes, 4 * kKiB);
        if (w.level_hint == "LARGE") {
            // Butceye sigmazsa butcenin kendisi denenir. L3'ten buyuk degilse cache olcumu olur -> eklenmez.
            // Etiket orana gore: >= 8*L3 (L3 algilandi) "DRAM", aksi halde "LARGE" (buyuk tampon).
            const std::uint64_t d = std::min(w.bytes, round_down(budget_bytes, 4 * kKiB));
            if (d <= l3) continue;
            w.bytes = d;
            if (is_dram_ratio(buffer_l3_ratio(d, c))) w.level_hint = "DRAM";
        }
        if (w.bytes == 0 || w.bytes > budget_bytes || w.bytes <= prev) continue;
        prev = w.bytes;
        out.push_back(w);
    }
    return out;
}

MtSlicePlan plan_mt_slices(std::uint64_t buffer_bytes, std::uint32_t threads) {
    MtSlicePlan p;
    if (threads == 0) threads = 1;
    const std::uint64_t max_threads = buffer_bytes / kMinMtSlice;
    if (max_threads == 0) return p;
    const auto t = static_cast<std::uint32_t>(std::min<std::uint64_t>(threads, max_threads));
    const std::uint64_t slice = round_down(buffer_bytes / t, 64 * kKiB);
    if (slice < kMinMtSlice) return p;
    p.threads = t;
    p.slice_bytes = slice;
    return p;
}

}  // namespace ef::benchmark::memory
