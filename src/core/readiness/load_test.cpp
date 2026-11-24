#include "core/readiness/load_test.h"

#include <algorithm>
#include <cmath>

namespace ef::core::readiness {

namespace {

constexpr std::size_t kMinRtt = 10;        // faz basina en az gecerli RTT
constexpr double kSaturatedLinkShare = 0.8;  // hat hizinin bu kadari kullanildiysa doymus sayilir
constexpr double kBloatMs = 30.0;            // p95 farki bunun ustu: belirgin yuk gecikmesi

std::optional<double> p95(const std::vector<std::optional<double>>& v) {
    std::vector<double> x;
    for (const auto& s : v)
        if (s && std::isfinite(*s) && *s >= 0) x.push_back(*s);
    if (x.size() < kMinRtt) return std::nullopt;
    std::sort(x.begin(), x.end());
    const auto idx = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(x.size()))) - 1;
    return x[std::min(idx, x.size() - 1)];
}

double loss_pct(const std::vector<std::optional<double>>& v) {
    if (v.empty()) return 0;
    std::size_t lost = 0;
    for (const auto& s : v) lost += !s || !std::isfinite(*s) || *s < 0;
    return 100.0 * static_cast<double>(lost) / static_cast<double>(v.size());
}

bool saturated(const LoadPhase& p, const std::optional<double>& link) {
    if (link && *link > 0 && p.mbps >= *link * kSaturatedLinkShare) return true;
    return p.plateau && p.seconds >= 8.0;
}

}  // namespace

std::string_view to_string(LoadVerdict v) noexcept {
    switch (v) {
        case LoadVerdict::Ok: return "ok";
        case LoadVerdict::Bloat: return "bloat";
        case LoadVerdict::LossUnderLoad: return "loss_under_load";
        case LoadVerdict::InsufficientLoad: return "insufficient_load";
        case LoadVerdict::BlockedBackend: return "blocked_backend";
        case LoadVerdict::Cancelled: return "cancelled";
        case LoadVerdict::BudgetReached: return "budget_reached";
        case LoadVerdict::Gated: return "gated";
        case LoadVerdict::NoBaseline: return "no_baseline";
    }
    return "blocked_backend";
}

std::optional<LoadVerdict> load_budget_stop(double elapsed_s, std::uint64_t bytes, bool cancelled) noexcept {
    if (cancelled) return LoadVerdict::Cancelled;
    if (elapsed_s >= kLoadMaxSeconds || bytes >= kLoadMaxBytes) return LoadVerdict::BudgetReached;
    return std::nullopt;
}

LoadTestResult evaluate_load_test(const LoadTestInput& in) {
    LoadTestResult r;
    if (!in.endpoint_configured) return r;  // BlockedBackend
    if (!in.gate_open) {
        r.verdict = LoadVerdict::Gated;
        return r;
    }
    if (const auto stop = load_budget_stop(in.elapsed_s, in.bytes, in.cancelled); stop && *stop == LoadVerdict::Cancelled) {
        r.verdict = *stop;
        return r;
    }
    r.idle_p95_ms = p95(in.idle_rtt_ms);
    if (!r.idle_p95_ms) {  // ICMP engelli veya kayipli: taban yok, "internet bozuk" denmez
        r.verdict = LoadVerdict::NoBaseline;
        return r;
    }
    const auto down = p95(in.download.rtt_ms), up = p95(in.upload.rtt_ms);
    if (down) r.bloat_down_ms = *down - *r.idle_p95_ms;
    if (up) r.bloat_up_ms = *up - *r.idle_p95_ms;
    r.idle_loss_pct = loss_pct(in.idle_rtt_ms);
    r.down_loss_pct = loss_pct(in.download.rtt_ms);
    r.up_loss_pct = loss_pct(in.upload.rtt_ms);
    const bool bloat = (r.bloat_down_ms && *r.bloat_down_ms > kBloatMs) || (r.bloat_up_ms && *r.bloat_up_ms > kBloatMs);
    if (bloat) {
        r.verdict = LoadVerdict::Bloat;  // doymasa da gozlenen artis gercektir
        return r;
    }
    // Yuk altinda belirgin kayip: gecikme dusuk gorunse de "iyi" denmez.
    if (std::max(r.down_loss_pct, r.up_loss_pct) - r.idle_loss_pct > 5.0) {
        r.verdict = LoadVerdict::LossUnderLoad;
        return r;
    }
    // Artis yok ama hat doymadiysa "iyi" denmez.
    if (!down || !up || !saturated(in.download, in.link_mbps) || !saturated(in.upload, in.link_mbps)) {
        r.verdict = LoadVerdict::InsufficientLoad;  // butce dolsa da doyurulamadiysa ayni sonuc
        return r;
    }
    r.verdict = LoadVerdict::Ok;
    return r;
}

}  // namespace ef::core::readiness
