#pragma once
// WIN-09: baslangic girdileri icin salt okuma tavsiye kurallari (saf cekirdek; hicbir sey yazmaz).
// Girdi WIN-03 listesidir (ad, komut/yol, acik/kapali, konum) + istege bagli yayinci.
// Tavsiye yalniz bir oneridir: uygulama her girdi icin ayrica Evet/Hayir ister (StartupAdapter).
// Ilke: emin olunmayan her durumda NoAdvice. Windows/guvenlik/surucu/erisilebilirlik girdileri asla
// "kapat" onerisi almaz (Keep).

#include <string>
#include <string_view>
#include <vector>

#include "core/adapters/startup/startup.h"

namespace ef::core::adapters::startup {

enum class Advice { RecommendDisable, Keep, NoAdvice };
std::string_view to_string(Advice a) noexcept;

struct StartupItem {
    std::string name;
    std::string command;    // Run komutu veya kisayol yolu
    std::string publisher;  // imza/surum bilgisi yayincisi; bilinmiyorsa bos
    bool enabled = true;
    Location location = Location::UserRun;
};
StartupItem from_entry(const Entry& e, std::string publisher = {});

// Hangi kural eslesti (tani/kanit icin) ve kullaniciya gosterilecek gerekce anahtari.
enum class AdviceRule {
    WindowsComponent,   // Windows klasoru / Windows bileseni -> Keep
    Security,           // antivirus / Windows Guvenlik -> Keep
    AntiCheat,          // anti-cheat istemcisi -> Keep
    HardwareControl,    // surucu yardimcisi, ses/GPU/girdi kontrol paneli -> Keep
    Accessibility,      // erisilebilirlik araci -> Keep
    CloudSync,          // bulut esitleme -> NoAdvice (veri kaybi/cakisma riski kullanicinin karari)
    GameLauncher,       // oyun platformu -> NoAdvice (oyunlar/guncellemeler ona bagli; kullanici secimi)
    MicrosoftApp,       // diger Microsoft uygulamalari -> NoAdvice
    Updater,            // uygulama guncelleyici/yardimci -> RecommendDisable
    OptionalApp,        // oyun icin gerekmeyen, oturumda otomatik baslayan uygulama -> RecommendDisable
    AlreadyDisabled,    // RecommendDisable olurdu ama zaten kapali -> NoAdvice
    Unknown,            // taninmadi -> NoAdvice
};
std::string_view to_string(AdviceRule r) noexcept;

struct Recommendation {
    Advice advice = Advice::NoAdvice;
    AdviceRule rule = AdviceRule::Unknown;
    std::string_view reason_key;  // design/strings/pending/WIN-09.json anahtari
};

Recommendation recommend(const StartupItem& item);
std::vector<Recommendation> recommend_all(const std::vector<StartupItem>& items);

}  // namespace ef::core::adapters::startup
