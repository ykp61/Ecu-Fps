// Denge testi (spec 2026-11-12 A5): gecmis kaydi (kaynak "balance", protokol "balance_v1", duz metrik anahtarlari,
// izin listesi kosul anahtarlari), kayittan uretilen kart gorunumu, karsilastirma politikasi ve asama sirasi.
#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/diagnosis/balance.h"
#include "core/i18n/catalog.h"
#include "core/insights/comparison_policy.h"
#include "core/insights/history.h"
#include "core/measure/balance_view.h"
#include "core/measure/run_marker.h"
#include "core/measure/view.h"
#include "ef_test.h"

using namespace ef::core;
namespace dg = ef::core::diagnosis;
namespace cm = ef::core::measure;
namespace ci = ef::core::insights;

namespace {

dg::StepStats valid(double fps, std::optional<double> busy) {
    dg::StepStats s;
    s.state = dg::StepState::Valid;
    s.fps = fps;
    s.frames = 1000;
    s.seconds = 8;
    s.cv = 0.01;
    s.gpu_busy_ratio = busy;
    return s;
}

// 1080p: CPU sinirliyor (oran 2,0) · 1440p: dengeli (1,0) · 4K: GPU sinirliyor (0,5).
dg::BalanceInputs mixed_inputs() {
    dg::BalanceInputs in;
    in.cpu = valid(100, 0.30);
    in.gpu = {valid(200, 0.97), valid(100, 0.97), valid(50, 0.98)};
    in.coupled = {valid(98, 0.50), valid(95, 0.80), valid(49, 0.97)};
    in.cpu_repeat = valid(100, 0.30);
    in.gpu_repeat = valid(200, 0.97);
    in.confounders.other_cpu_pct = 2.0;
    in.confounders.other_gpu_pct = 0.0;  // olculdu: test bagdastiricisini baska surec kullanmiyor
    in.cpu_split = dg::CpuFrameSplit{6.0, 1.0, 3.0};
    return in;
}

ci::HistoryEntry entry_of(const dg::BalanceResult& r, bool quick = true, const char* preview = "off") {
    ci::HistoryEntry e;
    e.when_unix_ms = 1793600000000;
    e.source = std::string(cm::kBalanceSource);
    e.title = "arena_v1";
    e.fingerprint = "arena_v1|frame_sim_v1|quick|w1.00+m4.00|0123456789abcdef";
    e.app_version = "0.1.0";
    e.scene_version = "arena_v1";
    e.protocol_version = std::string(ci::kBalanceProtocolVersion);
    e.diagnosis_version = std::string(dg::kBalanceRuleVersion);
    e.metrics = cm::balance_metrics(r);
    e.exclusions = cm::balance_exclusions(r);
    e.run_valid = cm::balance_run_valid(r);
    e.conditions = {{"mode", quick ? "quick" : "full"}, {"preview", preview}, {"adapter", "Ornek GPU 4070"}, {"d3d_threading", "creates_on,lists_off"},
                    {"cpu", "Ornek CPU"}, {"gpu", "Ornek GPU 4070"}};
    return e;
}

// Kayit diske yazilip geri okundugunda da ayni gorunumu vermeli.
ci::HistoryEntry roundtrip(const ci::HistoryEntry& e) {
    ci::History h;
    h.add(e);
    const auto back = ci::History::parse(h.serialize());
    EF_CHECK(back.ok() && back.value().entries().size() == 1);
    return back.ok() && !back.value().entries().empty() ? back.value().entries().front() : ci::HistoryEntry{};
}

bool has(const std::vector<std::string>& v, std::string_view s) { return std::find(v.begin(), v.end(), s) != v.end(); }

}  // namespace

EF_TEST(balance_history_protocol_and_allow_list) {
    // Ayri protokol; "builtin" protokolu ve sahne kayitlari degismez.
    EF_CHECK(ci::kBalanceProtocolVersion == "balance_v1");
    EF_CHECK(ci::kBuiltinProtocolVersion == "bench_v1");
    EF_CHECK(cm::kBalanceSource == "balance");
    for (const char* k : {"preview", "adapter", "d3d_threading", "retries", "other_gpu"}) EF_CHECK(ci::allowed_condition_key(k));
    for (const char* k : {"resolution", "tier", "mode", "os", "gpu", "gpu_driver", "cpu", "power", "storage_target", "build", "game_build", "live_panel"})
        EF_CHECK(ci::allowed_condition_key(k));  // mevcut anahtarlar yerinde
    EF_CHECK(!ci::allowed_condition_key("serial") && !ci::allowed_condition_key("user_path"));
    // Duz metrik anahtarlari disa aktarma kuralina uyar ([a-z0-9_.:-], <= 64).
    const auto r = dg::evaluate_balance(mixed_inputs());
    const auto metrics = cm::balance_metrics(r);
    for (const auto& [k, v] : metrics) EF_CHECK(ci::safe_metric_key(k));
    for (const char* k : {"balance.cpu_fps", "balance.cpu_lower_bound", "balance.gpu_fps_1080", "balance.gpu_fps_1440", "balance.gpu_fps_2160",
                          "balance.gpu_lower_bound_1080", "balance.coupled_fps_2160", "balance.verdict_1080", "balance.verdict_1440",
                          "balance.verdict_2160", "balance.cpu_sim_share", "balance.cpu_draw_share"})
        EF_CHECK(metrics.count(k) == 1);
    EF_CHECK(ci::safe_metric_key(std::string(dg::kBalanceRuleVersion)));  // diagnosis_version alanina yazilir
}

