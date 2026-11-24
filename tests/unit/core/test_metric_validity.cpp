// 4. bolum: kismen basarisiz kosuda metrik basina gecerlilik; iki sayfa + disa aktarim ayni politikayi kullanir.
#include "core/diagnosis/components.h"
#include "core/insights/comparison_policy.h"
#include "core/insights/history_compare.h"
#include "core/measure/view.h"
#include "core/serialization/json.h"
#include "ef_test.h"

using namespace ef::core::insights;

namespace {
HistoryEntry builtin(double fps) {
    HistoryEntry e;
    e.source = "builtin";
    e.title = "scene_v3 medium";
    e.fingerprint = "fp";
    e.app_version = "0.1.0";
    e.scene_version = "scene_v3";
    e.protocol_version = "bench_v1";
    e.run_valid = true;
    e.when_unix_ms = 100;
    e.metrics["scene_avg_fps"] = fps;
    e.metrics["cpu_st_units_per_s"] = 50;
    return e;
}
const MetricChange* find(const HistoryComparison& c, const char* k) {
    for (const auto& m : c.metrics)
        if (m.key == k) return &m;
    return nullptr;
}
}  // namespace

EF_TEST(metric_validity_subtest_families) {
    EF_CHECK(metric_subtest("scene_avg_fps") == "scene" && metric_subtest("cpu_st_units_per_s") == "cpu");
    EF_CHECK(metric_subtest("mem_latency_ns") == "memory" && metric_subtest("storage_seq_mbps") == "storage");
    EF_CHECK(metric_subtest("combined_avg_fps") == "combined" && metric_subtest("avg_fps").empty());
}

EF_TEST(metric_partial_storage_excluded_others_compared) {
    auto a = builtin(100), b = builtin(110);
    a.metrics["storage_seq_read_mbps"] = 3000;
    b.metrics["storage_seq_read_mbps"] = 900;
    b.exclusions.push_back("storage.partial");  // yeni kosuda depolama yarim kaldi
    const auto c = compare_history(a, b);
    const auto* fps = find(c, "scene_avg_fps");
    const auto* st = find(c, "storage_seq_read_mbps");
    EF_CHECK(fps && fps->delta && *fps->delta == 10.0);  // tamamlanan sahne hala karsilastirilir
    EF_CHECK(st && !st->delta && st->not_comparable_key == "comparison.metric_subtest");
    EF_CHECK(st->base_value && st->current_value);  // tekil degerler saklanir
}

EF_TEST(metric_failed_scene_blocks_fps_not_cpu) {
    auto a = builtin(100), b = builtin(110);
    b.run_valid = false;
    b.exclusions.push_back("scene.failed");
    b.metrics.erase("scene_avg_fps");
    b.metrics["cpu_st_units_per_s"] = 60;
    const auto c = compare_history(a, b);
    const auto* cpu = find(c, "cpu_st_units_per_s");
    const auto* fps = find(c, "scene_avg_fps");
    EF_CHECK(cpu && cpu->delta && *cpu->delta == 10.0);
    EF_CHECK(fps && !fps->delta && fps->not_comparable_key == "comparison.metric_not_measured");
    EF_CHECK(!c.compatibility.compatible && c.compatibility.conditions_match);
    // Olcumler sayfasi ayni karari verir: FPS farki yok, neden var.
    const auto v = ef::core::measure::measure_comparison(a, b, "scene_avg_fps");
    EF_CHECK(v.available && !v.comparable && !v.delta_percent && !v.note_key.empty());
}

EF_TEST(metric_run_valid_flag_alone_does_not_validate_failed_subtest) {
    auto e = builtin(100);
    e.metrics["cpu_mt_units_per_s"] = 5;
    e.exclusions.push_back("cpu.failed");
    EF_CHECK(e.run_valid);
    EF_CHECK(metric_validity(e, "cpu_mt_units_per_s") == MetricValidity::SubtestNotCompleted);
    EF_CHECK(metric_validity(e, "scene_avg_fps") == MetricValidity::Valid);
    EF_CHECK(metric_validity(e, "yok") == MetricValidity::NotMeasured);
}

