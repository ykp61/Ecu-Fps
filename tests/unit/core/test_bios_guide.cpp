// T20: BIOS/XMP adim adim rehber (saf). Firmware'e yazma yok; her anahtar metin katalogunda (TR+EN) bulunur.
#include <algorithm>
#include <string>

#include "core/guided/bios_guide.h"
#include "core/guided/site_links.h"
#include "core/i18n/catalog.h"
#include "core/readiness/readiness.h"
#include "ef_test.h"

using namespace ef::core::guided;
namespace i18n = ef::core::i18n;

namespace {
bool has_step(const BiosGuide& g, std::string_view k) { return std::find(g.step_keys.begin(), g.step_keys.end(), k) != g.step_keys.end(); }
bool key_in_catalog(const i18n::Catalog& c, const std::string& key) {
    const auto id = i18n::id_of(key);
    return id && !c.get(*id).empty();
}
}  // namespace

EF_TEST(t20_bios_vendor_mapping_case_insensitive) {
    EF_CHECK(bios_guide_for("ASUSTeK COMPUTER INC.", "GenuineIntel").vendor_key == "asus");
    EF_CHECK(bios_guide_for("asustek computer inc.", "").vendor_key == "asus");
    EF_CHECK(bios_guide_for("Micro-Star International Co., Ltd.", "AuthenticAMD").vendor_key == "msi");
    EF_CHECK(bios_guide_for("MSI", "").vendor_key == "msi");
    EF_CHECK(bios_guide_for("Gigabyte Technology Co., Ltd.", "").vendor_key == "gigabyte");
    EF_CHECK(bios_guide_for("AORUS", "").vendor_key == "gigabyte");
    EF_CHECK(bios_guide_for("ASRock", "").vendor_key == "asrock");
    EF_CHECK(bios_guide_for("ASROCK INCORPORATION", "").vendor_key == "asrock");
}

EF_TEST(t20_bios_enter_keys_and_profile_names) {
    EF_CHECK(bios_guide_for("ASUSTeK COMPUTER INC.", "").enter_key_text_key == "bios.guide.enter.del_f2");
    EF_CHECK(bios_guide_for("ASRock", "").enter_key_text_key == "bios.guide.enter.del_f2");
    EF_CHECK(bios_guide_for("Micro-Star International Co., Ltd.", "").enter_key_text_key == "bios.guide.enter.del");
    EF_CHECK(bios_guide_for("Gigabyte Technology Co., Ltd.", "").enter_key_text_key == "bios.guide.enter.del");
    EF_CHECK(bios_guide_for("ASUSTeK COMPUTER INC.", "AuthenticAMD").profile_name_key == "bios.guide.profile.expo_docp");
    EF_CHECK(bios_guide_for("ASUSTeK COMPUTER INC.", "GenuineIntel").profile_name_key == "bios.guide.profile.xmp");
    EF_CHECK(bios_guide_for("Micro-Star International Co., Ltd.", "GenuineIntel").profile_name_key == "bios.guide.profile.xmp_axmp");
    EF_CHECK(bios_guide_for("Micro-Star International Co., Ltd.", "AuthenticAMD").profile_name_key == "bios.guide.profile.expo_axmp");
    EF_CHECK(bios_guide_for("Gigabyte Technology Co., Ltd.", "AuthenticAMD").profile_name_key == "bios.guide.profile.xmp_expo");
    EF_CHECK(bios_guide_for("Gigabyte Technology Co., Ltd.", "").profile_name_key == "bios.guide.profile.xmp_expo");  // saticisi bilinmiyorsa tahmin yok
}

EF_TEST(t20_bios_oem_and_laptops_explain_not_exposed) {
    for (const char* m : {"Dell Inc.", "HP", "Hewlett-Packard", "LENOVO", "Acer", "Alienware"}) {
        const auto g = bios_guide_for(m, "GenuineIntel");
        EF_CHECK(g.vendor_key == "oem");
        EF_CHECK(has_step(g, "bios.guide.step.oem_not_exposed"));
        EF_CHECK(!has_step(g, "bios.guide.step.profile"));  // olmayan secenek icin adim uydurulmaz
    }
    EF_CHECK(bios_guide_for("Chpower Co.", "").vendor_key == "generic");  // "hp" gibi kisa adlar baska sozcukte eslesmez
}