EF_TEST(balance_view_roundtrips_verdicts_through_history) {
    const auto r = dg::evaluate_balance(mixed_inputs());
    EF_CHECK(r.rows[0].verdict == dg::BalanceVerdict::CpuLimited && r.rows[1].verdict == dg::BalanceVerdict::Balanced &&
              r.rows[2].verdict == dg::BalanceVerdict::GpuLimited);
    EF_CHECK(cm::balance_run_valid(r));
    EF_CHECK(cm::balance_exclusions(r).empty());  // hukum var, neden / not yok
    const auto e = roundtrip(entry_of(r, false));
    const auto v = cm::balance_view_from_history(e);
    EF_CHECK(v.has_value());
    if (!v) return;
    EF_CHECK(v->rows[0].verdict == dg::BalanceVerdict::CpuLimited && v->rows[1].verdict == dg::BalanceVerdict::Balanced &&
              v->rows[2].verdict == dg::BalanceVerdict::GpuLimited);
    for (const auto& row : v->rows) EF_CHECK(row.reason.empty() && !row.gpu_lower_bound);
    EF_CHECK(v->cpu_fps && *v->cpu_fps == 100.0 && !v->cpu_lower_bound);
    EF_CHECK(v->rows[0].gpu_fps && *v->rows[0].gpu_fps == 200.0 && v->rows[2].gpu_fps && *v->rows[2].gpu_fps == 50.0);
    EF_CHECK(v->rows[1].coupled_fps && *v->rows[1].coupled_fps == 95.0);
    EF_CHECK(v->global_reasons.empty() && v->notes.empty());
    EF_CHECK(v->cpu_sim_share && v->cpu_draw_share && *v->cpu_sim_share > 0.59 && *v->cpu_sim_share < 0.61 && *v->cpu_draw_share > 0.39 &&
              *v->cpu_draw_share < 0.41);
    EF_CHECK(v->adapter == "Ornek GPU 4070" && !v->quick && !v->preview && v->when_unix_ms == 1793600000000);
    EF_CHECK(v->has(dg::BalanceVerdict::CpuLimited) && v->has(dg::BalanceVerdict::GpuLimited) && !v->has(dg::BalanceVerdict::Unmeasurable));
}

EF_TEST(balance_view_keeps_lower_bounds_and_row_reasons) {
    auto in = mixed_inputs();
    in.gpu[0].gpu_busy_ratio = 0.50;  // GPU adiminda GPU tam mesgul degil: "en az"; CPU hukmunu guclendirir -> hukum kalir
    in.gpu[2].gpu_busy_ratio = 0.50;  // "en az" deger GPU hukmunu desteklemez -> Belirlenemedi
    const auto r = dg::evaluate_balance(in);
    EF_CHECK(r.rows[0].verdict == dg::BalanceVerdict::CpuLimited && r.rows[0].gpu_lower_bound);
    EF_CHECK(r.rows[2].verdict == dg::BalanceVerdict::Undetermined && r.rows[2].reason == dg::balance_reason::kGpuLowerBound);
    EF_CHECK(has(cm::balance_exclusions(r), "balance.row_2160.gpu_lower_bound"));
    const auto v = cm::balance_view_from_history(roundtrip(entry_of(r)));
    EF_CHECK(v.has_value());
    if (!v) return;
    EF_CHECK(v->rows[0].verdict == dg::BalanceVerdict::CpuLimited && v->rows[0].gpu_lower_bound && v->rows[0].reason.empty());
    EF_CHECK(v->rows[2].verdict == dg::BalanceVerdict::Undetermined && v->rows[2].reason == "gpu_lower_bound" && v->rows[2].gpu_lower_bound);
    EF_CHECK(v->rows[2].gpu_fps && *v->rows[2].gpu_fps == 50.0);  // olculen deger gosterilir; hukum yok
    EF_CHECK(v->quick);
    // CPU adiminda GPU bos degilse cpu_fps "en az"dir.
    auto in2 = mixed_inputs();
    in2.cpu.gpu_busy_ratio = 0.80;
    in2.cpu_repeat.gpu_busy_ratio = 0.80;
    const auto v2 = cm::balance_view_from_history(roundtrip(entry_of(dg::evaluate_balance(in2))));
    EF_CHECK(v2 && v2->cpu_lower_bound && v2->cpu_fps);
    if (v2) EF_CHECK(v2->rows[0].verdict == dg::BalanceVerdict::Undetermined && v2->rows[0].reason == "cpu_lower_bound");
}

