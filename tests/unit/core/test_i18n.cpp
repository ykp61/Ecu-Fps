#include <algorithm>
#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "core/i18n/turkish.h"
#include "ef_test.h"

using namespace ef::core::i18n;
using ef::contracts::ErrorCode;

EF_TEST(embedded_catalogs_are_complete) {
    for (auto l : {Locale::TrTR, Locale::EnUS}) {
        auto c = Catalog::embedded(l);
        EF_CHECK(c.ok());
        if (!c.ok()) continue;
        for (std::size_t i = 0; i < kStringCount; ++i) EF_CHECK(!c.value().get(static_cast<StringId>(i)).empty());
    }
    auto tr = Catalog::embedded(Locale::TrTR);
    EF_CHECK(tr.value().get(StringId::PerformanceOn) == "Performans açık");
    EF_CHECK(tr.value().get(StringId::ActionConfirm) == "Evet");
    EF_CHECK(tr.value().get(StringId::NavHistory) == "Geçmiş");
    auto en = Catalog::embedded(Locale::EnUS);
    EF_CHECK(en.value().get(StringId::ActionConfirm) == "Yes");
}

EF_TEST(cleanup_delete_confirm_is_lexically_distinct_from_performance_confirm) {
    // DS-04: geri donusuz temizlik onayi duz "Evet" degildir; iki dilde ayri ve acik etiket.
    for (auto l : {Locale::TrTR, Locale::EnUS}) {
        auto c = Catalog::embedded(l);
        EF_CHECK(c.ok());
        if (!c.ok()) continue;
        EF_CHECK(c.value().get(StringId::CleanupConfirmDelete) != c.value().get(StringId::ActionConfirm));
        EF_CHECK(c.value().get(StringId::CleanupConfirmDelete).size() > c.value().get(StringId::ActionConfirm).size());
        EF_CHECK(!c.value().get(StringId::CleanupScanCancel).empty());
        EF_CHECK(!c.value().get(StringId::CleanupStorageSettings).empty());
    }
}

EF_TEST(catalog_rejects_missing_unknown_empty_keys) {
    // Eksik anahtar
    auto missing = Catalog::load(R"({"schema_version":1,"locale":"tr-TR","strings":{"performance.on":"x"}})", Locale::TrTR);
    EF_CHECK(!missing.ok());
    EF_CHECK(missing.code() == ErrorCode::MissingValue);
    // Bilinmeyen anahtar
    std::string doc(embedded_json(Locale::TrTR));
    const auto pos = doc.find("\"strings\": {");
    EF_CHECK(pos != std::string::npos);
    std::string extra = doc;
    extra.insert(pos + 12, "\"evil.key\":\"x\",");
    EF_CHECK(Catalog::load(extra, Locale::TrTR).code() == ErrorCode::InvalidEnum);
    // Bos metin
    std::string empty = doc;
    const auto k = empty.find("\"Evet\"");
    empty.replace(k, 6, "\"\"");
    EF_CHECK(Catalog::load(empty, Locale::TrTR).code() == ErrorCode::MissingValue);
    // Yanlis dil
    EF_CHECK(Catalog::load(doc, Locale::EnUS).code() == ErrorCode::InvalidEnum);
    // Yeni sema
    std::string newer = doc;
    newer.replace(newer.find("\"schema_version\": 1"), 19, "\"schema_version\": 9");
    EF_CHECK(Catalog::load(newer, Locale::TrTR).code() == ErrorCode::SchemaTooNew);
}

EF_TEST(locale_selection) {
    EF_CHECK(select_locale("tr-TR") == Locale::TrTR);
    EF_CHECK(select_locale("TR") == Locale::TrTR);
    EF_CHECK(select_locale("en-GB") == Locale::EnUS);
    EF_CHECK(select_locale("de-DE") == Locale::EnUS);
    EF_CHECK(select_locale("tru") == Locale::EnUS);
    EF_CHECK(select_locale("") == Locale::EnUS);
}

EF_TEST(turkish_casing_dotted_dotless_i) {
    EF_CHECK(tr_upper("istanbul") == "İSTANBUL");
    EF_CHECK(tr_upper("ılık") == "ILIK");
    EF_CHECK(tr_lower("IŞIK") == "ışık");
    EF_CHECK(tr_lower("İZMİR") == "izmir");
    EF_CHECK(tr_upper("çğöşü") == "ÇĞÖŞÜ");
    EF_CHECK(tr_lower("ÇĞÖŞÜ") == "çğöşü");
    EF_CHECK(tr_lower(tr_upper("iı")) == "iı");  // donusum kayipsiz
    EF_CHECK(tr_upper("RTX 5080 fps") == "RTX 5080 FPS");
}

