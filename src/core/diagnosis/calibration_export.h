#pragma once
// DIAG-10: uygulama kosularindan diagcal_v1 kalibrasyon ornegi uretimi (saf; birim testlenir).
//  - Etiket DENEY TASARIMINDAN gelir (cagiran verir); bu modul siniflandirici ciktisindan etiket
//    turetmez/tahmin etmez.
//  - Zorunlu olmayan deger (p99, gpu_busy_ms, kullanim yuzdeleri) olculmediyse yazilmaz; 0 uydurulmaz.
//  - gpu_busy_ms kare olcekli GPU zaman damgasidir; kendi isimizin UST SINIRI (yabanci GPU isi dahil).
//  - Kisi, kullanici/oyun/makine adi veya dosya yolu tasinmaz; id opaktir ve cagiran uretir.

#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/diagnosis/calibration_io.h"

namespace ef::core::diagnosis {

struct SceneCalibrationSample {
    std::string id;                                    // opak, tekil (<=96), ornek "app-1762..."
    Provenance provenance = Provenance::RealHardware;  // gercek uygulama kosusu = real
    ComputeClass label = ComputeClass::Insufficient;   // deney tasarimindan
    std::optional<double> avg_fps;                     // zorunlu, > 0
    std::optional<double> frames;                      // zorunlu, >= kMinIntervals
    std::optional<double> p99_ms;                      // olculduyse
    std::optional<double> gpu_busy_ms;                 // olculduyse (> 0); yoksa yazilmaz
    std::optional<double> gpu_engine_3d_pct;           // coznurlugu yeterli (RootCandidate) ise
    std::optional<double> cpu_max_core_pct;
    std::optional<double> cpu_main_thread_pct;
    // EF-X20 (bos/Unspecified ise v1 ornegi uretilir). Aile cikarilmaz/tahmin edilmez: Unknown kalir.
    std::string machine_group, run_group;
    // EF-X20 dogrulama bulgusu 3: operatorun ACIKCA verdigi opak ozet (EF_CALIB_MACHINE_DIGEST; kucuk harf
    // hex 8-64). Uygulama donanim parmak izi TOPLAMAZ/hesaplamaz. Bos ise ornek yazilir ama kapiya uygun degildir.
    std::string machine_digest;
    LabelMethod label_method = LabelMethod::Unspecified;
    CalibrationLabel fine_label = CalibrationLabel::Legacy;
    DeviceFamily family;
};

// Gecersiz girdi (bos/asiri uzun id, avg_fps yok, frames < kMinIntervals, aralik disi deger) hata
// dondurur; sessizce ornek atlanmaz. Basarida siniflandiricinin kullandigi tam LabeledWindow kurulur.
contracts::Result<LabeledWindow> make_labeled_window(const SceneCalibrationSample& s);

// Var olan belgeye ekler: id tekrari ve kMaxCalibrationSamples siniri reddedilir.
contracts::Result<CalibrationDocument> append_sample(CalibrationDocument doc, const LabeledWindow& w);

// Metin tabanli ekleme: mevcut metin bossa yeni belge kurulur; doluysa ONCE dogrulanir (bozuk dosya
// sessizce ezilmez), sonra ornek eklenip ayni semayla yazilir. Cikti parse_calibration'dan gecmelidir.
contracts::Result<std::string> append_sample_json(std::string_view existing, const SceneCalibrationSample& s);

// Uygulama kosusunun kalibrasyon disa aktarimina girip giremeyecegi (saf): yalniz sahnesi olculmus, iptal
// edilmemis kosu.
struct CalibrationRunConditions {
    bool scene_measured = false;
    bool cancelled = false;
};
constexpr bool calibration_export_eligible(const CalibrationRunConditions& c) noexcept {
    return c.scene_measured && !c.cancelled;
}

}  // namespace ef::core::diagnosis
