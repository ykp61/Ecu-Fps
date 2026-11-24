#include "core/i18n/catalog.h"

#include "core/serialization/json.h"

namespace ef::core::i18n {

using contracts::ErrorCode;
using contracts::Result;

namespace {
constexpr std::array<std::string_view, kStringCount> kKeys{
#define EF_X(id, key) std::string_view(key),
    EF_STRING_IDS(EF_X)
#undef EF_X
};
constexpr std::size_t kMaxText = 512;
}  // namespace

std::string_view key_of(StringId id) noexcept {
    const auto i = static_cast<std::size_t>(id);
    return i < kStringCount ? kKeys[i] : std::string_view("?");
}

std::optional<StringId> id_of(std::string_view key) noexcept {
    for (std::size_t i = 0; i < kStringCount; ++i)
        if (kKeys[i] == key) return static_cast<StringId>(i);
    return std::nullopt;
}

std::string_view to_string(Locale l) noexcept { return l == Locale::TrTR ? "tr-TR" : "en-US"; }

Locale select_locale(std::string_view name) noexcept {
    if (name.size() >= 2 && (name[0] == 't' || name[0] == 'T') && (name[1] == 'r' || name[1] == 'R') &&
        (name.size() == 2 || name[2] == '-' || name[2] == '_'))
        return Locale::TrTR;
    return Locale::EnUS;
}

bool valid_ui_language(std::string_view pref) noexcept {
    return pref == kUiLanguageSystem || pref == "tr-TR" || pref == "en-US";
}

Locale resolve_ui_locale(std::string_view pref, std::string_view windows_ui_language) noexcept {
    if (pref == "tr-TR") return Locale::TrTR;
    if (pref == "en-US") return Locale::EnUS;
    return select_locale(windows_ui_language);
}

std::optional<Locale> locale_switch(std::string_view pref, std::string_view windows_ui_language, Locale current) noexcept {
    const Locale want = resolve_ui_locale(pref, windows_ui_language);
    if (want == current) return std::nullopt;
    return want;
}

Result<Catalog> Catalog::load(std::string_view json_text, Locale expected) {
    using R = Result<Catalog>;
    auto doc = json::parse(json_text);
    if (!doc.ok()) return R(doc.error());
    if (auto s = json::check_schema(doc.value(), 1); !s.ok()) return R(s.error());
    auto loc = json::get_string(doc.value(), "locale", 16);
    if (!loc.ok()) return R(loc.error());
    if (loc.value() != to_string(expected)) return R::fail(ErrorCode::InvalidEnum, "locale " + loc.value());
    const json::Value* strings = doc.value().find("strings");
    if (!strings || !strings->is(json::Value::Type::Object)) return R::fail(ErrorCode::ParseError, "strings");

    Catalog c;
    c.locale_ = expected;
    std::array<bool, kStringCount> seen{};
    for (std::size_t i = 0; i < strings->keys().size(); ++i) {
        const auto& key = strings->keys()[i];
        std::size_t idx = kStringCount;
        for (std::size_t k = 0; k < kStringCount; ++k)
            if (kKeys[k] == key) idx = k;
        if (idx == kStringCount) return R::fail(ErrorCode::InvalidEnum, "unknown key " + key);
        const auto& v = strings->items()[i];
        if (!v.is(json::Value::Type::String) || v.as_string().empty())
            return R::fail(ErrorCode::MissingValue, "empty " + key);
        if (v.as_string().size() > kMaxText) return R::fail(ErrorCode::ParseLimit, key);
        c.texts_[idx] = v.as_string();
        seen[idx] = true;
    }
    for (std::size_t k = 0; k < kStringCount; ++k)
        if (!seen[k]) return R::fail(ErrorCode::MissingValue, "missing key " + std::string(kKeys[k]));
    return c;
}

Result<Catalog> Catalog::embedded(Locale l) { return load(embedded_json(l), l); }

}  // namespace ef::core::i18n
