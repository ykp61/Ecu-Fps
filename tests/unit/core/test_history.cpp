// INS-01: yerel gecmis ve disa aktarma.
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "core/insights/history.h"
#include "core/measure/view.h"
#include "ef_test.h"

using namespace ef::core::insights;

EF_TEST(history_roundtrip_bounded_and_measured_only) {
    History h;
    HistoryEntry e;
    e.when_unix_ms = 1762300000000;
    e.source = "builtin";
    e.title = "scene_v1 high";
    e.fingerprint = "scene_v1|high|17a5c0de";
    e.app_version = "0.1.0";
    e.scene_version = "scene_v3";
    e.protocol_version = "bench_v1";
    e.run_valid = true;
    e.metrics = {{"scene_avg_fps", 2664.3}, {"bad key with space", 1.0}, {"nan_metric", std::numeric_limits<double>::quiet_NaN()}};
    e.exclusions = {"storage.skipped"};
    h.add(e);
    const auto back = History::parse(h.serialize());
    EF_CHECK(back.ok() && back.value().entries().size() == 1);
    const auto& m = back.value().entries()[0].metrics;
    EF_CHECK(m.size() == 1 && std::fabs(m.at("scene_avg_fps") - 2664.3) < 1e-9);  // gecersiz anahtar ve NaN yazilmaz
    EF_CHECK(back.value().entries()[0].exclusions[0] == "storage.skipped");
    // B3: karsilastirma alanlari round-trip.
    const auto& be = back.value().entries()[0];
    EF_CHECK(be.scene_version == "scene_v3" && be.protocol_version == "bench_v1" && be.run_valid);
    // Eski kayit (alanlar yok) parse edilir ama karsilastirmada fail-closed kalir.
    const auto old = History::parse(
        R"({"schema_version":1,"entries":[{"source":"builtin","fingerprint":"scene_v1|x","app_version":"0.1.0","metrics":{"avg_fps":100.0}}]})");
    EF_CHECK(old.ok() && old.value().entries().size() == 1);
    EF_CHECK(old.value().entries()[0].scene_version.empty() && old.value().entries()[0].protocol_version.empty());
    EF_CHECK(!old.value().entries()[0].run_valid);
    History big;
    std::size_t dropped = 0;
    for (std::size_t i = 0; i < kMaxEntries + 7; ++i) {
        HistoryEntry x;
        x.when_unix_ms = static_cast<std::int64_t>(i);
        dropped += big.add(x);
    }
    EF_CHECK(big.entries().size() == kMaxEntries && dropped == 7 && big.dropped_total() == 7);
    EF_CHECK(big.entries().front().when_unix_ms == static_cast<std::int64_t>(kMaxEntries + 6));  // en yeni basta
}

namespace {
// n kayitli eski surum dosyasi (en yeni basta: when = n .. 1).
std::string file_with(std::size_t n) {
    std::string body = R"({"schema_version":1,"dropped_total":0,"entries":[)";
    for (std::size_t i = 0; i < n; ++i) {
        body += i ? "," : "";
        body += R"({"when_unix_ms":)" + std::to_string(n - i) + R"(,"source":"builtin","title":"t","fingerprint":"f","app_version":"0.1.0","metrics":{"scene_avg_fps":)" +
                std::to_string(100 + i) + "}}";
    }
    return body + "]}";
}
}  // namespace

EF_TEST(history_keeps_newest_three_on_load_and_write) {
    // Spec 2026-11-12 C: yalniz en yeni uc kayit saklanir; ilk acilista eski kayitlar uce indirilir.
    static_assert(kMaxEntries == 3);
    for (const std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{4}, std::size_t{27}}) {
        const auto h = History::parse(file_with(n));
        EF_CHECK(h.ok());
        const auto& e = h.value().entries();
        EF_CHECK(e.size() == std::min<std::size_t>(n, 3));
        EF_CHECK(h.value().trimmed_on_load() == (n > 3 ? n - 3 : 0));  // > 0 -> cagiran dosyayi yeniden yazar
        EF_CHECK(h.value().dropped_total() == (n > 3 ? n - 3 : 0));
        for (std::size_t i = 0; i < e.size(); ++i) EF_CHECK(e[i].when_unix_ms == static_cast<std::int64_t>(n - i));  // en yeniler, en yeni basta
        // Yeniden yazilan dosya sinirin icindedir: tekrar okununca kirpma yok, ayni kayitlar.
        const auto again = History::parse(h.value().serialize());
        EF_CHECK(again.ok() && again.value().trimmed_on_load() == 0 && again.value().entries().size() == e.size());
        // Dorduncu (yeni) kayit yazilinca en eski dusurulur.
        History w = h.value();
        HistoryEntry x;
        x.when_unix_ms = 1000;
        const auto dropped = w.add(x);
        EF_CHECK(w.entries().size() == std::min<std::size_t>(n + 1, 3) && w.entries().front().when_unix_ms == 1000);
        EF_CHECK(dropped == (n >= 3 ? 1u : 0u));
        if (n >= 3) EF_CHECK(w.entries().back().when_unix_ms == static_cast<std::int64_t>(n - 1));  // en eski (n-2) silindi
    }
    // Bozuk sira (elle duzenlenmis): zamana gore en yeni uc kayit kalir.
    const auto mixed = History::parse(
        R"({"schema_version":1,"entries":[{"when_unix_ms":5},{"when_unix_ms":9},{"when_unix_ms":1},{"when_unix_ms":7}]})");
    EF_CHECK(mixed.ok() && mixed.value().entries().size() == 3 && mixed.value().trimmed_on_load() == 1);
    EF_CHECK(mixed.value().entries()[0].when_unix_ms == 9 && mixed.value().entries()[1].when_unix_ms == 7 &&
              mixed.value().entries()[2].when_unix_ms == 5);
}

