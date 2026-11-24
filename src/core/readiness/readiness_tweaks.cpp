// F-52: yaygin "oyun tweak" ayarlarinin salt-okuma denetimi (saf; Win32 yok).
// Kaynak: platform/windows/readiness/tweaks_reader (kayit defteri okumasi).
// Ilkeler:
//  - Deger yoksa Windows varsayilani (NetworkThrottlingIndex 10, SystemResponsiveness 20)
//    gecerlidir; anahtar okunamadiysa bulgu uretilmez.
//  - Tweak'lerin olculmus oyun faydasi yoktur; metin bunu soyler ve ters etki riskini belirtir.
//  - Hicbir deger yazilmaz; AutoFix yoktur (kullaniciya yalniz mevcut durum ve risk anlatilir).
#include <string>

#include "core/readiness/readiness.h"

namespace ef::core::readiness {

namespace {

constexpr std::uint32_t kDefaultNetworkThrottling = 10;
constexpr std::uint32_t kDefaultSystemResponsiveness = 20;

Check make(std::string id, Level level, std::string key) {
    Check c;
    c.id = std::move(id);
    c.section = Section::Windows;
    c.level = level;
    c.text_key = std::move(key);
    return c;
}

}  // namespace

std::vector<Check> system_tweak_checks(const SystemTweaksInfo& t) {
    std::vector<Check> out;
    if (t.network_readable) {
        const auto nti = t.network_throttling_index.value_or(kDefaultNetworkThrottling);
        const auto sr = t.system_responsiveness.value_or(kDefaultSystemResponsiveness);
        const bool modified = nti != kDefaultNetworkThrottling || sr != kDefaultSystemResponsiveness;
        Check c = modified
                     ? make("system.network_throttle_modified", Level::Advice, "chk.system.network_throttle_modified")
                     : make("system.network_throttle_default", Level::Info, "chk.system.network_throttle_default");
        c.args["nti"] = std::to_string(nti);
        c.args["sr"] = std::to_string(sr);
        out.push_back(std::move(c));
    }
    if (t.timer_readable && t.global_timer_resolution_requests.has_value() &&
        *t.global_timer_resolution_requests != 0) {
        Check c = make("system.timer_resolution", Level::Advice, "chk.system.timer_resolution");
        c.args["value"] = std::to_string(*t.global_timer_resolution_requests);
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace ef::core::readiness