EF_TEST(metric_old_record_stays_invalid_nothing_invented) {
    auto old = builtin(100);
    old.protocol_version.clear();
    old.scene_version.clear();
    old.run_valid = false;
    EF_CHECK(metric_validity(old, "scene_avg_fps") == MetricValidity::RunInvalid);
    auto cur = builtin(110);
    const auto c = compare_history(old, cur);
    const auto* fps = find(c, "scene_avg_fps");
    EF_CHECK(fps && !fps->delta && !fps->not_comparable_key.empty());
    EF_CHECK(c.attribution == Attribution::ContextMissing);
}

EF_TEST(metric_invalid_run_without_scene_exclusion_is_invalid) {
    auto a = builtin(100), b = builtin(110);
    b.run_valid = false;  // baska nedenle gecersiz
    EF_CHECK(metric_validity(b, "cpu_st_units_per_s") == MetricValidity::RunInvalid);
    const auto c = compare_history(a, b);
    for (const auto& m : c.metrics) EF_CHECK(!m.delta);
}

EF_TEST(metric_in_game_uses_run_valid) {
    HistoryEntry e;
    e.source = "in_game";
    e.metrics["avg_fps"] = 90;
    e.run_valid = false;
    EF_CHECK(metric_validity(e, "avg_fps") == MetricValidity::RunInvalid);
    e.run_valid = true;
    EF_CHECK(metric_validity(e, "avg_fps") == MetricValidity::Valid);
}

EF_TEST(history_conditions_roundtrip_and_filter) {
    History h;
    auto e = builtin(100);
    e.conditions["gpu_driver"] = "32.0.16.2114";
    e.conditions["resolution"] = "1920x1080";
    e.conditions["serial_number"] = "GIZLI";  // izin listesi disi: kalici kayda girmez
    h.add(e);
    const auto parsed = History::parse(h.serialize());
    EF_CHECK(parsed.ok());
    const auto& c = parsed.value().entries()[0].conditions;
    EF_CHECK(c.at("gpu_driver") == "32.0.16.2114" && c.at("resolution") == "1920x1080" && c.count("serial_number") == 0);
    EF_CHECK(h.export_json(1).find("GIZLI") == std::string::npos);
}

EF_TEST(history_old_file_without_conditions_loads) {
    const std::string old = R"({"schema_version":1,"dropped_total":0,"entries":[{"when_unix_ms":5,"source":"builtin","title":"t","fingerprint":"f","app_version":"0.1.0","metrics":{"scene_avg_fps":10},"exclusions":[]}]})";
    const auto p = History::parse(old);
    EF_CHECK(p.ok() && p.value().entries().size() == 1 && p.value().entries()[0].conditions.empty());
    EF_CHECK(!p.value().entries()[0].run_valid);
}

EF_TEST(history_driver_change_is_context_not_cause) {
    auto a = builtin(100), b = builtin(110);
    a.conditions["gpu_driver"] = "1";
    b.conditions["gpu_driver"] = "2";
    const auto c = compare_history(a, b);
    EF_CHECK(c.attribution == Attribution::SingleChangeCoincides);  // "neden" degil "denk geldi"
    const auto* fps = find(c, "scene_avg_fps");
    EF_CHECK(fps && fps->delta);
}

EF_TEST(history_export_carries_same_policy) {
    History h;
    auto a = builtin(100);
    a.when_unix_ms = 1;
    auto b = builtin(110);
    b.when_unix_ms = 2;
    b.metrics["storage_x_mbps"] = 1;
    b.exclusions.push_back("storage.partial");
    h.add(a);
    h.add(b);
    const auto text = h.export_json(9);
    EF_CHECK(text.find("\"storage_x_mbps\":\"subtest_not_completed\"") != std::string::npos);
    EF_CHECK(text.find("\"compared_to_previous\"") != std::string::npos);
    EF_CHECK(text.find("\"comparable\":true") != std::string::npos);
    EF_CHECK(text.find("\"scene_avg_fps\":\"comparable\"") != std::string::npos);
    EF_CHECK(text.find("\"storage_x_mbps\":\"comparison.metric_not_measured\"") != std::string::npos ||
              text.find("\"storage_x_mbps\":\"comparison.metric_subtest\"") != std::string::npos);
}

