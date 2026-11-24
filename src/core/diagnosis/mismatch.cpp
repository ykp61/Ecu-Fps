#include "core/diagnosis/mismatch.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace ef::core::diagnosis {

namespace {
struct Mean {
    double sum = 0;
    std::size_t n = 0;
    void add(double v) noexcept { sum += v, ++n; }
    std::optional<double> value() const noexcept { return n ? std::optional<double>(sum / static_cast<double>(n)) : std::nullopt; }
};
bool valid_perf(const std::optional<double>& v) noexcept { return v && std::isfinite(*v) && *v > 0.0; }
bool valid_limit(const std::optional<double>& v) noexcept { return v && std::isfinite(*v) && *v >= 0.0 && *v <= 100.0; }
bool below(const std::optional<std::uint32_t>& cur, const std::optional<std::uint32_t>& max) noexcept { return cur && max && *cur < *max; }
}  // namespace

CpuSpeedFinding evaluate_cpu_speed(std::span<const CpuSpeedSample> samples, std::span<const CpuSpeedWindow> windows) noexcept {
    CpuSpeedFinding f;
    for (const CpuSpeedWindow& w : windows) {
        if (!std::isfinite(w.begin_s) || !std::isfinite(w.end_s) || !(w.end_s > w.begin_s)) continue;
        const double seg = std::min(kCpuSpeedSegmentS, (w.end_s - w.begin_s) / 2.0);
        Mean first, last;
        for (const CpuSpeedSample& s : samples) {
            if (!std::isfinite(s.t_s) || s.t_s < w.begin_s || s.t_s > w.end_s) continue;
            if (valid_limit(s.limit_pct)) {
                f.measured = true;
                if (!f.min_limit_pct || *s.limit_pct < *f.min_limit_pct) f.min_limit_pct = *s.limit_pct;
            }
            if (!valid_perf(s.performance_pct)) continue;
            // Dilimler ortusmez (dilim <= pencere / 2); tam ortadaki ornek ilk dilime sayilir.
            if (s.t_s <= w.begin_s + seg) first.add(*s.performance_pct);
            else if (s.t_s >= w.end_s - seg) last.add(*s.performance_pct);
        }
        const auto a = first.value(), b = last.value();
        if (!a || !b) continue;
        f.measured = true;
        // Karar carpimla verilir (ilk - son > 0,15 x ilk): tam esikteki olcum (120 -> 102) bolme yuvarlamasiyla
        // "dusus" sayilmaz. Oran yalniz raporlanir.
        if (*a - *b > kCpuSpeedDropRatio * *a) f.drop = true;
        const double ratio = 1.0 - *b / *a;
        if (!f.drop_ratio || ratio > *f.drop_ratio) {
            f.drop_ratio = ratio;
            f.first_pct = a;
            f.last_pct = b;
        }
    }
    f.limited = f.min_limit_pct && *f.min_limit_pct < kCpuPerfLimitFull;
    return f;
}

PcieSample pcie_sample_from(const PcieLoadReading& r) noexcept {
    // Yuva azamisi OKUNAN degerdir. Okunamadiysa yalniz mantiksal sonuc yazilir: link kartin azamisinde calisiyorsa
    // yuva en az o kadarini destekliyordur. Link kart azamisinin altindaysa ve yuva okunamadiysa bos kalir: yuva
    // siniri (Gen 5 kart, Gen 4 yuva: olagan) ile egitim dususu ayirt edilemez, kart azamisi yuva yerine YAZILMAZ.
    const auto slot = [](const std::optional<std::uint32_t>& read, const std::optional<std::uint32_t>& cur,
                         const std::optional<std::uint32_t>& card) -> std::optional<std::uint32_t> {
        if (read) return read;
        return cur && card && *cur >= *card ? card : std::nullopt;
    };
    PcieSample s;
    s.gpu_load_pct = r.gpu_load_pct;
    s.cur_gen = r.cur_gen;
    s.cur_width = r.cur_width;
    s.card_max_gen = r.max_gen;
    s.card_max_width = r.max_width;
    s.slot_max_gen = slot(r.slot_max_gen, r.cur_gen, r.max_gen);
    s.slot_max_width = slot(r.slot_max_width, r.cur_width, r.max_width);
    return s;
}

