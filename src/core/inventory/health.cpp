#include "core/inventory/health.h"

#include <algorithm>

namespace ef::core::inventory {

using contracts::Confidence;

std::string_view to_string(HealthCode c) noexcept {
    switch (c) {
        case HealthCode::XmpLikelyOff: return "xmp_likely_off";
        case HealthCode::SingleDimm: return "single_dimm";
        case HealthCode::PcieWidthReduced: return "pcie_width_reduced";
        case HealthCode::ResizableBarOff: return "resizable_bar_off";
        case HealthCode::BiosOld: return "bios_old";
        case HealthCode::LowDiskSpace: return "low_disk_space";
    }
    return "?";
}

std::string_view text_key(HealthCode c) noexcept {
    switch (c) {
        case HealthCode::XmpLikelyOff: return "health.xmp_off";
        case HealthCode::SingleDimm: return "health.single_dimm";
        case HealthCode::PcieWidthReduced: return "health.pcie_width";
        case HealthCode::ResizableBarOff: return "health.rebar_off";
        case HealthCode::BiosOld: return "health.bios_old";
        case HealthCode::LowDiskSpace: return "health.low_disk";
    }
    return "?";
}

std::optional<std::uint32_t> rated_speed_from_part(std::string_view part) {
    // 4 basamakli 2133..9600 veya "C" oncesi 3 basamak (Kingston KF560C36 -> 5600) aranir.
    std::optional<std::uint32_t> best;
    for (std::size_t i = 0; i < part.size(); ++i) {
        if (part[i] < '0' || part[i] > '9') continue;
        if (i > 0 && part[i - 1] >= '0' && part[i - 1] <= '9') continue;  // sayinin basi
        std::size_t j = i;
        while (j < part.size() && part[j] >= '0' && part[j] <= '9') ++j;
        const auto len = j - i;
        std::uint32_t v = 0;
        for (std::size_t k = i; k < j && k < i + 6; ++k) v = v * 10 + static_cast<std::uint32_t>(part[k] - '0');
        std::optional<std::uint32_t> cand;
        if (len == 4 && v >= 2133 && v <= 9600) cand = v;
        else if (len == 3 && j < part.size() && (part[j] == 'C' || part[j] == 'c') && v >= 213 && v <= 960) cand = v * 10;
        if (cand && (!best || *cand > *best)) best = cand;
        i = j;
    }
    return best;
}

std::optional<std::int64_t> parse_bios_date_days(std::string_view s) {
    if (s.size() != 10 || s[2] != '/' || s[5] != '/') return std::nullopt;
    auto num = [&](std::size_t a, std::size_t n) -> std::optional<int> {
        int v = 0;
        for (std::size_t k = a; k < a + n; ++k) {
            if (s[k] < '0' || s[k] > '9') return std::nullopt;
            v = v * 10 + (s[k] - '0');
        }
        return v;
    };
    const auto m = num(0, 2), d = num(3, 2), y = num(6, 4);
    if (!m || !d || !y || *m < 1 || *m > 12 || *d < 1 || *d > 31 || *y < 1970 || *y > 2100) return std::nullopt;
    // days_from_civil (Howard Hinnant)
    std::int64_t yy = *y - (*m <= 2 ? 1 : 0);
    const std::int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    const std::int64_t yoe = yy - era * 400;
    const std::int64_t mp = (*m + 9) % 12;
    const std::int64_t doy = (153 * mp + 2) / 5 + *d - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

std::vector<HealthFinding> evaluate_health(const HealthInput& in) {
    std::vector<HealthFinding> out;

    // --- RAM ---
    if (in.ram.size() == 1) {
        out.push_back({HealthCode::SingleDimm, FixKind::GuidedHardware, Confidence::Medium,
                       "one memory module detected; actual channel use was not measured"});
    }
    for (const auto& m : in.ram) {
        if (!m.configured_mts) continue;
        std::optional<std::uint32_t> rated = rated_speed_from_part(m.part_number);
        if (m.speed_mts && (!rated || *m.speed_mts > *rated)) rated = m.speed_mts;
        if (!rated) continue;
        if (*m.configured_mts * 100 < *rated * 95) {
            // Parca numarasindan gelen anma hizi daha guvenilir isaret.
            const bool from_part = rated_speed_from_part(m.part_number).has_value();
            out.push_back({HealthCode::XmpLikelyOff, FixKind::GuidedBios, from_part ? Confidence::Medium : Confidence::Low,
                           "configured " + std::to_string(*m.configured_mts) + " MT/s < rated " + std::to_string(*rated) +
                               " MT/s" + (m.part_number.empty() ? "" : " (part " + m.part_number + ")")});
            break;  // modul basina tekrar etme
        }
    }

    // --- GPU PCIe ---
    if (in.dgpu_pcie && in.dgpu_pcie->current_width && in.dgpu_pcie->max_width &&
        *in.dgpu_pcie->current_width < *in.dgpu_pcie->max_width) {
        out.push_back({HealthCode::PcieWidthReduced, FixKind::GuidedHardware, Confidence::Medium,
                       "GPU link x" + std::to_string(*in.dgpu_pcie->current_width) + " of x" +
                           std::to_string(*in.dgpu_pcie->max_width) + " (slot, riser or M.2 lane sharing)"});
    }
    if (in.rebar_enabled.has_value() && !*in.rebar_enabled) {
        out.push_back({HealthCode::ResizableBarOff, FixKind::GuidedBios, Confidence::Low,
                       "inferred: GPU memory aperture is 256 MiB or less; GPU and firmware support not read"});
    }

    // --- BIOS ---
    if (in.bios_date && in.today_unix_days > 0) {
        if (auto d = parse_bios_date_days(*in.bios_date); d && in.today_unix_days - *d > 548) {
            out.push_back({HealthCode::BiosOld, FixKind::GuidedUpdate, Confidence::Low,
                           "date only, no newer version verified: BIOS date " + *in.bios_date + " (" + std::to_string((in.today_unix_days - *d) / 30) + " months)"});
        }
    }

    // --- Disk ---
    for (const auto& v : in.volumes) {
        if (!v.total_gib || !v.free_gib || *v.total_gib < 16) continue;
        if (*v.free_gib * 10 < *v.total_gib) {
            out.push_back({HealthCode::LowDiskSpace, FixKind::GuidedCleanup, Confidence::High,
                           v.root + " " + std::to_string(*v.free_gib) + "/" + std::to_string(*v.total_gib) + " GiB free"});
        }
    }
    return out;
}

}  // namespace ef::core::inventory
