#pragma once
// EF-X07.01 (ADR-026 X07): CPU undervolt icin yalniz OKUNUR uygunluk + rehber + kararlilik degerlendirmesi (saf).
//  - Uygulama CPU voltaji / Curve Optimizer / PBO YAZMAZ; otomatik tuning yoktur (kAutoTuneAvailable=false). Kilit veya
//    Undervolt Protection (UVP) atlatilmaz; ozel surucu (SMU/MSR) yok.
//  - Girdi yalniz islemci saticisi + marka metni (+ biliniyorsa dizustu). Model taninmazsa "veri yetersiz" (Unknown);
//    tahmin yapilmaz.
//  - Kaynaklar (artifacts/hardware-control-boundaries.md): Intel XTU yalniz K/KF/HK/X/XE (S99, S24); UVP 12. nesil ve
//    sonrasi, varsayilan acik, kapatilmasi onerilmez, yalniz bazi kilitsiz K/KF/KS/X + Z/X yonga setinde BIOS'ta (S106);
//    XTU sunucu/Celeron/Atom desteklemez; AMD Curve Optimizer destegi model bazlidir, urun sayfasindan dogrulanir (S107).
//  - Kararlilik: tek ya da birkac hatasiz gecis "kararli" sayilmaz; en iyi sonuc "bu testlerde hata gorulmedi"dir.

#include <string>
#include <string_view>
#include <vector>

namespace ef::core::guided {

inline constexpr bool kAutoTuneAvailable = false;  // resmi yazma saglayicisi yok (BLOCKED_PROVIDER)

enum class UndervoltVerdict {
    MayBeAdjustable,  // uretici araci / BIOS yolu olabilir; ayari kullanici yapar
    Locked,           // model kilitli: ayar yok, atlatma denenmez
    NotSupported,     // uretici araci bu aileyi desteklemiyor
    Unknown,          // veri yetersiz (model/satici taninmadi)
};

struct UndervoltGuide {
    UndervoltVerdict verdict = UndervoltVerdict::Unknown;
    std::string vendor_key;           // "intel" | "amd" | ""
    std::string model;                // ayrisan model (or. "i7-13700KF", "Ultra 9 285K", "Ryzen 7 7800X3D"); yoksa bos
    bool uvp_applies = false;         // Intel 12. nesil ve sonrasi
    std::string verdict_key;          // undervolt.verdict.*
    std::string tool_key;             // undervolt.tool.* (Unknown/Locked/NotSupported'ta bos)
    std::vector<std::string> step_keys;  // sirali; hepsi metin katalogunda
    bool auto_apply = false;          // daima false
};

// cpu_vendor: "GenuineIntel" / "AuthenticAMD" / "Intel" / "AMD" (bos olabilir; marka metninden de cikarilir).
// cpu_brand: CPUID marka metni ("13th Gen Intel(R) Core(TM) i7-13700KF" ...). is_laptop: kasa turu biliniyorsa.
UndervoltGuide undervolt_guide_for(std::string_view cpu_vendor, std::string_view cpu_brand, bool is_laptop);

std::string_view verdict_text_key(UndervoltVerdict v) noexcept;

// Once/sonra kararlilik degerlendirmesi (yalniz kullanicinin kendi ayari icin bilgi; uygulama bir sey uygulamaz).
enum class StabilityClaim { NoData, FailureSeen, NoFailureSeen };  // "Stable/garanti" diye bir sonuc yoktur
struct StabilityInput {
    int passes = 0;            // tamamlanan bagimsiz test gecisi
    int failures = 0;          // test hatasi / dogrulama uyusmazligi
    bool whea_or_crash = false;  // test penceresinde WHEA ya da cokme
};
StabilityClaim evaluate_stability(const StabilityInput& in) noexcept;
std::string_view stability_text_key(StabilityClaim c) noexcept;

}  // namespace ef::core::guided
