// P6: dizustu hibrit GPU - oyun basina Windows Grafik tercihi bulgulari + Arena "tumlesik kartta kostu" kurali.
// Sahte girdilerle saf kural testleri; GERCEK hibrit dizustu / gercek kayit okumasi DEGILDIR (mock gercek cihaz testi sayilmaz).
#include <string>

#include "core/diagnosis/balance.h"
#include "core/diagnosis/mismatch.h"
#include "core/i18n/catalog.h"
#include "core/readiness/readiness.h"
#include "ef_test.h"

using namespace ef::core::readiness;
namespace dg = ef::core::diagnosis;

namespace {
GpuPrefInfo laptop_hybrid(GpuPrefKind pref) {
    GpuPrefInfo g;
    g.laptop = true;
    g.hybrid = true;
    g.on_battery = false;
    g.games.push_back({"game.exe", pref});
    return g;
}
const Check* find(const std::vector<Check>& v, const std::string& id) {
    for (const auto& c : v)
        if (c.id == id) return &c;
    return nullptr;
}
}  // namespace

EF_TEST(gpu_pref_parse_setting) {
    const std::optional<std::string> none;
    EF_CHECK(gpu_pref_from_setting(none, false) == GpuPrefKind::SystemDefault);       // deger yok: Windows secer
    EF_CHECK(gpu_pref_from_setting(none, true) == GpuPrefKind::Unknown);              // okuma hatasi: tahmin yok
    EF_CHECK(gpu_pref_from_setting(std::string("GpuPreference=2;"), false) == GpuPrefKind::HighPerformance);
    EF_CHECK(gpu_pref_from_setting(std::string(" gpupreference = 1 ;"), false) == GpuPrefKind::PowerSaving);
    EF_CHECK(gpu_pref_from_setting(std::string("GpuPreference=0;"), false) == GpuPrefKind::SystemDefault);
    EF_CHECK(gpu_pref_from_setting(std::string("GpuPreference=7;"), false) == GpuPrefKind::Unknown);
    EF_CHECK(gpu_pref_from_setting(std::string("SwapEffectUpgradeEnable=1;"), false) == GpuPrefKind::Unknown);
}

EF_TEST(gpu_pref_silent_unless_laptop_and_hybrid) {
    auto g = laptop_hybrid(GpuPrefKind::SystemDefault);
    g.laptop = false;
    EF_CHECK(gpu_preference_checks(g).empty());  // masaustu
    g = laptop_hybrid(GpuPrefKind::SystemDefault);
    g.hybrid = false;
    EF_CHECK(gpu_preference_checks(g).empty());  // tek GPU / ayrim yok
    g = laptop_hybrid(GpuPrefKind::SystemDefault);
    g.hybrid = std::nullopt;
    EF_CHECK(gpu_preference_checks(g).empty());  // bilinmiyor: iddia yok
    g = laptop_hybrid(GpuPrefKind::SystemDefault);
    g.laptop = std::nullopt;
    EF_CHECK(gpu_preference_checks(g).empty());
}

EF_TEST(gpu_pref_findings_levels_and_guided_fix) {
    auto c = gpu_preference_checks(laptop_hybrid(GpuPrefKind::SystemDefault));
    EF_CHECK(c.size() == 1);
    EF_CHECK(c[0].section == Section::Graphics && c[0].level == Level::Advice && c[0].text_key == "chk.gpu.pref_default");
    EF_CHECK(c[0].action == ActionKind::OpenSettings && c[0].action_arg == "ms-settings:display-advancedgraphics");  // rehberli, yazma yok
    EF_CHECK(c[0].args.at("game") == "game.exe");

    c = gpu_preference_checks(laptop_hybrid(GpuPrefKind::PowerSaving));
    EF_CHECK(c.size() == 1 && c[0].level == Level::Advice && c[0].text_key == "chk.gpu.pref_power_saving");

    c = gpu_preference_checks(laptop_hybrid(GpuPrefKind::HighPerformance));
    EF_CHECK(c.size() == 1 && c[0].level == Level::Good && c[0].text_key == "chk.gpu.pref_high" && c[0].action == ActionKind::None);

    c = gpu_preference_checks(laptop_hybrid(GpuPrefKind::Unknown));
    EF_CHECK(c.size() == 1 && c[0].level == Level::Unknown && c[0].text_key == "chk.gpu.pref_unknown");  // veri yetersiz != sorun
    for (const auto& x : c) EF_CHECK(x.action != ActionKind::AutoFix);  // bu turda otomatik yazma yok
}

EF_TEST(gpu_pref_battery_and_game_cap) {
    auto g = laptop_hybrid(GpuPrefKind::HighPerformance);
    g.on_battery = true;
    auto c = gpu_preference_checks(g);
    EF_CHECK(find(c, "graphics.gpu_pref.battery") && find(c, "graphics.gpu_pref.battery")->level == Level::Advice);
    g.on_battery = std::nullopt;  // pil durumu okunamadi: pil iddiasi yok
    EF_CHECK(!find(gpu_preference_checks(g), "graphics.gpu_pref.battery"));
    g.games.clear();
    for (int i = 0; i < 6; ++i) g.games.push_back({"g" + std::to_string(i) + ".exe", GpuPrefKind::SystemDefault});
    EF_CHECK(gpu_preference_checks(g).size() == 3);  // en cok 3 oyun
}

EF_TEST(gpu_pref_text_keys_exist) {
    for (const char* k : {"chk.gpu.pref_high", "chk.gpu.pref_default", "chk.gpu.pref_power_saving", "chk.gpu.pref_unknown",
                          "chk.gpu.battery_hybrid", "balance.reason.ran_on_integrated_gpu"})
        EF_CHECK(ef::core::i18n::id_of(k).has_value());
}

EF_TEST(arena_ran_on_integrated_rule) {
    const std::string igpu = "00000000:0000aaaa", dgpu = "00000000:0000bbbb";
    EF_CHECK(dg::ran_on_integrated_with_discrete(igpu, dgpu, igpu, 2));    // hibrit, iGPU'da kostu
    EF_CHECK(!dg::ran_on_integrated_with_discrete(dgpu, dgpu, igpu, 2));   // dGPU'da kostu
    EF_CHECK(!dg::ran_on_integrated_with_discrete(igpu, dgpu, igpu, 1));   // tek donanim karti: iddia yok
    EF_CHECK(!dg::ran_on_integrated_with_discrete(igpu, igpu, igpu, 2));   // DXGI kart ayirmiyor
    EF_CHECK(!dg::ran_on_integrated_with_discrete("", dgpu, igpu, 2));     // okunamadi
    EF_CHECK(!dg::ran_on_integrated_with_discrete(igpu, "", igpu, 2));
    EF_CHECK(!dg::ran_on_integrated_with_discrete(igpu, dgpu, "", 2));
}
