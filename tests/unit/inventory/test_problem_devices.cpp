// T17: Aygit Yoneticisi sorun kodu eslemesi, kod 22 bilgi satiri, sinir ve hazirlik kontrolu (saf).
#include <string>

#include "core/i18n/catalog.h"
#include "core/inventory/problem_devices.h"
#include "core/readiness/readiness.h"
#include "ef_test.h"

namespace ci = ef::core::inventory;
namespace cr = ef::core::readiness;

namespace {
ci::ProblemDevice dev(const char* name, std::uint32_t code) {
    ci::ProblemDevice d;
    d.name = name;
    d.device_class = "Net";
    d.instance_id = "PCI#X";
    d.problem_code = code;
    return d;
}
}  // namespace

EF_TEST(problem_codes_map_to_keys_and_all_are_localized) {
    for (std::uint32_t code : {1u, 10u, 12u, 14u, 18u, 22u, 24u, 28u, 31u, 37u, 39u, 43u, 52u}) {
        EF_CHECK(ci::problem_is_known(code));
        EF_CHECK(std::string(ci::problem_text_key(code)) == "dev.problem." + std::to_string(code));
        EF_CHECK(ef::core::i18n::id_of(ci::problem_text_key(code)).has_value());
    }
    EF_CHECK(ef::core::i18n::id_of("dev.problem.generic").has_value());
}

EF_TEST(problem_unknown_code_falls_back_to_generic) {
    EF_CHECK(!ci::problem_is_known(77));
    EF_CHECK(ci::problem_text_key(77) == "dev.problem.generic");
    EF_CHECK(ci::problem_text_key(0) == "dev.problem.generic");
}

EF_TEST(problem_code_22_is_informational_not_fault) {
    EF_CHECK(ci::problem_is_informational(22));
    EF_CHECK(!ci::problem_is_informational(28));
    ci::ProblemDeviceReport r;
    r.enumerated = true;
    ci::add_problem_device(r, dev("Kapali", 22));
    EF_CHECK(ci::problem_fault_count(r) == 0);
    ci::add_problem_device(r, dev("Bozuk", 43));
    EF_CHECK(ci::problem_fault_count(r) == 1);
}

EF_TEST(problem_list_is_capped_at_64) {
    ci::ProblemDeviceReport r;
    r.enumerated = true;
    for (int i = 0; i < 70; ++i) ci::add_problem_device(r, dev("x", 28));
    EF_CHECK(r.devices.size() == ci::kMaxProblemDevices);
    EF_CHECK(r.truncated);
}

EF_TEST(problem_check_none_found_is_good) {
    ci::ProblemDeviceReport r;
    r.enumerated = true;
    const auto v = cr::problem_device_checks(r);
    EF_CHECK(v.size() == 1);
    EF_CHECK(v[0].id == "hardware.problem_devices" && v[0].section == cr::Section::Hardware);
    EF_CHECK(v[0].level == cr::Level::Good);
    EF_CHECK(v[0].action == cr::ActionKind::None);
}

EF_TEST(problem_check_only_disabled_device_is_still_good) {
    ci::ProblemDeviceReport r;
    r.enumerated = true;
    ci::add_problem_device(r, dev("Kapali", 22));
    EF_CHECK(cr::problem_device_checks(r)[0].level == cr::Level::Good);
}

EF_TEST(problem_check_found_warns_with_count_first_three_names_and_device_manager_action) {
    ci::ProblemDeviceReport r;
    r.enumerated = true;
    ci::add_problem_device(r, dev("A", 28));
    ci::add_problem_device(r, dev("Kapali", 22));
    ci::add_problem_device(r, dev("B", 43));
    ci::add_problem_device(r, dev("C", 10));
    ci::add_problem_device(r, dev("D", 39));
    const auto v = cr::problem_device_checks(r);
    EF_CHECK(v.size() == 1);
    EF_CHECK(v[0].level == cr::Level::Advice);
    EF_CHECK(v[0].text_key == "chk.hardware.problem_devices.found");
    EF_CHECK(v[0].args.at("count") == "4");
    EF_CHECK(v[0].args.at("names") == "A, B, C");  // ilk 3 ariza; kod 22 sayilmaz, D yazilmaz
    EF_CHECK(v[0].action == cr::ActionKind::OpenSettings);
    EF_CHECK(cr::action_uri_allowed(v[0].action_arg));
    EF_CHECK(ef::core::i18n::id_of(v[0].text_key).has_value());
}

EF_TEST(problem_check_enumeration_failure_is_unknown_never_good) {
    ci::ProblemDeviceReport r;  // enumerated=false
    const auto v = cr::problem_device_checks(r);
    EF_CHECK(v.size() == 1);
    EF_CHECK(v[0].level == cr::Level::Unknown);
    EF_CHECK(v[0].text_key == "chk.hardware.problem_devices.unknown");
    EF_CHECK(ef::core::i18n::id_of("chk.hardware.problem_devices.ok").has_value());
    EF_CHECK(ef::core::i18n::id_of("chk.hardware.problem_devices.unknown").has_value());
}

EF_TEST(devmgmt_is_the_only_added_non_settings_launch_target) {
    EF_CHECK(cr::action_uri_allowed("devmgmt.msc"));
    EF_CHECK(!cr::action_uri_allowed("devmgmt.msc /s"));
    EF_CHECK(!cr::action_uri_allowed("services.msc"));
}
