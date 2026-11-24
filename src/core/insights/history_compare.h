#pragma once
// R16: iki yerel olcum gecmisi kaydinin karsilastirilmasi (saf; birim testlenir).
//  - Yalniz ayni kaynak + ayni kosul izi (fingerprint) olan kayitlar karsilastirilir; degilse
//    compatibility=false ve farkli alan adlari doner ("kosullar uyusmuyor").
//  - Kosul farki update_compare'in Context modeliyle verilir: tek anahtar degisti -> "denk geldi"
//    (neden degil), birden cok anahtar -> karisik/ayristirilamaz, baglam eksik -> atif yok.
//  - Metrik farki yalniz iki kayitta da OLCULMUS deger varsa hesaplanir. Eksik metrik 0 sayilmaz;
//    mutlak fark ve (taban 0 degilse) yuzde degisim verilir.
//  - Gecerlilik/dislama farklari ayrica ve acikca listelenir.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/insights/history.h"
#include "core/insights/update_compare.h"

namespace ef::core::insights {

struct HistoryCompatibility {
    bool available = false;   // ayni kaynak + kosul izi: tekil degerler karsilastirilabilir
    bool compatible = false;  // B3: surumler + kosu gecerliligi de uyumlu (fark/yuzde uretilebilir)
    std::vector<std::string> differing;  // uyusmayan alanlar (bos veya eksik alanlar dahil)
    std::string note_key;     // ilk uyumsuzlugun katalog anahtari (uyumluysa bos)
    bool conditions_match = false;  // yalniz kosu gecerliligi eksik: tamamlanmis alt testler yine karsilastirilir
};

HistoryCompatibility history_compatibility(const HistoryEntry& base, const HistoryEntry& current);

struct MetricChange {
    std::string key;
    std::optional<double> base_value;
    std::optional<double> current_value;
    std::optional<double> delta;    // current - base; yalniz iki deger de varsa
    std::optional<double> percent;  // delta / base * 100; yalniz taban != 0 ve iki deger varsa
    std::string not_comparable_key;  // fark hesaplanmadiysa nedeni (comparison.*)
};

struct HistoryComparison {
    HistoryCompatibility compatibility;
    ContextDiff context;  // source/fingerprint disindaki kosul anahtarlari (app_version, exclusions)
    Attribution attribution = Attribution::ContextMissing;
    std::vector<MetricChange> metrics;  // yalniz izinli ve sonlu anahtarlar; anahtar sirali
    std::vector<std::string> exclusions_only_base;
    std::vector<std::string> exclusions_only_current;
};

// Karsilastirma baglami: source, fingerprint, app/scene/protokol surumu (biliniyorsa), kosu gecerliligi
// ve sirali dislamalar.
Context history_context(const HistoryEntry& e);

// base = onceki (daha eski), current = yeni kayit. Siralama cagiranin sorumlulugudur.
HistoryComparison compare_history(const HistoryEntry& base, const HistoryEntry& current);

// delta = Acik - Kapali (kayit sirasindan bagimsiz).
// EF-X19: Performans Acik/Kapali arasinda tekrarli kosu karsilastirmasi. Her kolda en az kMinCohortRuns gecerli,
// ayni kosullu kosu yoksa enough=false; fark uretilmez. Aralik sifiri iceriyorsa uncertain: iyilesti denmez.
inline constexpr std::size_t kMinCohortRuns = 3;
struct CohortComparison {
    bool applicable = false;  // secim Acik/Kapali karsilastirmasi ve metrigi var
    bool enough = false;
    std::size_t n_off = 0, n_on = 0;
    bool interleaved = false;  // kollar zamanla karisik: sira etkisi dislanabilir
    std::optional<double> delta, ci_low, ci_high;
    bool uncertain = true;
};
CohortComparison compare_cohort(const std::vector<HistoryEntry>& all, const HistoryEntry& base, const HistoryEntry& current);

}  // namespace ef::core::insights