EF_TEST(balance_view_global_reason_and_unmeasurable_row) {
    // Pilde: butun satirlar tek nedenle Belirlenemedi; kosu karsilastirmada gecersiz.
    auto in = mixed_inputs();
    in.confounders.on_battery = true;
    in.confounders.other_cpu_pct = 35.0;
    const auto r = dg::evaluate_balance(in);
    EF_CHECK(!cm::balance_run_valid(r));
    const auto ex = cm::balance_exclusions(r);
    EF_CHECK(has(ex, "balance.global.on_battery") && has(ex, "balance.global.background_cpu"));
    EF_CHECK(std::none_of(ex.begin(), ex.end(), [](const std::string& s) { return s.rfind("balance.row_", 0) == 0; }));  // genel neden bir kez yazilir
    const auto v = cm::balance_view_from_history(roundtrip(entry_of(r)));
    EF_CHECK(v.has_value());
    if (v) {
        EF_CHECK(v->global_reasons.size() == 2 && v->global_reasons[0] == "on_battery" && v->global_reasons[1] == "background_cpu");
        for (const auto& row : v->rows) EF_CHECK(row.verdict == dg::BalanceVerdict::Undetermined && row.reason == "on_battery");
        EF_CHECK(v->cpu_fps && v->rows[0].gpu_fps);  // olculen sayilar yine gosterilir
    }
    // 4K hedefi bellege sigmadi: o satir "olculemedi" (neden vram_budget); digerleri hukum alir.
    auto in2 = mixed_inputs();
    in2.gpu[2] = dg::unmeasurable_step(dg::balance_reason::kVramBudget);
    in2.coupled[2] = dg::unmeasurable_step(dg::balance_reason::kVramBudget);
    const auto r2 = dg::evaluate_balance(in2);
    EF_CHECK(cm::balance_run_valid(r2));
    const auto v2 = cm::balance_view_from_history(roundtrip(entry_of(r2)));
    EF_CHECK(v2.has_value());
    if (v2) {
        EF_CHECK(v2->rows[2].verdict == dg::BalanceVerdict::Unmeasurable && v2->rows[2].reason == "vram_budget" && !v2->rows[2].gpu_fps);
        EF_CHECK(v2->rows[0].verdict == dg::BalanceVerdict::CpuLimited && v2->rows[1].verdict == dg::BalanceVerdict::Balanced);
    }
    // Diger sureclerin CPU'su okunamadi: hukum engellenmez, not olarak tasinir.
    auto in3 = mixed_inputs();
    in3.confounders.other_cpu_pct.reset();
    const auto v3 = cm::balance_view_from_history(roundtrip(entry_of(dg::evaluate_balance(in3))));
    EF_CHECK(v3 && has(v3->notes, "background_cpu_unknown") && v3->rows[0].verdict == dg::BalanceVerdict::CpuLimited);
}

// denge_v4: CPU adiminin asil nedeni kayda yazilir ve gorunume geri gelir; eski kayit (anahtarsiz) bos kalir.
EF_TEST(balance_view_cpu_step_sub_reason_roundtrip) {
    auto in = mixed_inputs();
    in.cpu = dg::summarize_step(std::vector<double>(500, 10.0), {}, 8.0, 0, false);  // too_few_seconds
    const auto r = dg::evaluate_balance(in);
    const auto ex = cm::balance_exclusions(r);
    EF_CHECK(has(ex, "balance.global.cpu_step_invalid") && has(ex, "detail.cpu_step.too_few_seconds"));
    const auto v = cm::balance_view_from_history(roundtrip(entry_of(r)));
    EF_CHECK(v.has_value());
    if (v) {
        EF_CHECK(v->global_reasons.size() == 1 && v->global_reasons[0] == "cpu_step_invalid");
        EF_CHECK(v->cpu_step_reason == "too_few_seconds" && v->cpu_repeat_reason.empty());
        EF_CHECK(!cm::balance_reason_key(v->cpu_step_reason).empty() && cm::balance_reason_key(v->cpu_step_reason) == "balance.reason.too_few_seconds");
    }
    // Bilinmeyen / elle degistirilmis kod ham gosterilmez.
    auto e = roundtrip(entry_of(r));
    e.exclusions.push_back("detail.cpu_repeat.not_a_real_code");
    const auto vt = cm::balance_view_from_history(e);
    EF_CHECK(vt && vt->cpu_repeat_reason == "unknown");
    // Eski kayit: detail anahtari yok -> bos, genel neden ayni.
    auto old = roundtrip(entry_of(r));
    old.exclusions.erase(std::remove(old.exclusions.begin(), old.exclusions.end(), "detail.cpu_step.too_few_seconds"), old.exclusions.end());
    const auto vo = cm::balance_view_from_history(old);
    EF_CHECK(vo && vo->cpu_step_reason.empty() && vo->global_reasons.size() == 1);
    // Tekrar adimi gecersizse ayri anahtar.
    auto in2 = mixed_inputs();
    in2.cpu_repeat = dg::summarize_step(std::vector<double>(500, 10.0), {}, 8.0, 0, false);
    const auto v2 = cm::balance_view_from_history(roundtrip(entry_of(dg::evaluate_balance(in2))));
    EF_CHECK(v2 && v2->global_reasons.size() == 1 && v2->global_reasons[0] == "cpu_repeat_invalid" && v2->cpu_repeat_reason == "too_few_seconds" &&
              v2->cpu_step_reason.empty());
}

