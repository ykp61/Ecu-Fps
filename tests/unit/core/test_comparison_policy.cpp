// B3: olcum ekrani ve gecmis karsilastirmasinin ortak uyumluluk politikasi.
#include <algorithm>
#include <iterator>

#include "core/insights/comparison_policy.h"
#include "ef_test.h"

using namespace ef::core::insights;

namespace {

ComparisonInputs inputs() {
    ComparisonInputs in;
    in.source = "builtin";
    in.fingerprint = "scene_v3|medium|seed";
    in.app_version = "0.1.0";
    in.scene_version = "scene_v3";
    in.protocol_version = "bench_v1";
    in.run_valid = true;
    return in;
}

bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

}  // namespace

EF_TEST(comparison_policy_same_inputs_are_comparable) {
    const auto c = compare_compatibility(inputs(), inputs());
    EF_CHECK(c.available && c.comparable && c.differing.empty() && c.note_key.empty());
}

EF_TEST(comparison_policy_source_and_fingerprint_gate_comparison) {
    auto b = inputs();
    auto cur = inputs();
    cur.source = "in_game";
    auto c = compare_compatibility(b, cur);
    EF_CHECK(!c.available && !c.comparable && has(c.differing, "source") && c.note_key.empty());

    b = inputs();
    cur = inputs();
    cur.fingerprint.clear();  // bilinmiyor: iki bos deger eslesme degildir
    c = compare_compatibility(b, cur);
    EF_CHECK(!c.available && has(c.differing, "fingerprint"));

    b = inputs();
    b.fingerprint.clear();
    cur = inputs();
    cur.fingerprint.clear();
    c = compare_compatibility(b, cur);
    EF_CHECK(!c.available && has(c.differing, "fingerprint"));
}

EF_TEST(comparison_policy_versions_and_validity_fail_closed) {
    auto b = inputs();
    auto cur = inputs();
    cur.app_version = "0.2.0";
    auto c = compare_compatibility(b, cur);
    EF_CHECK(c.available && !c.comparable && has(c.differing, "app_version"));
    EF_CHECK(c.note_key == "comparison.version_mismatch");

    cur = inputs();
    cur.scene_version = "scene_v4";
    c = compare_compatibility(b, cur);
    EF_CHECK(c.available && !c.comparable && has(c.differing, "scene_version"));
    EF_CHECK(c.note_key == "comparison.scene_mismatch");

    cur = inputs();
    cur.protocol_version = "bench_v2";
    c = compare_compatibility(b, cur);
    EF_CHECK(c.available && !c.comparable && has(c.differing, "protocol_version"));
    EF_CHECK(c.note_key == "comparison.protocol_mismatch");

    cur = inputs();
    cur.run_valid = false;
    c = compare_compatibility(b, cur);
    EF_CHECK(c.available && !c.comparable && has(c.differing, "run_valid"));
    EF_CHECK(c.note_key == "comparison.invalid_run");

    // Bilinmeyen surum esit degildir (eski kayit fail-closed).
    cur = inputs();
    cur.scene_version.clear();
    c = compare_compatibility(b, cur);
    EF_CHECK(!c.comparable && has(c.differing, "scene_version"));

    // Birden cok uyumsuzluk: tum alanlar listelenir, ilk neden surum uyumsuzlugu.
    cur = inputs();
    cur.app_version = "0.2.0";
    cur.run_valid = false;
    c = compare_compatibility(b, cur);
    EF_CHECK(has(c.differing, "app_version") && has(c.differing, "run_valid"));
    EF_CHECK(c.note_key == "comparison.version_mismatch");
}

EF_TEST(comparison_policy_maps_history_entry_fields) {
    HistoryEntry e;
    e.source = "in_game";
    e.fingerprint = "fp";
    e.app_version = "0.1.0";
    e.scene_version = "game_capture";
    e.protocol_version = "capture_v3";
    e.run_valid = true;
    const auto in = comparison_inputs(e);
    EF_CHECK(in.source == e.source && in.fingerprint == e.fingerprint && in.app_version == e.app_version);
    EF_CHECK(in.scene_version == e.scene_version && in.protocol_version == e.protocol_version && in.run_valid);
}