EF_TEST(history_comparison_needs_two_records) {
    // Karsilastirma en cok uc kayitla calisir; onceki kayit yoksa (toplam iki kayittan az) taban yoktur.
    HistoryEntry cur;
    cur.source = "builtin";
    cur.fingerprint = "f";
    std::vector<HistoryEntry> prior;
    EF_CHECK(ef::core::measure::comparison_base(prior, cur) == nullptr);  // 0 onceki kayit
    HistoryEntry other = cur;
    other.fingerprint = "g";  // farkli kosul: taban degil
    prior.push_back(other);
    EF_CHECK(ef::core::measure::comparison_base(prior, cur) == nullptr);
    HistoryEntry same = cur;
    same.when_unix_ms = 4;
    prior.push_back(same);
    HistoryEntry older = cur;
    older.when_unix_ms = 2;
    prior.push_back(older);
    const auto* base = ef::core::measure::comparison_base(prior, cur);
    EF_CHECK(base && base->when_unix_ms == 4);  // en yeni uygun kayit
    HistoryEntry no_fp = cur;
    no_fp.fingerprint.clear();
    EF_CHECK(ef::core::measure::comparison_base(prior, no_fp) == nullptr);  // kosul izi bilinmiyor
    // Uc kayitlik gecmisten dorduncu yazilinca taban hala en yeni onceki kayittir.
    History h = History::parse(file_with(3)).value();
    HistoryEntry next;
    next.source = "builtin";
    next.fingerprint = "f";
    next.when_unix_ms = 50;
    const auto* b2 = ef::core::measure::comparison_base(h.entries(), next);
    EF_CHECK(b2 && b2->when_unix_ms == 3);
    // Tek kayit (ilk test) sonrasi: taban yok.
    History one;
    EF_CHECK(ef::core::measure::comparison_base(one.entries(), next) == nullptr);
}

EF_TEST(history_corrupt_or_newer_schema_rejected) {
    EF_CHECK(!History::parse("{").ok());
    EF_CHECK(!History::parse(R"({"schema_version":99,"entries":[]})").ok());
    EF_CHECK(!History::parse(R"({"entries":[]})").ok());
    EF_CHECK(History::parse(R"({"schema_version":1,"entries":[42,"x"]})").ok());  // gecersiz ogeler atlanir
}

EF_TEST(history_export_has_no_paths_or_ids) {
    History h;
    HistoryEntry e;
    e.title = "Counter-Strike 2";
    e.metrics = {{"avg_fps", 300.0}};
    h.add(e);
    const auto x = h.export_json(1762300000000);
    EF_CHECK(x.find("ecufps_history_export") != std::string::npos);
    EF_CHECK(x.find(":\\\\") == std::string::npos && x.find("serial") == std::string::npos && x.find("uuid") == std::string::npos);
    EF_CHECK(safe_metric_key("scene_avg_fps") && !safe_metric_key("C:\\x") && !safe_metric_key(""));
}

EF_TEST(history_export_is_allow_list_only) {
    // R16: disa aktarim yalniz mevcut izin listesi alanlarini tasir; yol/seri/UUID alanlari eklenmez.
    History h;
    HistoryEntry e;
    e.when_unix_ms = 1762300000000;
    e.source = "in_game";
    e.title = "Counter-Strike 2";
    e.fingerprint = "game_v1|1920x1080|17a5";
    e.app_version = "0.1.0";
    e.metrics = {{"avg_fps", 300.0}, {"C:\\Users\\someone\\secret", 42.0}};
    e.exclusions = {"run.too_short"};
    h.add(e);
    const auto x = h.export_json(1762300000001);
    EF_CHECK(x.find("C:\\\\") == std::string::npos);  // yol benzeri metrik anahtari yazilmaz
    EF_CHECK(x.find("\"title\"") != std::string::npos && x.find("Counter-Strike 2") != std::string::npos);
    EF_CHECK(x.find("run.too_short") != std::string::npos);  // izin listesi: gecerlilik nedeni
    EF_CHECK(x.find("exported_unix_ms") != std::string::npos);
    EF_CHECK(x.find("\"scene_version\"") != std::string::npos && x.find("\"protocol_version\"") != std::string::npos);
    EF_CHECK(x.find("\"run_valid\"") != std::string::npos);  // B3: karsilastirma alanlari disa aktarimda da var
}

// P3: Denge raporu yalniz Denge kayitlarini disa aktarir (kaynak suzgeci).
EF_TEST(history_export_can_filter_by_source) {
    History h;
    HistoryEntry a, b;
    a.when_unix_ms = 1762300000000;
    a.source = "balance";
    a.title = "DengeKaydi";
    b.when_unix_ms = 1762300000100;
    b.source = "builtin";
    b.title = "GenelKayit";
    h.add(a);
    h.add(b);
    const auto only = h.export_json(1762300000200, "balance");
    EF_CHECK(only.find("DengeKaydi") != std::string::npos && only.find("GenelKayit") == std::string::npos);
    const auto all = h.export_json(1762300000200);
    EF_CHECK(all.find("DengeKaydi") != std::string::npos && all.find("GenelKayit") != std::string::npos);
}