EF_TEST(diagnosis_three_states_are_distinct) {
    namespace dg = ef::core::diagnosis;
    dg::ComponentFinding f{dg::Component::Gpu};
    f.verdict = dg::Verdict::Abstain;
    f.abstain_reason = "no_runtime_evidence";
    EF_CHECK(dg::verdict_key(f) == "diag.verdict_not_measured");
    // B10: yuk altinda olculdu ama kalibrasyon dogrulanmadi: "Olculmedi" degil, ayri "olculdu, karar yok" durumu.
    f.abstain_reason = "diagnosis_calibration_unverified";
    EF_CHECK(dg::verdict_key(f) == "diag.verdict_measured_no_verdict");
    f.abstain_reason = "mixed_evidence";
    EF_CHECK(dg::verdict_key(f) == "diag.verdict_abstain");
    f.verdict = dg::Verdict::NoIssueFound;
    f.evidence = {"runtime:not_limiting_under_load"};
    EF_CHECK(dg::verdict_key(f) == "diag.verdict_no_issue");
    f.evidence = {"speed_at_rated"};  // envanter: "bu testte" denmez
    EF_CHECK(dg::verdict_key(f) == "diag.verdict_inventory_ok");
}

// EF-X22 dogrulama bulgusu: dogrulama fazi yazma butcesini paylasir; X22 oncesi kayitla depolama yazma hizi
// ayni is yuku sayilmaz. Depolama disi alt testler etkilenmez.
EF_TEST(x22_storage_metrics_not_compared_across_verify_change) {
    EF_CHECK(kStorageVerifyMarkerMetric == "storage_verify_blocks");
    auto old = builtin(100), now = builtin(110);
    old.metrics["storage_seq_1m_qd8_write_mbps"] = 3000;
    now.metrics["storage_seq_1m_qd8_write_mbps"] = 2500;
    now.metrics["storage_verify_blocks"] = 64;
    now.metrics["storage_verify_mismatch_blocks"] = 0;
    EF_CHECK(!storage_verify_ran(old) && storage_verify_ran(now));
    const auto c = compare_history(old, now);
    const auto* wr = find(c, "storage_seq_1m_qd8_write_mbps");
    const auto* fps = find(c, "scene_avg_fps");
    EF_CHECK(wr && !wr->delta && wr->not_comparable_key == "comparison.protocol_mismatch");
    EF_CHECK(wr->base_value && wr->current_value);  // tekil degerler saklanir
    EF_CHECK(fps && fps->delta && *fps->delta == 10.0);
    const auto compat = compare_compatibility(comparison_inputs(old), comparison_inputs(now));
    EF_CHECK(!metric_comparable(compat, old, now, "storage_seq_1m_qd8_write_mbps").allowed);
    EF_CHECK(metric_comparable(compat, old, now, "cpu_st_units_per_s").allowed);
    // Iki taraf da dogrulamali: fark uretilir.
    auto now2 = now;
    now2.metrics["storage_seq_1m_qd8_write_mbps"] = 2600;
    const auto c2 = compare_history(now, now2);
    const auto* wr2 = find(c2, "storage_seq_1m_qd8_write_mbps");
    EF_CHECK(wr2 && wr2->delta && *wr2->delta == 100.0);
    // Iki taraf da dogrulamasiz (eski kayitlar): eski davranis korunur.
    auto old2 = old;
    old2.metrics["storage_seq_1m_qd8_write_mbps"] = 3100;
    // find() bir MetricChange* dondurur: karsilastirma nesnesi isaretci kullanilirken yasamali (gecici nesne olunca
    // debug CRT serbest bellegi 0xDD ile doldurur -> bos delta; release'de tesaduf eseri gecer).
    const auto c3 = compare_history(old, old2);
    const auto* wr3 = find(c3, "storage_seq_1m_qd8_write_mbps");
    EF_CHECK(wr3 && wr3->delta && *wr3->delta == 100.0);
}
