#pragma once
// EF-X22: bilesen hesap/veri dogrulamasinin gecmise yazilan durum kodu (saf; kosucu yazar, kart okur).
// Gecmis metrikleri yalniz sayi tasidigi icin durum, kararli bir tamsayi kodu olarak <onek>verify_state
// anahtarina yazilir. Anahtar YOKSA dogrulama kosulmadi (veya eski kayit): "calismadi". Boylece
// "desteklenmiyor" (kod 3) ile "calismadi" (anahtar yok) birbirinden ayrilir. Kodlar degistirilmez; yeni
// durum yeni numara alir.
#include <optional>
#include <string_view>

namespace ef::core::measure {

enum class VerifyCode : int {
    Passed = 1,          // tum tekrarlar/bloklar referansla bit-ayni
    Mismatch = 2,        // hesap/veri tutarsizligi (kesin donanim karari degil)
    Unsupported = 3,     // bu cihazda calistirilamaz (GPU: donanim/D3D11 compute yok)
    SetupFailed = 4,     // kaynak ayrilamadi; dogrulama yapilmadi
    DeviceRemoved = 5,   // GPU cihazi kayboldu
    Timeout = 6,         // GPU beklemesi asildi
    ReadbackFailed = 7,  // sonuc geri okunamadi
    Incomplete = 8,      // sure/blok siniri: yeterli dogrulama yok
    Cancelled = 9,       // kullanici iptali / bekci zaman asimi
    Invalid = 10,        // yapilandirma gecersiz
    IoError = 11,        // depolama G/C hatasi
};

inline constexpr std::string_view kGpuVerifyStateMetric = "scene_gpu_verify_state";
inline constexpr std::string_view kCpuVerifyStateMetric = "cpu_verify_state";
inline constexpr std::string_view kStorageVerifyStateMetric = "storage_verify_state";

// Kartta gosterilen ozet durum.
enum class VerifyView { NotRun, Passed, Mismatch, Unsupported, Incomplete };

inline std::optional<VerifyCode> verify_code(std::optional<double> v) noexcept {
    if (!v) return std::nullopt;
    const double d = *v;
    if (!(d >= 1 && d <= 11) || d != static_cast<double>(static_cast<int>(d))) return std::nullopt;
    return static_cast<VerifyCode>(static_cast<int>(d));
}

inline VerifyView verify_view(std::optional<VerifyCode> c) noexcept {
    if (!c) return VerifyView::NotRun;
    switch (*c) {
        case VerifyCode::Passed: return VerifyView::Passed;
        case VerifyCode::Mismatch: return VerifyView::Mismatch;
        case VerifyCode::Unsupported: return VerifyView::Unsupported;
        default: return VerifyView::Incomplete;
    }
}

}  // namespace ef::core::measure