EF_TEST(t20_bios_unknown_is_generic) {
    for (const char* m : {"", "Unknown", "To be filled by O.E.M.", "BIOSTAR Group"}) {
        const auto g = bios_guide_for(m, "");
        EF_CHECK(g.vendor_key == "generic");
        EF_CHECK(g.enter_key_text_key == "bios.guide.enter.generic");
        EF_CHECK(has_step(g, "bios.guide.step.profile") && has_step(g, "bios.guide.step.verify"));
    }
}

EF_TEST(t20_bios_steps_are_safe_and_verifiable) {
    for (const char* m : {"ASUSTeK COMPUTER INC.", "Micro-Star International Co., Ltd.", "Gigabyte Technology Co., Ltd.", "ASRock", "x"}) {
        const auto g = bios_guide_for(m, "GenuineIntel");
        EF_CHECK(has_step(g, "bios.guide.step.note"));      // once mevcut degerleri not al
        EF_CHECK(has_step(g, "bios.guide.step.only"));      // yalniz profil; voltaj/zamanlama yok
        EF_CHECK(has_step(g, "bios.guide.step.fallback"));  // acilmazsa CMOS/otomatik geri donus
        EF_CHECK(has_step(g, "bios.guide.step.risk"));
        EF_CHECK(has_step(g, "bios.guide.step.verify"));    // xmp_after_restart: yeniden okuma
        EF_CHECK(g.step_keys.back() == "bios.guide.step.risk");
    }
}

EF_TEST(t20_bios_every_key_exists_in_both_catalogs) {
    for (auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        const auto cat = i18n::Catalog::embedded(loc);
        EF_CHECK(cat.ok());
        if (!cat.ok()) continue;
        for (const char* m : {"ASUSTeK COMPUTER INC.", "Micro-Star International Co., Ltd.", "Gigabyte Technology Co., Ltd.", "ASRock", "Dell Inc.", "Lenovo", "?"}) {
            for (const char* c : {"GenuineIntel", "AuthenticAMD", ""}) {
                const auto g = bios_guide_for(m, c);
                EF_CHECK(key_in_catalog(cat.value(), g.enter_key_text_key));
                EF_CHECK(key_in_catalog(cat.value(), g.profile_name_key));
                EF_CHECK(key_in_catalog(cat.value(), bios_vendor_name_key(g)));
                for (const auto& k : g.step_keys) EF_CHECK(key_in_catalog(cat.value(), k));
            }
        }
        for (const char* k : {"bios.guide.toggle", "bios.guide.title", "bios.guide.header", "bios.guide.readonly"}) EF_CHECK(key_in_catalog(cat.value(), k));
    }
}

EF_TEST(t20_settings_legal_link_rows) {
    // Ayarlar > Hakkinda: uc satir, sabit sira; adresler site tabanindan, izin listesinden gecer, sorgu dizesi yok.
    EF_CHECK(kLegalLinks.size() == 3);
    EF_CHECK(kLegalLinks[0].label_key == "settings.about.eula" && kLegalLinks[1].label_key == "settings.about.privacy" &&
              kLegalLinks[2].label_key == "settings.about.transparency");
    const auto tr = i18n::Catalog::embedded(i18n::Locale::TrTR);
    const auto en = i18n::Catalog::embedded(i18n::Locale::EnUS);
    EF_CHECK(tr.ok() && en.ok());
    for (const auto& l : kLegalLinks) {
        EF_CHECK(l.url.substr(0, kSiteBaseUrl.size()) == kSiteBaseUrl);
        EF_CHECK(l.url.find('?') == std::string_view::npos);
        EF_CHECK(ef::core::readiness::action_uri_allowed(std::string(l.url)));
        if (tr.ok()) EF_CHECK(key_in_catalog(tr.value(), std::string(l.label_key)));
        if (en.ok()) EF_CHECK(key_in_catalog(en.value(), std::string(l.label_key)));
    }
    EF_CHECK(!ef::core::readiness::action_uri_allowed("https://evil.example/eula.html"));
    EF_CHECK(!ef::core::readiness::action_uri_allowed("https://ecufast.asilpolat.com.evil.example/eula.html"));
}
