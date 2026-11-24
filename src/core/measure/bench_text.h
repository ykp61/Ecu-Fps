#pragma once
// UI denetimi B (Test merkezi dürüstlük/geri bildirim): Uygulama Testi sonucunun kullanıcı metni kuralları (saf;
// birim testlenir). Koşucu/depolama/sahne/bellek ASCII neden kodları üretir; UI bunları ASLA ham göstermez:
//  - Tam anahtar "bench.reason.<kod>" katalogda varsa o,
//  - yoksa önek ailesi (ör. "io_error_<n>" -> "bench.reason.io_error", {n} = sayısal sonek),
//  - yoksa genel yerelleştirilmiş metin "bench.reason.unknown".
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/measure.h"
#include "contracts/models.h"

namespace ef::core::measure {

struct ReasonText {
    std::string key;  // katalog anahtarı (her zaman katalogda var)
    std::string n;    // aile {n} değeri (yalnız rakam; boş = yer tutucu yok)
};
ReasonText reason_text(std::string_view code);

// Önek aileleri (birim testi kaynak taramasında da kullanılır).
struct ReasonFamily {
    std::string_view prefix;  // "io_error_"
    std::string_view key;     // "bench.reason.io_error"
    bool numeric;             // sonek {n} olarak gösterilir (yalnız rakamsa)
};
const std::vector<ReasonFamily>& reason_families();

// M10: alt test durumunun anlam tonu (durum noktası rengi).
enum class StatusTone { Success, Warning, Error, Secondary };
StatusTone status_tone(std::string_view status) noexcept;
// "bench.<id>.<status>" kanıt anahtarının durum soneki ("bench.cpu.partial" -> "partial"); değilse boş.
std::string_view evidence_status(std::string_view key) noexcept;

// M02: Uygulama Testi sonucu teşhis değildir; başlık alt test durumlarından türetilir (güven satırı yok).
struct SubtestOutcome {
    std::string id, status, reason;
};
struct BuiltinSummary {
    std::string headline_key;                 // bench.result.completed | partial | none
    std::map<std::string, std::string> args;  // partial: {"done","total"}
    int done = 0, total = 0;                  // kullanıcının seçmediği (no_volume_selected) bölüm sayılmaz
    bool any_data = false;                    // en az bir bölüm tamamlandı/kısmi veri üretti
    std::string fail_reason;                  // hiç veri yoksa ilk başarısız bölümün nedeni
};
BuiltinSummary builtin_summary(const std::vector<SubtestOutcome>& subtests);
// Geçmiş kaydı (yalnız "id.status" dışlamaları bilinir): tam/eksik başlığı.
std::string builtin_history_headline(const std::vector<std::string>& exclusions);

// M03: "scene_v3 medium" gibi iç kimlik UI'da gösterilmez. Eski geçmiş başlığından kademe ("medium") çıkarılır;
// iç kimlik değilse boş.
std::string scene_tier_from_title(std::string_view title);
// Kademe metni anahtarı ("bench.tier.medium"); bilinmeyen kademe -> boş.
std::string tier_key(std::string_view tier);

// BM-05: sahne alt testi FPS üretmediyse eksik değerin nedeni. Desteklenmeyen sistem ile kaynak hatası
// (Failed/Cancelled) "örneklenmedi" ile karıştırılmaz.
contracts::MissingReason scene_missing_reason(std::string_view status) noexcept;

// BM-03/04: sonuç güveni yalnız sahneye bakmaz. Sahne/CPU/RAM tamamlanmış olmalı; kısmi/başarısız/atlanan
// (kullanıcının seçmediği depolama hariç) bölüm veya ölçülmüş CPU/RAM MT çekişmesi güveni düşürür.
contracts::Confidence result_confidence(const std::vector<SubtestOutcome>& subtests,
                                        const std::map<std::string, double>& metrics);

}  // namespace ef::core::measure
