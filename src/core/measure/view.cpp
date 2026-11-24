#include "core/measure/view.h"

#include <cmath>
#include <cstdio>

#include "core/insights/comparison_policy.h"

namespace ef::core::measure {

ResultView ingame_result_view(std::string title, const RunManifest& manifest, const FrameStats& stats, std::int64_t when_unix_ms) {
    ResultView r;
    r.source = TestSource::InGame;
    r.title = std::move(title);
    r.when_unix_ms = when_unix_ms;
    r.diagnosis = Diagnosis::Insufficient;  // tek yakalama teshis raporu degildir
    r.run_valid = manifest.valid;
    r.confidence = manifest.valid ? contracts::Confidence::Medium : contracts::Confidence::Low;
    // Yeterli kare varsa degerler gosterilir; kosu gecersizse dusuk guven + neden bunu acikca sinirlar.
    if (stats.sufficient && stats.avg_fps && stats.low1_fps && stats.p99_ms) {
        r.avg_fps = contracts::Measurement::of(*stats.avg_fps, contracts::Unit::Fps);
        r.low1_fps = contracts::Measurement::of(*stats.low1_fps, contracts::Unit::Fps);
        r.p99_ms = contracts::Measurement::of(*stats.p99_ms, contracts::Unit::Milliseconds);
        r.pattern = stats.pattern;
        if (stats.low01_fps) r.low01_fps = contracts::Measurement::of(*stats.low01_fps, contracts::Unit::Fps);
        if (stats.p99_9_ms) r.p99_9_ms = contracts::Measurement::of(*stats.p99_9_ms, contracts::Unit::Milliseconds);
        if (std::isfinite(stats.max_ms) && stats.max_ms > 0) r.worst_ms = contracts::Measurement::of(stats.max_ms, contracts::Unit::Milliseconds);
    }
    for (const auto& e : manifest.exclusions)
        if (e != Exclusion::None) r.evidence_keys.push_back("bench.run." + std::string(to_string(e)));
    return r;
}

std::string_view to_string(TestPhase p) noexcept {
    switch (p) {
        case TestPhase::Idle: return "idle";
        case TestPhase::Preparing: return "preparing";
        case TestPhase::Running: return "running";
        case TestPhase::Stopping: return "stopping";
        case TestPhase::Done: return "done";
        case TestPhase::Cancelled: return "cancelled";
        case TestPhase::Failed: return "failed";
    }
    return "unknown";
}

bool scene_visible(const MeasureState& s) noexcept {
    return s.source == TestSource::Builtin && s.phase == TestPhase::Running && s.builtin_available;
}

std::string_view smoothness_key(const contracts::Measurement& avg, const contracts::Measurement& low1, FramePattern pattern) noexcept {
    switch (pattern) {
        case FramePattern::Stable: return "smooth.steady";
        case FramePattern::MicroStutter: return "smooth.dips";
        case FramePattern::Spiky: return "smooth.heavy";
        case FramePattern::Insufficient: break;
    }
    if (!avg.has_value() || !low1.has_value() || !(*avg.value() > 0.0)) return "smooth.unknown";
    const double ratio = *low1.value() / *avg.value();
    if (ratio >= 0.8) return "smooth.steady";
    return ratio >= 0.6 ? "smooth.dips" : "smooth.heavy";
}

bool can_start(const MeasureState& s) noexcept {
    const bool idle = s.phase == TestPhase::Idle || s.phase == TestPhase::Done || s.phase == TestPhase::Cancelled ||
                      s.phase == TestPhase::Failed;
    const bool available = s.source == TestSource::Builtin ? s.builtin_available && !s.gate_blocked : s.ingame_available;
    return idle && available && !s.endurance.running;  // R13: uzun test surerken ikinci test baslamaz
}

void adopt_storage_roots(MeasureState& s, std::vector<std::string> measurable_roots) {
    if (measurable_roots.size() > kMaxStorageRoots) measurable_roots.resize(kMaxStorageRoots);
    // Onceki secim birim adiyla izlenir (liste sirasi degisse de ayni birim secili kalir); yoksa "Yapma".
    std::string chosen;
    if (s.storage_choice > 0 && s.storage_choice <= s.storage_roots.size()) chosen = s.storage_roots[s.storage_choice - 1];
    s.storage_roots = std::move(measurable_roots);
    s.storage_choice = 0;
    if (!chosen.empty())
        for (std::size_t k = 0; k < s.storage_roots.size(); ++k)
            if (s.storage_roots[k] == chosen) s.storage_choice = k + 1;
}

bool can_stop(const MeasureState& s) noexcept {
    return s.phase == TestPhase::Preparing || s.phase == TestPhase::Running;
}

bool busy(const MeasureState& s) noexcept {
    return s.phase == TestPhase::Preparing || s.phase == TestPhase::Running || s.phase == TestPhase::Stopping;
}

bool live_view_shown(const MeasureState& s) noexcept { return busy(s) && s.source == TestSource::Builtin && s.live_view.active; }

bool endurance_available(const MeasureState& s) noexcept { return s.builtin_available && !s.gate_blocked; }

bool endurance_can_start(const MeasureState& s) noexcept {
    return endurance_available(s) && !busy(s) && !s.endurance.running;
}

const BalanceView* balance_card_view(const MeasureState& s) noexcept {
    if (s.result && s.result->balance) return &*s.result->balance;
    if (s.balance_latest) return &*s.balance_latest;
    return nullptr;
}

bool balance_report_offered(const MeasureState& s) noexcept {
    const BalanceView* v = balance_card_view(s);
    return v && balance_view_unmeasured(*v);
}

bool proof_can_start(const MeasureState& s) noexcept {
    return s.builtin_available && !s.game_running && !s.gate_blocked && !busy(s) && !s.endurance.running && !s.proof.running();
}

double endurance_elapsed_s(const MeasureState& s, std::int64_t now_ms) noexcept {
    if (s.endurance.running && s.endurance.started_ms > 0 && now_ms >= s.endurance.started_ms) {
        const double live = static_cast<double>(now_ms - s.endurance.started_ms) / 1000.0;
        return live > s.endurance.elapsed_s ? live : s.endurance.elapsed_s;
    }
    return std::isfinite(s.endurance.elapsed_s) && s.endurance.elapsed_s > 0 ? s.endurance.elapsed_s : 0.0;
}

std::optional<std::pair<int, int>> stage_position(std::string_view k) noexcept {
    static constexpr std::string_view kStages[kBuiltinStages] = {"measure.stage_scene", "measure.stage_cpu", "measure.stage_memory",
                                                                  "measure.stage_storage", "measure.stage_combined"};
    for (int i = 0; i < kBuiltinStages; ++i)
        if (k == kStages[i]) return std::pair<int, int>{i + 1, kBuiltinStages};
    if (const auto b = balance_stage_index(k)) return std::pair<int, int>{*b, kBalanceStages};
    return std::nullopt;
}

std::optional<double> stage_fraction(const MeasureState& s) noexcept {
    if (s.phase != TestPhase::Running || s.source != TestSource::Builtin) return std::nullopt;
    const auto p = stage_position(s.stage_key);
    if (!p) return std::nullopt;
    return static_cast<double>(p->first - 1) / p->second;
}

double display_elapsed_s(const MeasureState& s, std::int64_t now_ms) noexcept {
    if (busy(s) && s.started_ms > 0 && now_ms >= s.started_ms) {
        const double live = static_cast<double>(now_ms - s.started_ms) / 1000.0;
        return live > s.elapsed_s ? live : s.elapsed_s;
    }
    return std::isfinite(s.elapsed_s) && s.elapsed_s > 0 ? s.elapsed_s : 0.0;
}

std::optional<Announcement> measure_announcement(const MeasureState& a, const MeasureState& b) noexcept {
    if (a.phase == b.phase) {
        if (b.phase == TestPhase::Running && a.stage_key != b.stage_key && stage_position(b.stage_key))
            return Announcement{AnnounceKind::Stage, false};
        return std::nullopt;
    }
    switch (b.phase) {
        case TestPhase::Preparing: return Announcement{AnnounceKind::Started, false};
        case TestPhase::Running:
            if (!busy(a)) return Announcement{AnnounceKind::Started, false};
            if (stage_position(b.stage_key)) return Announcement{AnnounceKind::Stage, false};
            return std::nullopt;
        case TestPhase::Stopping: return Announcement{AnnounceKind::Stopping, false};
        case TestPhase::Done: return busy(a) ? std::optional<Announcement>(Announcement{AnnounceKind::Done, true}) : std::nullopt;
        case TestPhase::Cancelled: return Announcement{AnnounceKind::Cancelled, true};
        case TestPhase::Failed: return Announcement{AnnounceKind::Failed, true};
        case TestPhase::Idle: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<double> progress_fraction(const MeasureState& s) noexcept {
    if (s.phase != TestPhase::Running || !(s.total_s > 0) || !std::isfinite(s.elapsed_s)) return std::nullopt;
    const double f = s.elapsed_s / s.total_s;
    return f < 0 ? 0.0 : (f > 1 ? 1.0 : f);
}

std::string format_number(double v, int decimals, bool turkish) {
    if (!std::isfinite(v)) return "—";
    if (decimals < 0) decimals = 0;
    if (decimals > 2) decimals = 2;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s(buf);
    if (s == "-0" || s == "-0.0" || s == "-0.00") s.erase(0, 1);
    // Binlik ayirici: tr "." / en ",", ondalik: tr "," / en "."
    const auto dot = s.find('.');
    std::string int_part = s.substr(0, dot == std::string::npos ? s.size() : dot);
    std::string frac = dot == std::string::npos ? "" : s.substr(dot + 1);
    bool neg = !int_part.empty() && int_part[0] == '-';
    if (neg) int_part.erase(0, 1);
    std::string grouped;
    const int n = static_cast<int>(int_part.size());
    for (int i = 0; i < n; ++i) {
        grouped.push_back(int_part[static_cast<std::size_t>(i)]);
        const int left = n - 1 - i;
        if (left > 0 && left % 3 == 0 && n > 4) grouped.push_back(turkish ? '.' : ',');  // 4 haneli sayilar gruplanmaz
    }
    std::string out = (neg ? "-" : "") + grouped;
    if (!frac.empty()) out += (turkish ? "," : ".") + frac;
    return out;
}

std::string_view missing_key(contracts::MissingReason r) noexcept {
    using contracts::MissingReason;
    switch (r) {
        case MissingReason::None: return "";
        case MissingReason::NotSupported: return "missing.not_supported";
        case MissingReason::PermissionDenied: return "missing.permission";
        case MissingReason::NotSampled: return "missing.not_sampled";
        case MissingReason::SourceError: return "missing.source_error";
        case MissingReason::Invalid: return "missing.invalid";
    }
    return "missing.not_sampled";
}

std::string_view diagnosis_key(Diagnosis d) noexcept {
    switch (d) {
        case Diagnosis::None: return "diagnosis.none";
        case Diagnosis::Limited: return "diagnosis.limited";
        case Diagnosis::Mixed: return "diagnosis.mixed";
        case Diagnosis::Insufficient: return "test.insufficient";
    }
    return "test.insufficient";
}

const core::insights::HistoryEntry* comparison_base(const std::vector<core::insights::HistoryEntry>& prior,
                                                    const core::insights::HistoryEntry& current) noexcept {
    for (const auto& prev : prior)
        if (prev.source == current.source && !prev.fingerprint.empty() && prev.fingerprint == current.fingerprint) return &prev;
    return nullptr;
}

ComparisonView reopen_comparison(const std::vector<core::insights::HistoryEntry>& entries, const ResultView* result) {
    if (entries.empty() || !result || entries.front().source != "builtin" || result->when_unix_ms != entries.front().when_unix_ms) return {};
    const std::vector<core::insights::HistoryEntry> prior(entries.begin() + 1, entries.end());
    if (const auto* prev = comparison_base(prior, entries.front())) return measure_comparison(*prev, entries.front(), "scene_avg_fps");
    return {};
}

ComparisonView measure_comparison(const core::insights::HistoryEntry& base, const core::insights::HistoryEntry& current,
                                  std::string_view avg_fps_key) {
    ComparisonView out;
    // B3: gecmis karsilastirmasiyla ayni ortak uyumluluk politikasi.
    const auto compat =
        core::insights::compare_compatibility(core::insights::comparison_inputs(base), core::insights::comparison_inputs(current));
    if (!compat.available) return out;
    out.available = true;
    // Metrik basina ortak karar: sahne tamamlanmadiysa FPS farki yok; neden ayni anahtarla soylenir.
    const auto verdict = core::insights::metric_comparable(compat, base, current, avg_fps_key);
    if (!verdict.allowed && compat.comparable && verdict.note_key == "comparison.metric_not_measured") {
        out.verdict = Verdict::Uncertain;  // kosullar uyumlu ama iki tarafta da FPS yok: fark uretilmez, engel de yok
        return out;
    }
    if (!verdict.allowed) {
        out.comparable = false;
        out.note_key = verdict.note_key.empty() ? compat.note_key : verdict.note_key;
        out.verdict = Verdict::Uncertain;
        return out;
    }
    const auto b = base.metrics.find(std::string(avg_fps_key));
    const auto c = current.metrics.find(std::string(avg_fps_key));
    if (b->second != 0.0) {
        out.delta_percent = (c->second - b->second) / b->second * 100.0;
    }
    // Tek kosu: iyilesme/gerileme dogrulanamaz. Fark yalniz tanimlayici olarak gosterilir.
    out.verdict = Verdict::Uncertain;
    return out;
}

std::string_view verdict_key(Verdict v) noexcept {
    switch (v) {
        case Verdict::Improved: return "comparison.improved";
        case Verdict::Regressed: return "comparison.regressed";
        case Verdict::Uncertain: return "comparison.uncertain";
        case Verdict::NoCohort: return "comparison.no_cohort";
    }
    return "comparison.no_cohort";
}


std::string_view confidence_key(contracts::Confidence c) noexcept {
    switch (c) {
        case contracts::Confidence::High: return "measure.confidence_high";
        case contracts::Confidence::Medium: return "measure.confidence_medium";
        default: return "measure.confidence_low";
    }
}

}  // namespace ef::core::measure