EF_TEST(balance_view_rejects_foreign_and_tampered_records) {
    const auto good = entry_of(dg::evaluate_balance(mixed_inputs()));
    EF_CHECK(cm::balance_view_from_history(good).has_value());
    auto other_source = good;
    other_source.source = "builtin";
    EF_CHECK(!cm::balance_view_from_history(other_source));
    auto other_protocol = good;
    other_protocol.protocol_version = "balance_v2";
    EF_CHECK(!cm::balance_view_from_history(other_protocol));  // bilinmeyen protokol yorumlanmaz
    // Yarida kesilen kosu: hukum kodu yok -> gorunum yok (kart cizilmez, Panel basligi degismez).
    const auto interrupted = cm::interrupted_entry({"balance", 1793600000000});
    EF_CHECK(interrupted.source == "balance" && !interrupted.run_valid && !cm::balance_view_from_history(interrupted));
    // Elle degistirilmis kayit: bilinmeyen neden ham gosterilmez; gecersiz hukum kodu / FPS / pay yok sayilir.
    auto tampered = good;
    tampered.metrics["balance.verdict_1080"] = 0;  // Belirlenemedi, ama neden yok
    tampered.metrics["balance.verdict_1440"] = 9;  // gecersiz kod
    tampered.metrics["balance.gpu_fps_2160"] = -5;
    tampered.metrics["balance.cpu_sim_share"] = 4.0;
    tampered.exclusions = {"balance.row_2160.<script>", "balance.row_9999.drift", "balance.note.not_a_note", "balance.row_1440"};
    const auto v = cm::balance_view_from_history(tampered);
    EF_CHECK(v.has_value());
    if (v) {
        EF_CHECK(v->rows[0].verdict == dg::BalanceVerdict::Undetermined && v->rows[0].reason == cm::kBalanceUnknownReason);
        EF_CHECK(v->rows[1].verdict == dg::BalanceVerdict::Undetermined && v->rows[1].reason == cm::kBalanceUnknownReason);
        EF_CHECK(v->rows[2].verdict == dg::BalanceVerdict::GpuLimited && v->rows[2].reason.empty() && !v->rows[2].gpu_fps);
        EF_CHECK(!v->cpu_sim_share && !v->cpu_draw_share && v->notes.empty());
    }
    // Genel neden varsa kayittaki hukum kodu ne olursa olsun hukum gosterilmez.
    auto forced = good;
    forced.exclusions = {"balance.global.vendor_throttle"};
    const auto vf = cm::balance_view_from_history(forced);
    EF_CHECK(vf.has_value());
    if (vf)
        for (const auto& row : vf->rows) EF_CHECK(row.verdict == dg::BalanceVerdict::Undetermined && row.reason == "vendor_throttle");
}

EF_TEST(balance_text_keys_are_cataloged) {
    using dg::BalanceVerdict;
    for (const auto v : {BalanceVerdict::Undetermined, BalanceVerdict::CpuLimited, BalanceVerdict::Balanced, BalanceVerdict::GpuLimited,
                         BalanceVerdict::Unmeasurable}) {
        EF_CHECK(i18n::id_of(cm::balance_verdict_key(v)).has_value());
        EF_CHECK(i18n::id_of(cm::balance_short_key(v)).has_value());
    }
    for (std::size_t i = 0; i < dg::kBalanceRows; ++i) EF_CHECK(i18n::id_of(cm::balance_row_key(i)).has_value());
    EF_CHECK(cm::balance_row_key(dg::kBalanceRows).empty());
    // Kuralin uretebilecegi her neden ve notun kendi metni vardir (genel "unknown" metnine dusmez).
    namespace br = dg::balance_reason;
    for (const auto code : {br::kTooFewFrames, br::kTooFewSeconds, br::kUnstable, br::kTimestampDisjoint, br::kVramBudget, br::kTargetFailed,
                            br::kTimeout, br::kNotRun, br::kOnBattery, br::kBatterySaver, br::kPowerSaverPlan, br::kEfficiencyOverlay, br::kVendorThrottle, br::kVendorLimitUnverified,
                            br::kBackgroundCpu, br::kBackgroundGpu, br::kCpuStepInvalid, br::kCpuRepeatInvalid, br::kGpuStepInvalid, br::kCoupledStepInvalid,
                            br::kGpuRepeatInvalid, br::kDrift, br::kCpuLowerBound, br::kGpuLowerBound, br::kCoupledMismatch,
                            br::kCoupledBusyMismatch, br::kCoupledBusyUnknown}) {
        const auto key = cm::balance_reason_key(code);
        EF_CHECK(key == "balance.reason." + std::string(code));
        EF_CHECK(i18n::id_of(key).has_value());
    }
    EF_CHECK(cm::balance_reason_key("no_such_reason") == "balance.reason.unknown");
    EF_CHECK(cm::balance_reason_key("Bad Code!") == "balance.reason.unknown");
    EF_CHECK(i18n::id_of("balance.reason.unknown").has_value());
    namespace bn = dg::balance_note;
    for (const auto code : {bn::kCpuSpeedDrop, bn::kCpuFirmwareLimitLogged, bn::kVendorLimitFlag, bn::kBackgroundCpuUnknown, bn::kBackgroundGpuUnknown, bn::kCpuDrift,
                            bn::kGpuDrift}) {
        const auto key = cm::balance_note_key(code);
        EF_CHECK(!key.empty() && i18n::id_of(key).has_value());
    }
    EF_CHECK(cm::balance_note_key("nope").empty());
    for (const char* k : {"balance.card.title", "balance.card.caption", "balance.card.cpu_bar", "balance.card.gpu_bar", "balance.card.fps",
                          "balance.card.fps_at_least", "balance.card.fps_at_least_acc", "balance.card.row_acc", "balance.card.global",
                          "balance.card.scope_note", "balance.card.tip_label", "balance.card.tip_cpu", "balance.card.tip_gpu",
                          "balance.card.calm_hint", "balance.card.adapter", "balance.card.adapter_unknown", "balance.card.cpu_split", "balance.card.not_measured",
                          "balance.summary.row", "balance.summary.suffix", "balance.summary.lead", "measure.stage_balance_prepare",
                          "bench.reason.open_failed", "bench.reason.step_failed", "comparison.preview_mismatch"})
        EF_CHECK(i18n::id_of(k).has_value());
}

