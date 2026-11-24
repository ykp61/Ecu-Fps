// F-52: yaygin oyun tweak ayarlarinin salt-okuma denetimi (saf; Win32 cagrisi yok).
// Deger yoksa Windows varsayilani gecerlidir; okunamayan anahtar bulgu uretmez; AutoFix yoktur.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "core/readiness/readiness.h"
#include "ef_test.h"

using namespace ef::core::readiness;

namespace {

std::optional<Check> find(const std::vector<Check>& v, const std::string& id) {
    for (const auto& c : v)
        if (c.id == id) return c;
    return std::nullopt;
}

SystemTweaksInfo network_defaults() {
    SystemTweaksInfo t;
    t.network_readable = true;
    return t;
}

}  // namespace

EF_TEST(tweaks_unreadable_makes_no_claim) {
    EF_CHECK(system_tweak_checks(SystemTweaksInfo{}).empty());
}

EF_TEST(tweaks_defaults_report_info_values) {
    const auto v = system_tweak_checks(network_defaults());
    EF_CHECK(v.size() == 1);
    const auto c = find(v, "system.network_throttle_default");
    EF_CHECK(c && c->level == Level::Info && c->section == Section::Windows);
    EF_CHECK(c && c->text_key == "chk.system.network_throttle_default");
    EF_CHECK(c && c->args.at("nti") == "10" && c->args.at("sr") == "20");
    EF_CHECK(c && c->action == ActionKind::None);
    EF_CHECK(!find(v, "system.timer_resolution"));
    EF_CHECK(ef::core::i18n::id_of("chk.system.network_throttle_default").has_value());
}

EF_TEST(tweaks_modified_network_throttle_advises) {
    auto t = network_defaults();
    t.network_throttling_index = 0xFFFFFFFFu;
    const auto v = system_tweak_checks(t);
    const auto c = find(v, "system.network_throttle_modified");
    EF_CHECK(c && c->level == Level::Advice && c->text_key == "chk.system.network_throttle_modified");
    EF_CHECK(c && c->args.at("nti") == "4294967295" && c->args.at("sr") == "20");
    EF_CHECK(!find(v, "system.network_throttle_default"));
    EF_CHECK(ef::core::i18n::id_of("chk.system.network_throttle_modified").has_value());
}

EF_TEST(tweaks_modified_system_responsiveness_advises) {
    auto t = network_defaults();
    t.system_responsiveness = 10;
    const auto v = system_tweak_checks(t);
    const auto c = find(v, "system.network_throttle_modified");
    EF_CHECK(c && c->args.at("nti") == "10" && c->args.at("sr") == "10");
}

EF_TEST(tweaks_global_timer_enabled_advises) {
    auto t = network_defaults();
    t.timer_readable = true;
    t.global_timer_resolution_requests = 1;
    const auto v = system_tweak_checks(t);
    const auto c = find(v, "system.timer_resolution");
    EF_CHECK(c && c->level == Level::Advice && c->text_key == "chk.system.timer_resolution");
    EF_CHECK(c && c->args.at("value") == "1" && c->action == ActionKind::None);
    EF_CHECK(ef::core::i18n::id_of("chk.system.timer_resolution").has_value());
}

EF_TEST(tweaks_global_timer_disabled_makes_no_claim) {
    auto t = network_defaults();
    t.timer_readable = true;
    t.global_timer_resolution_requests = 0;
    EF_CHECK(!find(system_tweak_checks(t), "system.timer_resolution"));
}

EF_TEST(tweaks_timer_key_missing_makes_no_claim) {
    auto t = network_defaults();
    t.timer_readable = true;  // anahtar okunabildi, deger yok -> varsayilan 0
    EF_CHECK(!find(system_tweak_checks(t), "system.timer_resolution"));
}

EF_TEST(tweaks_timer_unreadable_makes_no_claim) {
    SystemTweaksInfo t;
    t.timer_readable = false;
    t.global_timer_resolution_requests = 1;  // tutarsiz girdi: okunamadiysa iddia yok
    EF_CHECK(system_tweak_checks(t).empty());
}

EF_TEST(tweaks_never_produce_autofix) {
    auto t = network_defaults();
    t.network_throttling_index = 0;
    t.timer_readable = true;
    t.global_timer_resolution_requests = 1;
    for (const auto& c : system_tweak_checks(t)) EF_CHECK(c.action != ActionKind::AutoFix);
}
