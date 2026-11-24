#pragma once
// DIAG-06/BM-02: gercek donanim kalibrasyon veri seti (diagcal_v1) okuyucu/yazici.
//  - EF-X20: diagcal_v2 (makine/kosu grubu, etiket yontemi, cihaz ailesi, ince etiket) de okunur/yazilir;
//    yalniz v1 alanli belge v1 olarak yazilir (geri uyum).
//  - Veri seti etiketli pencereler tasir; etiket kaynagi (synthetic/real) ayridir. Sentetik ornek
//    gercek dogruluk sayilmaz; yayin kapisi yalniz gercek holdout ile acilir.
//  - Tum alanlar sinirlidir; bilinmeyen alan yok sayilmaz, hata dondurur. Kare dizisi yerine ozet
//    istatistik tasinir (siniflandirici yalniz bu ozeti kullanir); boylece dosya kucuk kalir.
//  - Bu modul ag/IO yapmaz; dosya okuma cagirana aittir (arac veya test).

#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "core/diagnosis/calibration.h"

namespace ef::core::diagnosis {

inline constexpr int kCalibrationSchemaVersion = 2;
inline constexpr std::string_view kCalibrationFormat = "diagcal_v1";
inline constexpr std::string_view kCalibrationFormatV2 = "diagcal_v2";
inline constexpr std::size_t kMaxCalibrationSamples = 5000;

struct CalibrationDocument {
    std::vector<LabeledWindow> samples;
    int schema_version = 1;  // okunan/yazilacak sema (v1 korunur)
};

// JSON metninden okur. Sema surumu desteklenenden buyukse SchemaTooNew; bicim/alan hatasi ParseError
// veya OutOfRange; tekrarli id ParseError. Hicbir ornek sessizce atlanmaz.
contracts::Result<CalibrationDocument> parse_calibration(std::string_view text);

// Ayni semada yazar (test ve arac cikisi icin). NaN/sonsuz deger yazilmaz (OutOfRange).
contracts::Result<std::string> write_calibration(const CalibrationDocument& doc);

}  // namespace ef::core::diagnosis