// Kosu hata ile bittiginde sonuc yazilmaz: hata nedeni "o ana kadarki olcum gosteriliyor" diyen genel "timeout"
// metnine dusmez; her kosucu hatasinin katalogda kendi metni vardir.
EF_TEST(balance_fail_reason_maps_runner_errors_to_cataloged_text) {
    EF_CHECK(cm::balance_fail_reason("") == "step_failed");
    EF_CHECK(cm::balance_fail_reason("timeout") == "step_timeout");
    for (const char* e : {"no_hardware_adapter", "open_failed", "device_removed", "gpu_timeout", "sim_checksum", "sim_pool"}) EF_CHECK(cm::balance_fail_reason(e) == e);
    for (const char* e : {"", "timeout", "no_hardware_adapter", "open_failed", "device_removed", "gpu_timeout", "sim_checksum", "sim_pool"})
        EF_CHECK(i18n::id_of("bench.reason." + cm::balance_fail_reason(e)).has_value());
    const auto cat = i18n::Catalog::embedded(i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    if (cat.ok()) {
        const auto id = i18n::id_of("bench.reason.step_timeout");
        EF_CHECK(id.has_value() && std::string(cat.value().get(*id)).find("o ana kadarki") == std::string::npos);
    }
}

EF_TEST(balance_stage_position_is_step_of_nine) {
    static_assert(cm::kBalanceStages == 9 && cm::kBuiltinStages == 5);
    EF_CHECK(cm::balance_stage_key(0).empty() && cm::balance_stage_key(10).empty());
    for (std::uint32_t code = 1; code <= 9; ++code) {
        const auto key = cm::balance_stage_key(code);
        EF_CHECK(!key.empty() && i18n::id_of(key).has_value());
        const auto pos = cm::stage_position(key);
        EF_CHECK(pos && pos->first == static_cast<int>(code) && pos->second == 9);
    }
    EF_CHECK(cm::balance_stage_key(1) == "measure.stage_balance_s1" && cm::balance_stage_key(5) == "measure.stage_balance_k1" &&
              cm::balance_stage_key(9) == "measure.stage_balance_t2");
    // Genel test asamalari degismez; hazirlik asamasi sira tasimaz.
    const auto cpu = cm::stage_position("measure.stage_cpu");
    EF_CHECK(cpu && cpu->first == 2 && cpu->second == 5);
    EF_CHECK(!cm::stage_position("measure.stage_balance_prepare"));
    cm::MeasureState s;
    s.phase = cm::TestPhase::Running;
    s.stage_key = "measure.stage_balance_k1";
    const auto f = cm::stage_fraction(s);
    EF_CHECK(f && *f > 0.444 && *f < 0.445);  // dort adim tamamlandi: 4/9
    // Kapsam varsayilani Genel test; baslatma kurali kapsamdan bagimsizdir (kaynak yine Uygulama Testi).
    cm::MeasureState idle;
    EF_CHECK(idle.scope == cm::BuiltinScope::General);
    idle.builtin_available = true;
    idle.scope = cm::BuiltinScope::Balance;
    EF_CHECK(cm::can_start(idle));
    idle.gate_blocked = true;
    EF_CHECK(!cm::can_start(idle));  // oyun kapisi denge testini de engeller
}

EF_TEST(balance_comparison_needs_same_protocol_scene_and_preview) {
    const auto r = dg::evaluate_balance(mixed_inputs());
    const auto a = entry_of(r), b = entry_of(r);
    auto c = ci::compare_compatibility(ci::comparison_inputs(a), ci::comparison_inputs(b));
    EF_CHECK(c.available && c.comparable && c.differing.empty());
    EF_CHECK(ci::metric_comparable(c, a, b, "balance.cpu_fps").allowed);
    // Canli test ekrani acik / kapali kosular karsilastirilmaz.
    const auto on = entry_of(r, true, "on");
    c = ci::compare_compatibility(ci::comparison_inputs(a), ci::comparison_inputs(on));
    EF_CHECK(c.available && !c.comparable && c.note_key == "comparison.preview_mismatch");
    EF_CHECK(std::find(c.differing.begin(), c.differing.end(), "preview") != c.differing.end());
    EF_CHECK(!ci::metric_comparable(c, a, on, "balance.cpu_fps").allowed);
    EF_CHECK(ci::comparison_field_label("preview") == i18n::StringId::HistoryCompareCondPreview);
    EF_CHECK(std::find(std::begin(ci::kComparisonDifferingFields), std::end(ci::kComparisonDifferingFields), std::string_view("preview")) !=
              std::end(ci::kComparisonDifferingFields));
    // Farkli protokol / sahne surumu.
    auto proto = b;
    proto.protocol_version = "balance_v2";
    c = ci::compare_compatibility(ci::comparison_inputs(a), ci::comparison_inputs(proto));
    EF_CHECK(!c.comparable && c.note_key == "comparison.protocol_mismatch");
    auto scene = b;
    scene.scene_version = "arena_v2";
    c = ci::compare_compatibility(ci::comparison_inputs(a), ci::comparison_inputs(scene));
    EF_CHECK(!c.comparable && c.note_key == "comparison.scene_mismatch");
    // Karistirici yuzunden gecersiz kosu fark uretmez.
    auto in = mixed_inputs();
    in.confounders.battery_saver = true;
    const auto invalid = entry_of(dg::evaluate_balance(in));
    c = ci::compare_compatibility(ci::comparison_inputs(a), ci::comparison_inputs(invalid));
    EF_CHECK(!c.comparable && c.note_key == "comparison.invalid_run");
    // Denge kaydi "builtin" kaydiyla hic eslesmez (ayri kaynak).
    auto builtin = a;
    builtin.source = "builtin";
    c = ci::compare_compatibility(ci::comparison_inputs(builtin), ci::comparison_inputs(a));
    EF_CHECK(!c.available);
    // Mevcut "builtin" kayitlari "preview" kosulu tasimaz: yeni alan onlarin karsilastirmasini degistirmez.
    ci::HistoryEntry x;
    x.source = "builtin";
    x.fingerprint = "fp";
    x.app_version = "0.1.0";
    x.scene_version = "scene_v3";
    x.protocol_version = std::string(ci::kBuiltinProtocolVersion);
    x.run_valid = true;
    x.metrics["scene_avg_fps"] = 100;
    auto y = x;
    y.metrics["scene_avg_fps"] = 110;
    c = ci::compare_compatibility(ci::comparison_inputs(x), ci::comparison_inputs(y));
    EF_CHECK(c.available && c.comparable && ci::metric_comparable(c, x, y, "scene_avg_fps").allowed);
}

EF_TEST(balance_run_marker_source_is_accepted) {
    const auto m = cm::decode_run_marker(cm::encode_run_marker({"balance", 42}));
    EF_CHECK(m && m->source == "balance" && m->started_unix_ms == 42);
    EF_CHECK(!cm::decode_run_marker(cm::encode_run_marker({"other", 42})));
}

// ---- D6: uyum denetimleri ve kosu kosullari ----

namespace {
dg::PcieLoadReading link_reading(std::uint32_t w, std::uint32_t wmax, std::uint32_t g, std::uint32_t gmax, std::optional<double> load = 97.0) {
    dg::PcieLoadReading r;
    r.cur_width = w, r.max_width = wmax, r.cur_gen = g, r.max_gen = gmax;
    r.slot_max_width = wmax, r.slot_max_gen = gmax;
    r.gpu_load_pct = load;
    return r;
}
}  // namespace

EF_TEST(balance_check_metrics_write_only_measured_values) {
    // Hicbir sey okunamadi: yalniz tekrar sayisi (kosucunun kendi sayaci) yazilir; 0 uydurulmaz.
    std::map<std::string, double> m;
    cm::add_balance_check_metrics(m, cm::BalanceRunChecks{});
    EF_CHECK(m.size() == 1 && m.at("balance.retries") == 0.0);
    // Tam okuma.
    cm::BalanceRunChecks c;
    c.pcie = link_reading(8, 16, 3, 4);
    c.cpu_speed.measured = true;
    c.cpu_speed.first_pct = 121.5, c.cpu_speed.last_pct = 99.0, c.cpu_speed.min_limit_pct = 100.0;
    c.cpu_firmware_limit_events = 0;
    c.other_gpu_pct = 3.5;
    c.retries = 2;
    m.clear();
    cm::add_balance_check_metrics(m, c);
    for (const auto& [k, v] : m) EF_CHECK(ci::safe_metric_key(k));
    EF_CHECK(m.at("balance.pcie_width") == 8.0 && m.at("balance.pcie_width_max") == 16.0 && m.at("balance.pcie_gen") == 3.0 &&
              m.at("balance.pcie_gen_max") == 4.0 && m.at("balance.pcie_slot_width_max") == 16.0 && m.at("balance.pcie_load_pct") == 97.0);
    EF_CHECK(m.at("balance.cpu_perf_first_pct") == 121.5 && m.at("balance.cpu_perf_last_pct") == 99.0 && m.at("balance.cpu_perf_limit_min_pct") == 100.0);
    EF_CHECK(m.at("balance.cpu_fw_limit_events") == 0.0 && m.at("balance.other_gpu_pct") == 3.5 && m.at("balance.retries") == 2.0);
    // Link bostayken (yuk < esik) ya da yuk bilinmeden okunduysa "yuk altinda" degildir: link anahtarlari YAZILMAZ.
    for (const auto& load : {std::optional<double>(40.0), std::optional<double>()}) {
        c.pcie = link_reading(8, 16, 3, 4, load);
        m.clear();
        cm::add_balance_check_metrics(m, c);
        EF_CHECK(m.count("balance.pcie_width") == 0 && m.count("balance.pcie_gen") == 0 && m.count("balance.pcie_load_pct") == 0);
        EF_CHECK(!cm::balance_pcie_from_metrics(m));
    }
    // Sonlu olmayan deger yazilmaz.
    c.other_gpu_pct = std::numeric_limits<double>::quiet_NaN();
    m.clear();
    cm::add_balance_check_metrics(m, c);
    EF_CHECK(m.count("balance.other_gpu_pct") == 0);
}

EF_TEST(balance_check_conditions_record_retries_and_background_gpu) {
    cm::BalanceRunChecks c;
    auto k = cm::balance_check_conditions(c);
    EF_CHECK(k.size() == 2 && k.at("retries") == "0" && k.at("other_gpu") == "unknown");
    c.retries = 2;
    c.other_gpu_pct = 9.9;
    k = cm::balance_check_conditions(c);
    EF_CHECK(k.at("retries") == "2" && k.at("other_gpu") == "low");
    for (const auto& [key, value] : k) EF_CHECK(ci::allowed_condition_key(key));
    // Kosullar kayitla birlikte diske gidip geri gelir.
    auto e = entry_of(dg::evaluate_balance(mixed_inputs()));
    for (const auto& [key, value] : k) e.conditions[key] = value;
    const auto back = roundtrip(e);
    EF_CHECK(back.conditions.count("retries") == 1 && back.conditions.at("retries") == "2" && back.conditions.at("other_gpu") == "low");
    c.other_gpu_pct = 10.0;  // kuralin esigi (kBalanceOtherGpuMaxPct) ile ayni sinir
    EF_CHECK(cm::balance_check_conditions(c).at("other_gpu") == "high");
    c.other_gpu_pct = std::numeric_limits<double>::infinity();
    EF_CHECK(cm::balance_check_conditions(c).at("other_gpu") == "unknown");
}

EF_TEST(balance_pcie_under_load_roundtrips_and_shows_only_below_maximum) {
    // x8 / x16 yuk altinda: kayda yazilir, geri okunur, kartta dilden bagimsiz link metni olur.
    cm::BalanceRunChecks c;
    c.pcie = link_reading(8, 16, 3, 4);
    auto e = entry_of(dg::evaluate_balance(mixed_inputs()));
    cm::add_balance_check_metrics(e.metrics, c);
    auto back = roundtrip(e);
    const auto p = cm::balance_pcie_from_metrics(back.metrics);
    EF_CHECK(p && p->cur_width == 8u && p->max_width == 16u && p->cur_gen == 3u && p->max_gen == 4u && p->slot_max_width == 16u &&
              p->slot_max_gen == 4u && p->gpu_load_pct == 97.0);
    if (p) EF_CHECK(dg::evaluate_pcie_under_load(*p).reduced());
    auto v = cm::balance_view_from_history(back);
    EF_CHECK(v && v->pcie_link == "x8 / x16, Gen 3 / Gen 4");
    // Azamide: okuma kayitta durur ama kartta not yoktur.
    c.pcie = link_reading(16, 16, 5, 5);
    e = entry_of(dg::evaluate_balance(mixed_inputs()));
    cm::add_balance_check_metrics(e.metrics, c);
    back = roundtrip(e);
    EF_CHECK(cm::balance_pcie_from_metrics(back.metrics).has_value());
    v = cm::balance_view_from_history(back);
    EF_CHECK(v && v->pcie_link.empty());
    // Okuma yok: gorunumde de yok.
    v = cm::balance_view_from_history(roundtrip(entry_of(dg::evaluate_balance(mixed_inputs()))));
    EF_CHECK(v && v->pcie_link.empty());
    EF_CHECK(!cm::balance_pcie_from_metrics({}));
    // Elle degistirilmis kayit: aralik disi / kesirli deger okunmamis sayilir; yuk kaniti olmayan link "yuk altinda" gosterilmez.
    const std::map<std::string, double> bad{{"balance.pcie_width", 64.0}, {"balance.pcie_width_max", 16.0}, {"balance.pcie_gen", 2.5},
                                            {"balance.pcie_gen_max", 4.0}};
    EF_CHECK(!cm::balance_pcie_from_metrics(bad));
    auto tampered = entry_of(dg::evaluate_balance(mixed_inputs()));
    tampered.metrics["balance.pcie_width"] = 8, tampered.metrics["balance.pcie_width_max"] = 16;
    tampered.metrics["balance.pcie_gen"] = 4, tampered.metrics["balance.pcie_gen_max"] = 4;  // balance.pcie_load_pct yok
    v = cm::balance_view_from_history(tampered);
    EF_CHECK(v && v->pcie_link.empty());
    tampered.metrics["balance.pcie_load_pct"] = 250.0;  // aralik disi yuk: kanit sayilmaz
    v = cm::balance_view_from_history(tampered);
    EF_CHECK(v && v->pcie_link.empty());
    EF_CHECK(i18n::id_of("balance.card.pcie_load").has_value());
}

// Yuva azamisi okunamadiginda (gercek cihaz: AMD kok portu) olcum kayda girer ve kartta gorunur; kural hukmu uretilmez.
EF_TEST(balance_pcie_under_load_with_unread_slot_is_recorded_as_measured_only) {
    cm::BalanceRunChecks c;
    c.pcie = link_reading(16, 16, 4, 5);
    c.pcie->slot_max_width.reset(), c.pcie->slot_max_gen.reset();
    auto e = entry_of(dg::evaluate_balance(mixed_inputs()));
    cm::add_balance_check_metrics(e.metrics, c);
    EF_CHECK(e.metrics.at("balance.pcie_gen") == 4.0 && e.metrics.at("balance.pcie_gen_max") == 5.0 && e.metrics.at("balance.pcie_load_pct") == 97.0);
    EF_CHECK(e.metrics.count("balance.pcie_slot_width_max") == 0 && e.metrics.count("balance.pcie_slot_gen_max") == 0);  // okunmayan yazilmaz
    const auto back = roundtrip(e);
    const auto p = cm::balance_pcie_from_metrics(back.metrics);
    EF_CHECK(p && !p->slot_max_width && !p->slot_max_gen);
    if (p) {
        const auto f = dg::evaluate_pcie_under_load(*p);
        EF_CHECK(f.measured_under_load && f.below_max() && !f.reduced());  // olcum var, "dusuk link" hukmu yok
    }
    const auto v = cm::balance_view_from_history(back);
    EF_CHECK(v && v->pcie_link == "x16 / x16, Gen 4 / Gen 5");
}

// C2: kayittaki link okumasi yalniz kayit bugunku ekran kartiyla alindiysa bilesen raporuna girer.
EF_TEST(balance_pcie_record_is_used_only_for_the_same_graphics_card) {
    cm::BalanceRunChecks c;
    c.pcie = link_reading(8, 16, 4, 4);
    auto e = entry_of(dg::evaluate_balance(mixed_inputs()));
    cm::add_balance_check_metrics(e.metrics, c);
    e.conditions["gpu"] = "NVIDIA GeForce RTX 5080";
    const auto rec = cm::balance_pcie_record(roundtrip(e));
    EF_CHECK(rec.reading && rec.reading->cur_width == 8u && rec.gpu == "NVIDIA GeForce RTX 5080");
    // Ayni kart: okuma kullanilir.
    const auto same = cm::balance_pcie_for_gpu(rec, "NVIDIA GeForce RTX 5080");
    EF_CHECK(same && same->cur_width == 8u && same->max_width == 16u);
    // Kart degisti: eski kartin linki yeni karta yazilmaz.
    EF_CHECK(!cm::balance_pcie_for_gpu(rec, "AMD Radeon RX 9070 XT"));
    // Bugunku kart henuz bilinmiyor (acilista envanter okunmadan): kullanilmaz; envanter gelince yeniden sorulur.
    EF_CHECK(!cm::balance_pcie_for_gpu(rec, ""));
    // Kosul degeri en cok 160 bayt saklanir: uzun ad ilk 160 baytiyla karsilastirilir.
    const std::string long_name(200, 'g');
    cm::BalancePcieRecord long_rec = rec;
    long_rec.gpu = long_name.substr(0, cm::kConditionCompareBytes);
    EF_CHECK(cm::balance_pcie_for_gpu(long_rec, long_name).has_value());
    EF_CHECK(!cm::balance_pcie_for_gpu(long_rec, std::string(200, 'h')));
    // Kayitta "gpu" kosulu yok (kosu envanter okunmadan yapildi): karsilastirilacak kanit yok, okuma oldugu gibi.
    e.conditions.erase("gpu");
    const auto bare = cm::balance_pcie_record(e);
    EF_CHECK(bare.gpu.empty() && cm::balance_pcie_for_gpu(bare, "").has_value() && cm::balance_pcie_for_gpu(bare, "AMD Radeon RX 9070 XT").has_value());
    // Okuma yok: hicbir durumda deger uretilmez.
    const auto none = cm::balance_pcie_record(entry_of(dg::evaluate_balance(mixed_inputs())));
    EF_CHECK(!none.reading && !cm::balance_pcie_for_gpu(none, "NVIDIA GeForce RTX 5080") && !cm::balance_pcie_for_gpu(cm::BalancePcieRecord{}, ""));
}

EF_TEST(balance_view_carries_background_gpu_reason_and_notes) {
    auto in = mixed_inputs();
    in.confounders.other_gpu_pct = 48.0;
    const auto r = dg::evaluate_balance(in);
    EF_CHECK(!cm::balance_run_valid(r) && has(cm::balance_exclusions(r), "balance.global.background_gpu"));
    auto v = cm::balance_view_from_history(roundtrip(entry_of(r)));
    EF_CHECK(v && v->global_reasons.size() == 1 && v->global_reasons[0] == "background_gpu");
    if (v)
        for (const auto& row : v->rows) EF_CHECK(row.verdict == dg::BalanceVerdict::Undetermined && row.reason == "background_gpu");
    // Okunamadi: hukum kalir, not tasinir. CPU hiz dususu + olay kaydi notlari da tasinir.
    in = mixed_inputs();
    in.confounders.other_gpu_pct.reset();
    in.cpu_speed_drop = true;
    in.cpu_firmware_limit_logged = true;
    const auto r2 = dg::evaluate_balance(in);
    EF_CHECK(cm::balance_run_valid(r2));
    v = cm::balance_view_from_history(roundtrip(entry_of(r2)));
    EF_CHECK(v && has(v->notes, "background_gpu_unknown") && has(v->notes, "cpu_speed_drop") && has(v->notes, "cpu_firmware_limit_logged"));
    if (v) EF_CHECK(v->rows[0].verdict == dg::BalanceVerdict::CpuLimited && v->global_reasons.empty());
}

// P3: kart "Bu PC'de olculemedi" gosteriyor mu (Raporu kaydet dugmesinin kosulu) + yeni metin anahtarlari katalogda.
EF_TEST(balance_view_unmeasured_and_report_keys) {
    cm::BalanceView v;
    for (auto& r : v.rows) r.verdict = dg::BalanceVerdict::Balanced;
    EF_CHECK(!cm::balance_view_unmeasured(v));
    v.rows[2].verdict = dg::BalanceVerdict::Unmeasurable;
    EF_CHECK(cm::balance_view_unmeasured(v));
    v.rows[2].verdict = dg::BalanceVerdict::Undetermined;
    EF_CHECK(cm::balance_view_unmeasured(v));
    v.rows[2].verdict = dg::BalanceVerdict::GpuLimited;
    v.global_reasons = {"cpu_step_invalid"};
    EF_CHECK(cm::balance_view_unmeasured(v));
    for (const char* k : {"balance.report.save", "balance.report.hint", "balance.report.busy", "balance.report.saved", "balance.report.failed"})
        EF_CHECK(i18n::id_of(k).has_value());
}