PcieLoadFinding evaluate_pcie_under_load(const PcieLoadReading& r) {
    PcieLoadFinding f;
    f.platform = evaluate_pcie({pcie_sample_from(r)});
    // "Yuk altinda olculdu": okuma anindaki GPU yuku esigin ustunde VE en az bir boyut kart azamisiyle
    // karsilastirilabiliyor. Bu, kuralin sinifindan bagimsizdir: yuva azamisi okunamadiginda kural hukum vermez
    // (Insufficient) ama olculen "x16 / x16, Gen 4 / Gen 5" yine yuk alti okumadir.
    const bool loaded = r.gpu_load_pct && std::isfinite(*r.gpu_load_pct) && *r.gpu_load_pct >= kGpuLoadPct;
    const bool comparable = (r.cur_width && r.max_width) || (r.cur_gen && r.max_gen);
    f.measured_under_load = loaded && comparable;
    if (f.measured_under_load) {
        f.width_below_max = below(r.cur_width, r.max_width);
        f.gen_below_max = below(r.cur_gen, r.max_gen);
    }
    return f;
}

std::string pcie_link_text(const PcieLoadReading& r) {
    std::string out;
    if (r.cur_width && r.max_width) out = "x" + std::to_string(*r.cur_width) + " / x" + std::to_string(*r.max_width);
    if (r.cur_gen && r.max_gen) {
        if (!out.empty()) out += ", ";
        out += "Gen " + std::to_string(*r.cur_gen) + " / Gen " + std::to_string(*r.max_gen);
    }
    return out;
}

GameAdapterPick pick_game_adapter(std::span<const AdapterLoadSample> samples) {
    std::map<std::string, Mean> by_luid;  // sirali: esitlikte kucuk LUID
    for (const AdapterLoadSample& s : samples)
        if (!s.luid.empty() && std::isfinite(s.pct) && s.pct >= 0.0) by_luid[s.luid].add(s.pct);
    GameAdapterPick out;
    double best = -1.0;
    for (const auto& [luid, m] : by_luid)
        if (m.sum > best) {
            best = m.sum;
            out.luid = luid;
            out.mean_pct = m.value();
            out.samples = m.n;
        }
    return out;
}

GameAdapterFinding evaluate_game_adapter(const GameAdapterInput& in) noexcept {
    GameAdapterFinding f;
    if (in.game_luid.empty()) {
        f.reason = "no_game_adapter";
        return f;
    }
    if (!in.game_3d_pct || !std::isfinite(*in.game_3d_pct) || *in.game_3d_pct < kGameAdapterMinLoadPct) {
        f.reason = "low_load";
        return f;
    }
    if (in.high_perf_luid.empty()) {
        f.reason = "no_high_perf";
        return f;
    }
    if (in.game_luid == in.high_perf_luid) {
        f.cls = GameAdapterClass::OnHighPerformance;
        return f;
    }
    if (in.hardware_adapters < 2) {  // tek donanim karti varken farkli LUID: sayim tutarsiz, iddia yok
        f.reason = "single_adapter";
        return f;
    }
    f.cls = GameAdapterClass::OnOtherAdapter;
    f.level = in.laptop && !*in.laptop ? GameAdapterLevel::Advice : GameAdapterLevel::Info;
    return f;
}

bool ran_on_integrated_with_discrete(const std::string& ran_luid, const std::string& high_perf_luid, const std::string& min_power_luid,
                                     std::size_t hardware_adapters) noexcept {
    if (hardware_adapters < 2 || ran_luid.empty() || high_perf_luid.empty() || min_power_luid.empty()) return false;
    if (high_perf_luid == min_power_luid) return false;  // DXGI iki ayri kart ayirmiyor (masaustu / tek kart)
    return ran_luid == min_power_luid;
}

}  // namespace ef::core::diagnosis
