// F-79: calisan overlay/izleme/RGB sureclerinin siniflandirmasi (saf; Win32 cagrisi yok).
// Surec varligi nedensellik kaniti degildir; bulgular bilgi/oneri duzeyindedir ve AutoFix yoktur.
#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "core/readiness/readiness.h"
#include "ef_test.h"

using namespace ef::core::readiness;

namespace {

const Check* find_ptr(const std::vector<Check>& v, const std::string& id) {
    for (const auto& c : v)
        if (c.id == id) return &c;
    return nullptr;
}

}  // namespace

EF_TEST(background_empty_makes_no_claim) {
    const auto stack = classify_background_stack({});
    EF_CHECK(stack.overlays.empty() && stack.monitoring.empty() && stack.rgb.empty());
    EF_CHECK(background_stack_checks(stack).empty());
}

EF_TEST(background_unrelated_processes_ignored) {
    const auto stack = classify_background_stack({"explorer.exe", "game.exe", "steam.exe", "chrome.exe"});
    EF_CHECK(stack.overlays.empty() && stack.monitoring.empty() && stack.rgb.empty());
    EF_CHECK(background_stack_checks(stack).empty());
}

EF_TEST(background_paths_and_case_normalized) {
    const auto stack = classify_background_stack(
        {R"(C:\Program Files\Discord\Discord.exe)", "C:/Tools/RTSS/RTSS.EXE", R"(D:\Apps\iCUE.exe)"});
    EF_CHECK(stack.overlays.size() == 2);
    EF_CHECK(stack.overlays.at(0) == "discord.exe");
    EF_CHECK(stack.overlays.at(1) == "rtss.exe");
    EF_CHECK(stack.rgb.size() == 1 && stack.rgb.at(0) == "icue.exe");
}

EF_TEST(background_duplicates_counted_once) {
    const auto stack = classify_background_stack({"discord.exe", R"(C:\Discord\Discord.exe)", "DISCORD.EXE"});
    EF_CHECK(stack.overlays.size() == 1);
}

EF_TEST(background_single_overlay_is_info) {
    const auto stack = classify_background_stack({"discord.exe"});
    const auto v = background_stack_checks(stack);
    EF_CHECK(v.size() == 1);
    const auto* c = find_ptr(v, "background.overlay");
    EF_CHECK(c && c->level == Level::Info && c->section == Section::Software);
    EF_CHECK(c && c->text_key == "chk.background.overlay");
    EF_CHECK(c && c->args.at("list") == "discord.exe");
    EF_CHECK(c && c->action == ActionKind::None);
    EF_CHECK(!find_ptr(v, "background.overlay_many"));
    EF_CHECK(ef::core::i18n::id_of("chk.background.overlay").has_value());
}

EF_TEST(background_many_overlays_advise_with_count) {
    const auto stack = classify_background_stack({"discord.exe", "obs64.exe", "gamebar.exe"});
    const auto v = background_stack_checks(stack);
    const auto* c = find_ptr(v, "background.overlay_many");
    EF_CHECK(c && c->level == Level::Advice && c->text_key == "chk.background.overlay_many");
    EF_CHECK(c && c->args.at("count") == "3");
    EF_CHECK(c && c->args.at("list") == "discord.exe, obs64.exe, gamebar.exe");
    EF_CHECK(!find_ptr(v, "background.overlay"));
    EF_CHECK(ef::core::i18n::id_of("chk.background.overlay_many").has_value());
}

EF_TEST(background_monitoring_advises) {
    const auto stack = classify_background_stack({"HWiNFO64.exe", R"(C:\MSI Afterburner\MSIAfterburner.exe)"});
    EF_CHECK(stack.monitoring.size() == 2);
    const auto v = background_stack_checks(stack);
    const auto* c = find_ptr(v, "background.monitoring");
    EF_CHECK(c && c->level == Level::Advice && c->text_key == "chk.background.monitoring");
    EF_CHECK(c && c->args.at("list") == "hwinfo64.exe, msiafterburner.exe");
    EF_CHECK(ef::core::i18n::id_of("chk.background.monitoring").has_value());
}

EF_TEST(background_rgb_advises_no_close) {
    const auto stack = classify_background_stack({"iCUE.exe", "ArmouryCrate.UserSessionHelper.exe"});
    EF_CHECK(stack.rgb.size() == 2);
    const auto v = background_stack_checks(stack);
    const auto* c = find_ptr(v, "background.rgb");
    EF_CHECK(c && c->level == Level::Advice && c->text_key == "chk.background.rgb");
    EF_CHECK(c && c->section == Section::Software);
    EF_CHECK(ef::core::i18n::id_of("chk.background.rgb").has_value());
}

EF_TEST(background_multiple_patterns_same_process_counted_once) {
    // "razer synapse.exe" iki RGB desenine de uyar; ayni ad iki kez sayilmaz.
    const auto stack = classify_background_stack({"razer synapse.exe"});
    EF_CHECK(stack.rgb.size() == 1);
}

EF_TEST(background_never_produce_autofix) {
    const auto stack = classify_background_stack({"discord.exe", "obs64.exe", "hwinfo64.exe", "icue.exe"});
    const auto v = background_stack_checks(stack);
    EF_CHECK(!v.empty());
    for (const auto& c : v) EF_CHECK(c.action != ActionKind::AutoFix);
}
