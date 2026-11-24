#pragma once
// BM-02: teshis yayin kapisi durumu (saf veri; UI metin anahtari tasir). Siniflandirici yalniz gercek
// donanim holdout dogrulamasi gecildiginde yayinlanabilir sinif uretir; sentetik fikstur gercek dogruluk
// sayilmaz. Kapi kapaliyken rapor/UI bunu acikca gosterir.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ef::core::diagnosis {

struct ReleaseStatus {
    bool ready = false;
    bool synthetic_only = true;    // holdout yalniz sentetik fiksturden olusuyor
    std::size_t real_holdout = 0;  // etiketli gercek donanim holdout ornegi
    std::size_t holdout_total = 0;
    std::string reason_key;  // "diag.gate.ready" | "diag.gate.no_real_holdout" | "diag.gate.false_positive_rate"
                             // | label_or_group_missing | machine_coverage | class_imbalance | split_leakage
                             // | device_family_uncovered (EF-X20)
};

// DIAG-06: dogrulanmis veri setinin DERLEMEYE gomulmus ozeti. Calisma aninda dosya okunmaz: kapi yalniz
// gelistiricinin veri setini degerlendirip basligi yeniden uretmesiyle acilir. Uretimde varsayilan
// "yok"tur ve sentetik ornek gercek sayilmaz.
// EF-X20: cihaz ailesi (destek kapsami). Bilinmeyen boyut -> kapsam disi (fail-closed).
enum class CpuVendor { Unknown, Intel, Amd, Other };
enum class GpuFamily { Unknown, Nvidia, Amd, Intel, Other };
enum class FormFactor { Unknown, Desktop, Laptop };
enum class StorageKind { Unknown, Ssd, Hdd };
static_assert(static_cast<int>(CpuVendor::Other) == 3 && static_cast<int>(GpuFamily::Other) == 4 &&
                  static_cast<int>(FormFactor::Laptop) == 2 && static_cast<int>(StorageKind::Hdd) == 2,
              "family_bits bit yerlesimi 3x4x2 (+2 depolama) varsayar");
struct DeviceFamily {
    CpuVendor cpu = CpuVendor::Unknown;
    GpuFamily gpu = GpuFamily::Unknown;
    FormFactor form = FormFactor::Unknown;
    StorageKind storage = StorageKind::Unknown;
};
// Bit 0..23: cpu x gpu x form birlesimi; bit 24 ssd, 25 hdd. Herhangi bir boyut Unknown ise 0.
std::uint32_t family_bits(const DeviceFamily& d) noexcept;

struct ValidatedCalibration {
    bool present = false;
    std::size_t real_holdout = 0;
    std::size_t holdout_total = 0;
    std::size_t false_positive = 0;
    std::size_t abstained = 0;
    std::size_t correct = 0;
    std::string_view dataset_sha256;
    std::string_view collected_on;  // YYYY-MM-DD (veri toplama tarihi)
    // EF-X20 (sona eklenir; konumsal baslatma korunur). real_holdout = kapiya UYGUN gercek holdout.
    std::size_t holdout_machines = 0;
    std::size_t holdout_positives = 0;  // sinirlayici etiket
    std::size_t holdout_negatives = 0;  // healthy/insufficient etiket
    std::uint32_t covered_families = 0;
};

// Ozetten kapi durumu. Kurallar evaluate() ile aynidir: en az kMinRealHoldout gercek ornek ve
// yanlis pozitif orani <= kMaxFalsePositiveRate. Ozet yoksa/gercek ornek yoksa kapi kapalidir.
ReleaseStatus release_status_from_validated(const ValidatedCalibration& v);

// Uretim ozeti (varsayilan: yok). Tanim: src/core/diagnosis/validated_calibration.h.
const ValidatedCalibration& validated_calibration() noexcept;

// EF-X20/T03: kapi aciksa bile cihaz ailesi kapsam disiysa hazir degil ("diag.gate.device_family_uncovered").
ReleaseStatus release_status_for_device(const ReleaseStatus& base, std::uint32_t covered_families, const DeviceFamily& d);
ReleaseStatus diagnosis_release_status_for(const DeviceFamily& d);

}  // namespace ef::core::diagnosis