static HistoryEntry x19_game(const char* build) {
    HistoryEntry e;
    e.source = "in_game";
    e.fingerprint = "fp";
    e.app_version = "1";
    e.scene_version = "s";
    e.protocol_version = "p";
    e.run_valid = true;
    if (*build) e.conditions["game_build"] = build;
    return e;
}

EF_TEST(x19_t01_same_game_other_build_gives_no_delta) {
    auto c = compare_compatibility(comparison_inputs(x19_game("100")), comparison_inputs(x19_game("101")));
    EF_CHECK(!c.comparable && c.note_key == "comparison.game_build_mismatch");
    EF_CHECK(!compare_compatibility(comparison_inputs(x19_game("")), comparison_inputs(x19_game(""))).comparable);  // bilinmeyen esit sayilmaz
    EF_CHECK(compare_compatibility(comparison_inputs(x19_game("100")), comparison_inputs(x19_game("100"))).comparable);
}

EF_TEST(x19_other_hardware_gives_no_delta_power_stays_comparable) {
    auto a = x19_game("100"), b = x19_game("100");
    a.conditions["gpu"] = "RTX 3060";
    b.conditions["gpu"] = "RTX 5080";
    EF_CHECK(compare_compatibility(comparison_inputs(a), comparison_inputs(b)).note_key == "comparison.hardware_mismatch");
    b.conditions.erase("gpu");  // tek tarafta eksik: kanit yok
    EF_CHECK(!compare_compatibility(comparison_inputs(a), comparison_inputs(b)).comparable);
    b.conditions["gpu"] = "RTX 3060";
    a.conditions["power"] = "performance_off";
    b.conditions["power"] = "performance_on";
    EF_CHECK(compare_compatibility(comparison_inputs(a), comparison_inputs(b)).comparable);  // olculen degisken budur
}

EF_TEST(x15_preview_on_run_not_compared_with_preview_off) {
    ComparisonInputs a;
    a.source = "builtin";
    a.fingerprint = "f";
    a.app_version = "1";
    a.scene_version = "s";
    a.protocol_version = "p";
    a.run_valid = true;
    ComparisonInputs b = a;
    b.live_panel = "preview_on";
    const auto c = compare_compatibility(a, b);
    EF_CHECK(c.available && !c.comparable && !c.conditions_match && c.note_key == "comparison.live_panel_mismatch");
    EF_CHECK(compare_compatibility(a, a).comparable);
    EF_CHECK(compare_compatibility(b, b).comparable);
}

// EF-X15 dogrulama bulgusu 2: compare_compatibility'nin yazabildigi her alanin kullanici etiketi vardir;
// UI ham kimligi ("live_panel") gostermez.
EF_TEST(x15_every_differing_field_has_a_label) {
    for (const auto f : kComparisonDifferingFields) EF_CHECK(comparison_field_label(f).has_value());
    EF_CHECK(comparison_field_label("live_panel") == ef::core::i18n::StringId::HistoryCompareCondLivePanel);
    EF_CHECK(!comparison_field_label("no_such_field").has_value());
    const auto known = [](const std::string& k) {
        return std::find(std::begin(kComparisonDifferingFields), std::end(kComparisonDifferingFields), k) !=
               std::end(kComparisonDifferingFields);
    };
    // Mumkun olan en cok alan farki: once kaynak/parmak izi, sonra geri kalan tum alanlar.
    ComparisonInputs empty;
    for (const auto& k : compare_compatibility(empty, empty).differing) EF_CHECK(known(k));
    ComparisonInputs a;
    a.source = "in_game";
    a.fingerprint = "f";
    a.cpu = "c1";
    ComparisonInputs b = a;
    b.cpu = "c2";
    b.live_panel = "preview_on";
    const auto c = compare_compatibility(a, b);
    EF_CHECK(c.differing.size() == 7);  // 3 surum + game_build + hardware + live_panel + run_valid
    for (const auto& k : c.differing) EF_CHECK(known(k) && comparison_field_label(k).has_value());
}
