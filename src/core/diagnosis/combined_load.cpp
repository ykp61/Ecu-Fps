#include "core/diagnosis/combined_load.h"

#include <cmath>

#include "core/diagnosis/platform.h"

namespace ef::core::diagnosis {

namespace {

bool any_caps_field(const std::vector<LimitSample>& w) {
    for (const auto& s : w)
        if (s.caps) return true;
    return false;
}

std::size_t loaded_windows(const std::vector<LimitSample>& w) {
    std::size_t n = 0;
    for (const auto& s : w)
        if (s.load_pct && std::isfinite(*s.load_pct) && *s.load_pct >= kGpuLoadPct) ++n;
    return n;
}

// Pencere kumesi tek basina degerlendirilebilir mi (bayrak alani var, en az 2 yuk alti pencere, kural Unverifiable degil).
bool evaluable(const std::vector<LimitSample>& w, VendorLimit v) { return any_caps_field(w) && loaded_windows(w) >= 2 && v != VendorLimit::Unverifiable; }

bool is_verdict(CombinedLimit c) noexcept { return c == CombinedLimit::NoLimitSeen || c == CombinedLimit::CombinedOnly || c == CombinedLimit::AlsoSolo; }

}  // namespace

std::string_view combined_text_key(CombinedLimit c) noexcept {
    switch (c) {
        case CombinedLimit::NotSupported: return "diag.combined.not_supported";
        case CombinedLimit::InsufficientData: return "diag.combined.insufficient";
        case CombinedLimit::NoLimitSeen: return "diag.combined.no_limit";
        case CombinedLimit::CombinedOnly: return "diag.combined.combined_only";
        case CombinedLimit::AlsoSolo: return "diag.combined.also_solo";
    }
    return "diag.combined.insufficient";
}

CombinedLimitResult diagnose_combined_limits(const CombinedLimitInput& in, const ReleaseStatus& gate) {
    CombinedLimitResult r;
    for (const auto& s : in.combined) {
        if (!s.caps) continue;
        r.thermal_seen = r.thermal_seen || s.caps->thermal;
        r.power_seen = r.power_seen || s.caps->power;
    }
    if (in.vendor_api_missing || !any_caps_field(in.combined)) {
        r.candidate = CombinedLimit::NotSupported;
    } else {
        const VendorLimit vc = balance_vendor_limit(in.combined);
        if (!evaluable(in.combined, vc)) {
            r.candidate = CombinedLimit::InsufficientData;
        } else if (vc != VendorLimit::Throttle) {
            r.candidate = CombinedLimit::NoLimitSeen;  // FlagOnly: saat dususu yok, dogrulanmis kisit sayilmaz
        } else {
            const VendorLimit vs = balance_vendor_limit(in.solo);
            if (!evaluable(in.solo, vs)) r.candidate = CombinedLimit::InsufficientData;  // karsilastirma tabani yok
            else r.candidate = vs == VendorLimit::Throttle ? CombinedLimit::AlsoSolo : CombinedLimit::CombinedOnly;
        }
    }
    if (is_verdict(r.candidate) && !gate.ready) {
        r.published = false;
        r.text_key = "diag.combined.withheld";
        r.gate_reason_key = gate.reason_key.empty() ? std::string("diag.gate.no_real_holdout") : gate.reason_key;
    } else {
        r.published = true;
        r.text_key = combined_text_key(r.candidate);
    }
    return r;
}

}  // namespace ef::core::diagnosis
