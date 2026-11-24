// EF-X07.01: CPU undervolt uygunluk + rehber + kararlilik degerlendirmesi (saf; yazma yolu yok).
#include <algorithm>
#include <string>

#include "core/guided/undervolt_guide.h"
#include "core/i18n/catalog.h"
#include "ef_test.h"

using namespace ef::core::guided;
namespace i18n = ef::core::i18n;

namespace {
bool has_step(const UndervoltGuide& g, std::string_view k) { return std::find(g.step_keys.begin(), g.step_keys.end(), k) != g.step_keys.end(); }
UndervoltGuide intel(const char* brand, bool laptop = false) { return undervolt_guide_for("GenuineIntel", brand, laptop); }
UndervoltGuide amd(const char* brand, bool laptop = false) { return undervolt_guide_for("AuthenticAMD", brand, laptop); }
}  // namespace

// T01: kilitli CPU veya UVP icin atlatma denenmez; otomatik uygulama hic yok.
EF_TEST(x07_never_auto_applies_and_never_bypasses) {
    EF_CHECK(!kAutoTuneAvailable);
    for (const char* b : {"Intel(R) Core(TM) i5-10400 CPU @ 2.90GHz", "13th Gen Intel(R) Core(TM) i7-13700KF", "AMD Ryzen 7 7800X3D 8-Core Processor", "",
                          "Intel(R) Celeron(R) N4020", "12th Gen Intel(R) Core(TM) i7-12700H"}) {
        for (const char* v : {"GenuineIntel", "AuthenticAMD", ""}) {
            const auto g = undervolt_guide_for(v, b, false);
            EF_CHECK(!g.auto_apply);
            EF_CHECK(!g.step_keys.empty() && g.step_keys.front() == "undervolt.step.no_auto");
        }
    }
    const auto locked = intel("12th Gen Intel(R) Core(TM) i5-12400F");
    EF_CHECK(locked.verdict == UndervoltVerdict::Locked && locked.model == "i5-12400F" && locked.tool_key.empty());
    EF_CHECK(has_step(locked, "undervolt.step.no_bypass") && locked.uvp_applies && has_step(locked, "undervolt.step.uvp"));
    EF_CHECK(!has_step(locked, "undervolt.step.small"));  // kilitli modelde ayar adimi uydurulmaz
}

EF_TEST(x07_intel_unlocked_suffixes_follow_xtu_list) {
    for (const char* b : {"Intel(R) Core(TM) i9-14900K", "13th Gen Intel(R) Core(TM) i7-13700KF", "Intel(R) Core(TM) i9-13900KS", "Intel(R) Core(TM) i9-10980XE CPU @ 3.00GHz",
                          "Intel(R) Core(TM) i7-8850H", "Intel(R) Core(TM) Ultra 9 285K"}) {
        const auto g = intel(b);
        const bool want = std::string(b).find("8850H") == std::string::npos;
        EF_CHECK((g.verdict == UndervoltVerdict::MayBeAdjustable) == want);
    }
    const auto k = intel("Intel(R) Core(TM) i9-14900K");
    EF_CHECK(k.model == "i9-14900K" && k.tool_key == "undervolt.tool.xtu" && k.uvp_applies && has_step(k, "undervolt.step.uvp"));
    EF_CHECK(has_step(k, "undervolt.step.stability") && has_step(k, "undervolt.step.warranty") && k.step_keys.back() == "undervolt.step.warranty");
    const auto old = intel("Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz");
    EF_CHECK(old.verdict == UndervoltVerdict::MayBeAdjustable && !old.uvp_applies && !has_step(old, "undervolt.step.uvp"));
    const auto hk = intel("Intel(R) Core(TM) i9-8950HK", true);
    EF_CHECK(hk.verdict == UndervoltVerdict::MayBeAdjustable && has_step(hk, "undervolt.step.laptop_oem"));
    const auto ultra = intel("Intel(R) Core(TM) Ultra 7 155H", true);
    EF_CHECK(ultra.verdict == UndervoltVerdict::Locked && ultra.uvp_applies && ultra.model == "Ultra 7 155H");
}

EF_TEST(x07_intel_unsupported_families_and_unknowns) {
    for (const char* b : {"Intel(R) Celeron(R) N4020 CPU @ 1.10GHz", "Intel(R) Atom(TM) x5-Z8350", "Intel(R) Xeon(R) W-2295 CPU @ 3.00GHz"})
        EF_CHECK(intel(b).verdict == UndervoltVerdict::NotSupported);
    // Resmi listede olmayan / taninmayan: tahmin yok
    EF_CHECK(intel("13th Gen Intel(R) Core(TM) i9-13980HX").verdict == UndervoltVerdict::Unknown);
    EF_CHECK(intel("Intel(R) Pentium(R) Gold 7505").verdict == UndervoltVerdict::Unknown);
    EF_CHECK(intel("").verdict == UndervoltVerdict::Unknown);
    EF_CHECK(undervolt_guide_for("", "Some CPU", false).verdict == UndervoltVerdict::Unknown);
    EF_CHECK(undervolt_guide_for("", "Some CPU", false).vendor_key.empty());
}