EF_TEST(turkish_casing_handles_invalid_utf8) {
    const std::string bad = "a\xC3";
    EF_CHECK(tr_upper(bad) == "A\xEF\xBF\xBD");  // U+FFFD
    EF_CHECK(!tr_lower("\xFF\xFE").empty());
}

EF_TEST(turkish_alphabet_ordering) {
    std::vector<std::string> words{"zeytin", "şeker", "su", "ip", "ılık", "çay", "cam", "öğle", "ok", "Ünlü", "uzun", "ğ"};
    std::sort(words.begin(), words.end(), [](const auto& a, const auto& b) { return tr_compare(a, b) < 0; });
    const std::vector<std::string> expected{"cam", "çay", "ğ", "ılık", "ip", "ok", "öğle", "su", "şeker", "uzun", "Ünlü", "zeytin"};
    EF_CHECK(words == expected);
    EF_CHECK(tr_compare("Işık", "ışık") > 0);  // esitlikte kucuk harf once
    EF_CHECK(tr_compare("kâr", "kar") == 0);  // sapka birincil sirayi degistirmez
    EF_CHECK(tr_compare("kâr", "kas") < 0);
    EF_CHECK(tr_compare("a", "a") == 0);
}

// Denetim 5.2: dil = Ayarlar tercihi, yoksa Windows GORUNTU dili. Gecersiz tercih "system" sayilir.
EF_TEST(i18n_ui_language_preference_wins_over_windows) {
    EF_CHECK(resolve_ui_locale("system", "tr-TR") == Locale::TrTR);
    EF_CHECK(resolve_ui_locale("system", "en-GB") == Locale::EnUS);
    EF_CHECK(resolve_ui_locale("system", "") == Locale::EnUS);
    EF_CHECK(resolve_ui_locale("en-US", "tr-TR") == Locale::EnUS);  // Turkce Windows, Ingilizce secim
    EF_CHECK(resolve_ui_locale("tr-TR", "de-DE") == Locale::TrTR);
    EF_CHECK(resolve_ui_locale("fr-FR", "tr-TR") == Locale::TrTR);  // gecersiz tercih -> Windows
    EF_CHECK(valid_ui_language("system") && valid_ui_language("tr-TR") && valid_ui_language("en-US"));
    EF_CHECK(!valid_ui_language("") && !valid_ui_language("tr") && !valid_ui_language("EN-US"));
}

// Uyumluluk #1: dil secimi pencere kapatilip acilinca degil, hemen uygulanir. Yeniden yukleme yalniz cozulen dil
// acik katalogdan farkliysa olur; ayni dile "degisiklik" katalogu yeniden kurmaz.
EF_TEST(i18n_locale_switch_only_when_resolved_language_differs) {
    EF_CHECK(locale_switch("en-US", "tr-TR", Locale::TrTR) == Locale::EnUS);  // saha: Ingilizce secildi, arayuz Turkce kaldi
    EF_CHECK(locale_switch("tr-TR", "en-US", Locale::EnUS) == Locale::TrTR);
    EF_CHECK(!locale_switch("en-US", "tr-TR", Locale::EnUS).has_value());
    EF_CHECK(!locale_switch("system", "tr-TR", Locale::TrTR).has_value());
    EF_CHECK(locale_switch("system", "en-GB", Locale::TrTR) == Locale::EnUS);  // Windows dili degisti, "sisteme uy"
    EF_CHECK(!locale_switch("bogus", "de-DE", Locale::EnUS).has_value());       // gecersiz tercih -> Windows -> en-US
    // Yeni dilin gomulu katalogu gercekten o dilin metnini verir (yeniden kurulan pencere yeni dili gosterir).
    const auto target = locale_switch("en-US", "tr-TR", Locale::TrTR);
    EF_CHECK(target.has_value());
    if (target) {
        auto c = Catalog::embedded(*target);
        EF_CHECK(c.ok() && c.value().locale() == Locale::EnUS && c.value().get(StringId::ActionConfirm) == "Yes");
    }
}
