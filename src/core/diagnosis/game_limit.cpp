#include "core/diagnosis/game_limit.h"

#include <cmath>
#include <cstddef>

namespace ef::core::diagnosis {

namespace {
// Kayan nokta siniri: 61,8 gibi degerler ikilide tam yazilamaz; esik dahil kalsin diye cok kucuk pay.
constexpr double kEdgeEps = 1e-9;

bool positive(double v) noexcept { return std::isfinite(v) && v > 0.0; }
}  // namespace

std::vector<double> vrr_cap_candidates(double refresh_hz) {
    std::vector<double> out;
    if (!positive(refresh_hz)) return out;
    for (const double c : {vrr_auto_cap_fps(refresh_hz), refresh_hz - kVrrRefreshOffsetFps})
        if (positive(c)) out.push_back(c);
    return out;
}

std::optional<double> frame_interval_cv(const std::vector<double>& intervals_ms) {
    double sum = 0;
    std::size_t n = 0;
    for (const double v : intervals_ms)
        if (positive(v)) {
            sum += v;
            ++n;
        }
    if (n < 2) return std::nullopt;
    const double mean = sum / static_cast<double>(n);
    double sq = 0;
    for (const double v : intervals_ms)
        if (positive(v)) sq += (v - mean) * (v - mean);
    const double cv = std::sqrt(sq / static_cast<double>(n - 1)) / mean;
    if (!std::isfinite(cv)) return std::nullopt;
    return cv;
}

FrameCapMatch detect_frame_cap(std::optional<double> avg_fps, std::optional<double> refresh_hz,
                               std::optional<double> interval_cv) noexcept {
    FrameCapMatch m;
    if (!avg_fps || !positive(*avg_fps)) return m;
    const double fps = *avg_fps;
    double best = 0;
    auto consider = [&](double cap, bool is_refresh, bool is_vrr) {
        if (!positive(cap)) return;
        const double err = std::fabs(fps - cap) / cap;
        if (err > kFrameCapTolerance + kEdgeEps) return;
        if (!m.suspected || err < best - kEdgeEps) {
            m.suspected = true;
            m.cap_fps = cap;
            m.is_refresh = is_refresh;
            m.is_vrr = is_vrr;
            best = err;
        }
    };
    const bool refresh_ok = refresh_hz && positive(*refresh_hz);
    if (refresh_ok) consider(*refresh_hz, true, false);  // once yenileme hizi: esit uzaklikta o bildirilir
    for (const double cap : kCommonFrameCapsFps) consider(cap, false, false);
    // Uyumluluk B4 (bosluk 14): ekranin hemen altindaki VRR sinirlari yalniz sabit kare akisinda ve baska sinir
    // eslesmediyse denenir (yenileme hizi eslesmesi daha genel olani bildirir).
    const bool steady = interval_cv && std::isfinite(*interval_cv) && *interval_cv >= 0.0 && *interval_cv <= kLowVarianceCv + kEdgeEps;
    if (steady && refresh_ok && !m.suspected) {
        // Ayni adaylar vrr_cap_candidates ile; noexcept yolda bellek ayirmamak icin burada dogrudan.
        const double r = *refresh_hz;
        consider(vrr_auto_cap_fps(r), false, true);
        consider(r - kVrrRefreshOffsetFps, false, true);
    }
    return m;
}

std::vector<GpuBusyWindow> gpu_busy_windows(const std::vector<double>& intervals_ms, const std::vector<double>& gpu_busy_ms,
                                            double window_ms) {
    std::vector<GpuBusyWindow> out;
    if (intervals_ms.empty() || intervals_ms.size() != gpu_busy_ms.size() || !positive(window_ms)) return out;
    GpuBusyWindow cur;
    double t = 0;
    for (std::size_t i = 0; i < intervals_ms.size(); ++i) {
        const double iv = intervals_ms[i], busy = gpu_busy_ms[i];
        if (!positive(iv) || !std::isfinite(busy) || busy < 0.0) continue;
        if (cur.frames == 0) cur.start_ms = t;
        t += iv;
        ++cur.frames;
        cur.interval_ms += iv;
        cur.busy_ms += busy;
        cur.end_ms = t;
        if (cur.interval_ms >= window_ms) {
            out.push_back(cur);
            cur = GpuBusyWindow{};
        }
    }
    if (cur.frames > 0) {
        if (!out.empty() && cur.interval_ms < window_ms / 2.0) {
            auto& last = out.back();
            last.frames += cur.frames;
            last.interval_ms += cur.interval_ms;
            last.busy_ms += cur.busy_ms;
            last.end_ms = cur.end_ms;
        } else {
            out.push_back(cur);
        }
    }
    for (auto& w : out) w.ratio = w.interval_ms > 0 ? w.busy_ms / w.interval_ms : 0.0;
    return out;
}

std::string_view to_string(GameLimit v) noexcept {
    switch (v) {
        case GameLimit::GpuFull: return "gpu_full";
        case GameLimit::GpuWaited: return "gpu_waited";
        case GameLimit::FrameCapSuspected: return "frame_cap";
        case GameLimit::Undetermined: return "undetermined";
    }
    return "undetermined";
}

std::string_view to_string(GameLimitReason r) noexcept {
    switch (r) {
        case GameLimitReason::None: return "none";
        case GameLimitReason::RunInvalid: return "run_invalid";
        case GameLimitReason::TooFewFrames: return "too_few_frames";
        case GameLimitReason::NoGpuTiming: return "no_gpu_timing";
        case GameLimitReason::Mixed: return "mixed";
        case GameLimitReason::Conflict: return "conflict";
    }
    return "none";
}

GameLimitResult evaluate_game_limit(const GameLimitInput& in) {
    GameLimitResult r;
    if (in.avg_fps && positive(*in.avg_fps)) r.avg_fps = in.avg_fps;

    // Olculen GPU paylari hukumden bagimsiz doldurulur (kare siniri / gecersiz kosuda da gosterilebilir).
    const auto windows = gpu_busy_windows(in.intervals_ms, in.gpu_busy_ms);
    double total_iv = 0, total_busy = 0;
    for (const auto& w : windows) {
        r.frames += w.frames;
        total_iv += w.interval_ms;
        total_busy += w.busy_ms;
        ++r.windows;
        // busy_ms == 0: pencerede kare sunuldu ama hic GPU paketi gorulmedi. Cizim yapan oyunda bu, GPU'nun beklemesi
        // degil izleme boslugudur; "bekledi" payina yazilmaz (kareleri toplamda kalir, hukmu zayiflatir).
        const bool gpu_seen = w.busy_ms > 0;
        if (w.ratio + kEdgeEps >= kGpuBusyFullRatio) {
            ++r.full_windows;
            r.full_frames += w.frames;
        } else if (gpu_seen && w.ratio - kEdgeEps <= kGpuBusyShortRatio) {
            ++r.waited_windows;
            r.waited_frames += w.frames;
        }
    }
    // Hic GPU paketi gorulmediyse (toplam 0) seri "olculmedi" sayilir: bekleme hukmune cevrilmez.
    const bool gpu_timed = r.frames > 0 && total_iv > 0 && total_busy > 0;
    if (gpu_timed) {
        const double n = static_cast<double>(r.frames);
        r.full_pct = 100.0 * static_cast<double>(r.full_frames) / n;
        r.waited_pct = 100.0 * static_cast<double>(r.waited_frames) / n;
        r.busy_ratio = total_busy / total_iv;
        r.gpu_time_approximate = in.hags_on.value_or(false);
    }

    auto undetermined = [&r](GameLimitReason why) {
        r.verdict = GameLimit::Undetermined;
        r.reason = why;
        return r;
    };
    if (!in.run_valid) return undetermined(GameLimitReason::RunInvalid);
    if (in.intervals_ms.size() < kGameLimitMinFrames || !r.avg_fps) return undetermined(GameLimitReason::TooFewFrames);

    // VRR adayi kapisi (inceleme B4): GPU suresi / kullanimi GPU'nun doldugunu gosteriyorsa aday sinir sayilmaz.
    const bool gpu_contradicts_vrr =
        (gpu_timed && ((static_cast<double>(r.full_frames) / static_cast<double>(r.frames)) + kEdgeEps >= kGameLimitDominantShare ||
                       (r.busy_ratio && *r.busy_ratio + kEdgeEps >= kGpuBusyFullRatio))) ||
        (in.game_gpu_pct && std::isfinite(*in.game_gpu_pct) && *in.game_gpu_pct >= kGpuHighPct);
    // Kare siniri once: sinirda calisan oyunda GPU'nun beklemesi / dolmasi parca hukmu degildir.
    if (const auto cap = detect_frame_cap(r.avg_fps, in.refresh_hz, frame_interval_cv(in.intervals_ms));
        cap.suspected && !(cap.is_vrr && gpu_contradicts_vrr)) {
        r.verdict = GameLimit::FrameCapSuspected;
        r.reason = GameLimitReason::None;
        r.cap_fps = cap.cap_fps;
        r.cap_is_refresh = cap.is_refresh;
        r.cap_is_vrr = cap.is_vrr;
        return r;
    }
    if (!gpu_timed) return undetermined(GameLimitReason::NoGpuTiming);
    if (r.frames < kGameLimitMinFrames || r.windows < kGameLimitMinWindows) return undetermined(GameLimitReason::TooFewFrames);

    const double n = static_cast<double>(r.frames);
    const double full_share = static_cast<double>(r.full_frames) / n;
    const double waited_share = static_cast<double>(r.waited_frames) / n;
    if (full_share + kEdgeEps >= kGameLimitDominantShare) {
        r.verdict = GameLimit::GpuFull;
        r.reason = GameLimitReason::None;
    } else if (waited_share + kEdgeEps >= kGameLimitDominantShare) {
        r.verdict = GameLimit::GpuWaited;
        r.reason = GameLimitReason::None;
    } else {
        return undetermined(GameLimitReason::Mixed);
    }
    // Capraz denetim: surecin kendi 3D motor kullanimi ayni seyi baska kaynaktan olcer. Celiski = hukum yok.
    //  - "GPU bekledi" (karelerin >= %60'inda oran <= 0,60) iken kosu ortalamasi en cok ~%76 olabilir; kullanim
    //    >= %90 ise iki olcum tutmuyor.
    //  - "GPU tam mesgul" hukmu karelerin %60'iyla verilebilir (kosu ortalamasi %54'e kadar inebilir); bu yuzden
    //    yalniz KOSU GENELI GPU suresi orani da >= 0,90 iken kullanim < %70 ise celiski sayilir (ayni buyukluk).
    if (in.game_gpu_pct && std::isfinite(*in.game_gpu_pct)) {
        const bool waited_but_busy = r.verdict == GameLimit::GpuWaited && *in.game_gpu_pct >= kGpuHighPct;
        const bool full_but_idle = r.verdict == GameLimit::GpuFull && *in.game_gpu_pct < kGpuLowPct && r.busy_ratio &&
                                   *r.busy_ratio + kEdgeEps >= kGpuBusyFullRatio;
        if (waited_but_busy || full_but_idle) return undetermined(GameLimitReason::Conflict);
    }
    return r;
}

std::optional<bool> hags_enabled_any(const std::vector<HagsAdapterState>& adapters) noexcept {
    bool any_supported = false, any_enabled_known = false, any_enabled = false;
    for (const auto& a : adapters) {
        if (!a.supported || !*a.supported) continue;
        any_supported = true;
        if (a.enabled) {
            any_enabled_known = true;
            any_enabled = any_enabled || *a.enabled;
        }
    }
    if (!any_supported || !any_enabled_known) return std::nullopt;
    return any_enabled;
}

}  // namespace ef::core::diagnosis
