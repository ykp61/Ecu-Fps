#pragma once
// DIAG-05: aciklama ve guven modeli (saf; birim testlenir). Modul ciktilarini tek rapora indirger:
//  - Durum: Healthy / Limiting / Mixed / Insufficient. Calisma ani (yuk altinda) teshis yoksa genel durum
//    Healthy OLAMAZ ("veri yokken darbogaz yok denmez").
//  - Her madde destekleyen + karsit kanit kodu, guven NEDENI (metin anahtari; yuzde yok), duzeltilebilir
//    ayar grubu (bios / oyun ayari / windows) veya fiziksel sinir (donanim yukseltmesi) tasir.
//  - Parca modelinden "darbogaz puani" uretilmez.

#include <optional>
#include <string>
#include <vector>

#include "core/diagnosis/components.h"
#include "core/diagnosis/compute.h"
#include "core/diagnosis/memory_storage.h"
#include "core/diagnosis/platform.h"

namespace ef::core::diagnosis {

enum class ReportStatus { Healthy, Limiting, Mixed, Insufficient };
std::string_view to_string(ReportStatus s) noexcept;

enum class FixGroup { None, Bios, GameSettings, Windows, Hardware };

struct ReportItem {
    std::string area;              // "compute", "memory_storage", "platform", "component.ram" ...
    ReportStatus status = ReportStatus::Insufficient;
    std::vector<std::string> supporting, counter;
    std::string confidence;        // guven nedeni kodu
    FixGroup fix = FixGroup::None;
    bool physical_limit = false;   // ayarla duzelmez (donanim siniri)
};

struct DiagnosisReport {
    ReportStatus overall = ReportStatus::Insufficient;
    bool has_runtime_evidence = false;
    // BM-02: yayin kapisi durumu; kapi kapaliyken rapor bunu acikca tasir (sentetik kalibrasyon
    // gercek dogruluk sayilmaz).
    bool release_ready = false;
    std::string release_key;
    std::vector<ReportItem> items;
};

// BM-02: release kapi durumudur; hazir degilse calisma ani siniflandirma maddesi yayinlanmaz
// (Insufficient + "diagnosis_calibration_unverified" karsi kaniti).
DiagnosisReport make_report(const std::optional<ComputeVerdict>& compute, const std::optional<MemoryStorageDiagnosis>& mem,
                            const std::vector<PlatformFinding>& platform, const std::vector<ComponentFinding>& components,
                            const ReleaseStatus& release);

}  // namespace ef::core::diagnosis
