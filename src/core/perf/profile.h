#pragma once
// OPT-05: oyun profili yasam dongusu (saf; birim testlenir). Tek anahtar korunur: profil, "Performans Acik"
// icin hangi plan gruplarinin secilecegini oyun bazinda belirler; ayri mod/profil secimi kullaniciya sunulmaz.
//  - Profil oyun surumu + surucu surumune baglidir; degisirse "yeniden dogrulama" ister (otomatik
//    benchmark zorunlu degil: genel plan kullanilir, olcum istege bagli).
//  - Baska bir optimizer ayni ayari degistirirse yeniden yazma yarisi yapilmaz: ilk cakismada bir kez
//    uyarilir, ikinci cakismada o hedef icin "ateskes" (biz yazmayi birakiriz) ve kullaniciya bildirilir.
//  - Profil guncellemesi journal kayitlarina dokunmaz (eski islem kendi snapshot'iyla geri alinabilir).

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ef::core::perf {

struct GameProfile {
    int schema_version = 1;
    int revision = 1;                       // guncellemede artar
    std::string game_id;                    // "steam:730"
    std::string game_version, driver_version;  // dogrulandigi kosullar
    std::map<std::string, bool> groups;     // plan grubu -> secili
    bool measured = false;                  // olcumle dogrulandi mi (istege bagli)
};

enum class ProfileStatus { Valid, GameUpdated, DriverUpdated, NoProfile };
std::string_view to_string(ProfileStatus s) noexcept;
ProfileStatus profile_status(const std::optional<GameProfile>& p, const std::string& game_version, const std::string& driver_version);

// Profil yoksa veya gecersizse kullanilacak secim: genel (kullanicinin Evet/Hayir secimleri). Uzun test yok.
std::map<std::string, bool> effective_groups(const std::optional<GameProfile>& p, ProfileStatus st, const std::map<std::string, bool>& general);

// Profil guncelleme: revizyon artar, kosullar yenilenir; dogrulama bayragi sifirlanir.
GameProfile update_profile(const GameProfile& old, const std::map<std::string, bool>& groups, const std::string& game_version,
                           const std::string& driver_version);

// Yazma yarisi korumasi.
enum class DriftAction { None, WarnOnce, Ceasefire };
class DriftGuard {
public:
    // Hedefe biz `ours` yazdik; simdi `observed` okunuyor.
    DriftAction observe(const std::string& target, const std::string& ours, const std::string& observed);
    bool ceasefire(const std::string& target) const;
    std::vector<std::string> ceasefire_targets() const;

private:
    std::map<std::string, int> conflicts_;
};

}  // namespace ef::core::perf