EF_TEST(x07_amd_desktop_needs_product_page_check_mobile_unknown) {
    const auto d = amd("AMD Ryzen 7 7800X3D 8-Core Processor");
    EF_CHECK(d.verdict == UndervoltVerdict::MayBeAdjustable && d.model == "Ryzen 7 7800X3D" && d.tool_key == "undervolt.tool.amd");
    EF_CHECK(has_step(d, "undervolt.step.amd_check") && has_step(d, "undervolt.step.stability") && !d.uvp_applies);
    EF_CHECK(amd("AMD Ryzen 5 5600 6-Core Processor").verdict == UndervoltVerdict::MayBeAdjustable);
    EF_CHECK(amd("AMD Ryzen 7 8700G w/ Radeon 780M Graphics").verdict == UndervoltVerdict::MayBeAdjustable);
    for (const char* b : {"AMD Ryzen 9 7940HS w/ Radeon 780M Graphics", "AMD Ryzen 7 5800U with Radeon Graphics", "AMD Ryzen AI 9 HX 370 w/ Radeon 890M"}) {
        const auto m = amd(b);
        EF_CHECK(m.verdict == UndervoltVerdict::Unknown && has_step(m, "undervolt.step.laptop_oem") && m.tool_key.empty());
    }
    EF_CHECK(amd("AMD Ryzen 5 5600", true).verdict == UndervoltVerdict::Unknown);  // dizustu: OEM karar verir
    EF_CHECK(amd("AMD Ryzen Threadripper PRO 5995WX 64-Cores").verdict == UndervoltVerdict::Unknown);
    EF_CHECK(amd("AMD Athlon Silver 3050U").verdict == UndervoltVerdict::Unknown);
}

// T03: tek gecis (veya birkac) kararlilik garantisi sayilmaz; hata varsa basarisiz.
EF_TEST(x07_stability_never_claims_stable) {
    EF_CHECK(evaluate_stability({}) == StabilityClaim::NoData);
    EF_CHECK(evaluate_stability({1, 0, false}) == StabilityClaim::NoFailureSeen);
    EF_CHECK(evaluate_stability({100, 0, false}) == StabilityClaim::NoFailureSeen);  // "kararli" diye bir sonuc yok
    EF_CHECK(evaluate_stability({5, 1, false}) == StabilityClaim::FailureSeen);
    EF_CHECK(evaluate_stability({5, 0, true}) == StabilityClaim::FailureSeen);
    EF_CHECK(evaluate_stability({0, 0, true}) == StabilityClaim::FailureSeen);
    EF_CHECK(stability_text_key(StabilityClaim::NoFailureSeen) == "undervolt.stability.no_failure");
}

EF_TEST(x07_every_key_exists_in_both_catalogs) {
    std::vector<std::string> keys = {"undervolt.title"};
    for (auto v : {UndervoltVerdict::MayBeAdjustable, UndervoltVerdict::Locked, UndervoltVerdict::NotSupported, UndervoltVerdict::Unknown})
        keys.emplace_back(verdict_text_key(v));
    for (auto c : {StabilityClaim::NoData, StabilityClaim::FailureSeen, StabilityClaim::NoFailureSeen}) keys.emplace_back(stability_text_key(c));
    for (const char* b : {"Intel(R) Core(TM) i9-14900K", "Intel(R) Core(TM) i9-8950HK", "Intel(R) Core(TM) i5-12400", "Intel(R) Celeron(R) N4020",
                          "AMD Ryzen 7 7800X3D", "AMD Ryzen 9 7940HS"}) {
        for (bool laptop : {false, true}) {
            const auto g = undervolt_guide_for(b[0] == 'A' ? "AuthenticAMD" : "GenuineIntel", b, laptop);
            keys.push_back(g.verdict_key);
            if (!g.tool_key.empty()) keys.push_back(g.tool_key);
            keys.insert(keys.end(), g.step_keys.begin(), g.step_keys.end());
        }
    }
    for (auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        const auto cat = i18n::Catalog::embedded(loc);
        EF_CHECK(cat.ok());
        if (!cat.ok()) continue;
        for (const auto& k : keys) {
            const auto id = i18n::id_of(k);
            EF_CHECK(id.has_value() && !cat.value().get(*id).empty());
        }
    }
}
