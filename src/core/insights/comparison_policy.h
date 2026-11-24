#pragma once
// B3: iki olcum kaydinin karsilastirilabilirligi icin TEK ortak politika (saf; birim testlenir).
//  - Olcum ekrani (measure_comparison) ve gecmis karsilastirmasi (history_compatibility/compare_history)
//    ayni karari kullanir; iki kod yolu farkli kural uygulayamaz.
//  - Kaynak veya kosul izi (fingerprint) bos/farkliysa karsilastirma YOK (available=false).
//  - Uygulama, sahne ve olcum protokolu surumleri iki tarafta da dolu ve esit olmali; kosular gecerli
//    olmali. Aksi halde available=true kalir (tekil olcumler saklanir/gosterilir) ama comparable=false:
//    fark, yuzde veya kazanim URETILMEZ. Neden note_key katalog anahtariyla soylenir.
//  - Bilinmeyen surum "esit" sayilmaz; eski kayitlar bu alanlari tasimadigi icin fail-closed kalir.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/i18n/catalog.h"
#include "core/insights/history.h"

namespace ef::core::insights {

struct ComparisonInputs {
    std::string source;
    std::string fingerprint;
    std::string app_version;
    std::string scene_version;
    std::string protocol_version;
    bool run_valid = false;
    // EF-X19: farkli donanim veya oyun surumu fark uretmez. Oyunda testte oyun surumu zorunlu; bilinmiyorsa fail-closed.
    std::string game_build, cpu, gpu;
    std::string live_panel;  // EF-X15 eski kayit kosulu: "preview_on" veya bos (panel B10 ile kalkti; eski kayitlar ayrik kalir)
    std::string preview;     // Denge testi: canli test ekrani "on" / "off"; diger kaynaklarda bos
};

struct ComparisonCompatibility {
    bool available = false;   // ayni kaynak + dogrulanmis kosul izi
    bool comparable = false;  // surumler + gecerlilik de uyumlu
    std::vector<std::string> differing;  // uyusmayan alan adlari (bos = uyumlu)
    std::string note_key;     // ilk uyumsuzlugun katalog anahtari; uyumluysa bos
    bool conditions_match = false;  // kaynak + kosul + surumler uyumlu (kosu gecerliligi haric)
};

ComparisonInputs comparison_inputs(const HistoryEntry& e);

// EF-X15 dogrulama bulgusu 2: compare_compatibility'nin differing listesine yazabilecegi TUM alan adlari.
// Yeni alan eklenirse buraya ve comparison_field_label'a eklenmeli; aksi halde UI ham kimligi gosterirdi.
inline constexpr std::string_view kComparisonDifferingFields[] = {
    "source", "fingerprint", "app_version", "scene_version", "protocol_version",
    "game_build", "hardware", "live_panel", "preview", "run_valid"};
// Alan adinin kullanici metni; bilinmeyen alan nullopt (cagiran ham kimligi kullaniciya gostermemeli).
std::optional<i18n::StringId> comparison_field_label(std::string_view field) noexcept;

// base = onceki (daha eski) kayit, current = yeni kayit.
ComparisonCompatibility compare_compatibility(const ComparisonInputs& base, const ComparisonInputs& current);

// Kismen basarisiz kosu: tek run_valid bayragi alt test sonucunu gizlemez. Yerlesik testte her metrik kendi alt
// testine aittir; yalniz o alt test TAMAMLANMISSA gecerlidir. Bir alt testin Partial/Failed/Cancelled olmasi
// digerlerini gecersiz kilmaz, ama kendi metriklerini (kismi depolama dahil) fark hesabindan cikarir.
enum class MetricValidity { Valid, SubtestNotCompleted, RunInvalid, NotMeasured };
std::string_view metric_validity_key(MetricValidity v) noexcept;  // "valid" / "subtest_not_completed" / "run_invalid" / "not_measured"
// Metrik hangi yerlesik alt testten: scene/cpu/memory/storage/combined; digerleri bos.
std::string_view metric_subtest(std::string_view key) noexcept;
MetricValidity metric_validity(const HistoryEntry& e, std::string_view key);
// EF-X22: depolama dogrulama fazi kosulduysa runner bu metrigi yazar. Faz yazma butcesini paylastigi icin
// bir tarafta kosup digerinde kosmayan kayitlarin depolama metrikleri karsilastirilmaz (protocol_mismatch).
// Eski (X22 oncesi) kayitta isaret yoktur: yeni kayitla depolama farki uretilmez; diger alt testler etkilenmez.
inline constexpr std::string_view kStorageVerifyMarkerMetric = "storage_verify_blocks";
bool storage_verify_ran(const HistoryEntry& e);

struct MetricComparison {
    bool allowed = false;
    std::string note_key;  // reddedildiyse neden (comparison.*)
};
// Iki kaydin tek metrigi icin ortak karar: Olcumler sayfasi, Gecmis sayfasi ve disa aktarim bunu kullanir.
MetricComparison metric_comparable(const ComparisonCompatibility& compat, const HistoryEntry& base, const HistoryEntry& current,
                                   std::string_view key);

}  // namespace ef::core::insights
