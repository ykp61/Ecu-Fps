#pragma once
// Gomulu metin tablosu. Tum kullanici metinleri buradan gelir; kodda sabit kullanici metni yazilmaz.

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/i18n/string_ids.h"

namespace ef::core::i18n {

enum class StringId {
#define EF_X(id, key) id,
    EF_STRING_IDS(EF_X)
#undef EF_X
    Count_
};

inline constexpr std::size_t kStringCount = static_cast<std::size_t>(StringId::Count_);
std::string_view key_of(StringId id) noexcept;
// Anahtardan kimlik (ornek bulgu text_key). Bilinmeyen anahtar: nullopt.
std::optional<StringId> id_of(std::string_view key) noexcept;

enum class Locale { TrTR, EnUS };
std::string_view to_string(Locale l) noexcept;

// Windows dil adindan (ornek "tr-TR", "en-GB") secim. Turkce disi her dil en-US'e duser.
Locale select_locale(std::string_view windows_locale_name) noexcept;

// Denetim 5.2 (sahip karari): uygulama dili tercihi. "system" = Windows GORUNTU dili (bolge bicimi degil),
// "tr-TR" / "en-US" = kullanicinin Ayarlar'da sectigi dil. Baska deger gecersizdir ve "system" sayilir.
inline constexpr std::string_view kUiLanguageSystem = "system";
bool valid_ui_language(std::string_view pref) noexcept;
Locale resolve_ui_locale(std::string_view pref, std::string_view windows_ui_language) noexcept;
// Uyumluluk #1: dil canli degisir (yeniden baslatma yok). Tercih + Windows dili acik katalogdan farkli bir dile
// cozuluyorsa o dil; aynisiysa nullopt (katalog yeniden yuklenmez, pencere yeniden kurulmaz).
std::optional<Locale> locale_switch(std::string_view pref, std::string_view windows_ui_language, Locale current) noexcept;

class Catalog {
public:
    // JSON metninden yukler. Eksik, fazla, bos veya cok uzun anahtar hata verir.
    static contracts::Result<Catalog> load(std::string_view json_text, Locale expected);
    // Uygulamaya gomulu tabloyu yukler.
    static contracts::Result<Catalog> embedded(Locale l);

    std::string_view get(StringId id) const noexcept { return texts_[static_cast<std::size_t>(id)]; }
    Locale locale() const noexcept { return locale_; }

private:
    Locale locale_ = Locale::TrTR;
    std::array<std::string, kStringCount> texts_;
};

// Gomulu ham JSON (derleme zamaninda design/strings/*.json'dan uretilir).
std::string_view embedded_json(Locale l) noexcept;

}  // namespace ef::core::i18n
