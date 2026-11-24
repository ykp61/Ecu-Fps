#pragma once
// T20: uygulamanin acabilecegi yasal / seffaflik sayfalari (site/*.html; T19). Yalniz kullanici tiklayinca tarayicida
// acilir; uygulama bu adreslere kendiliginden baglanmaz. Adres izin listesi core::readiness::action_uri_allowed'dedir.

#include <array>
#include <string_view>

namespace ef::core::guided {

inline constexpr std::string_view kSiteBaseUrl = "https://ecufast.asilpolat.com";

struct SiteLink {
    std::string_view label_key;  // settings.about.* metin anahtari
    std::string_view url;        // kSiteBaseUrl ile baslar
};

inline constexpr std::array<SiteLink, 3> kLegalLinks = {{
    {"settings.about.eula", "https://ecufast.asilpolat.com/eula.html"},
    {"settings.about.privacy", "https://ecufast.asilpolat.com/gizlilik-kvkk.html"},
    {"settings.about.transparency", "https://ecufast.asilpolat.com/guvenlik-seffaflik.html"},
}};

}  // namespace ef::core::guided
