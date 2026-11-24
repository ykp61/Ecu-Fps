// EF-X09/X10: salt okuma durum satirlari ve kapilar (sentetik girdi).
#include <algorithm>

#include "core/broker/admin.h"
#include "core/readiness/platform_security.h"
#include "core/serialization/json.h"
#include "ef_test.h"

using namespace ef::core::readiness;

EF_TEST(x09_t01_known_state_gives_info_only_never_ok_or_fix) {
    const auto v = platform_security_checks({2, true, false});
    EF_CHECK(std::all_of(v.begin(), v.end(), [](const Check& c) { return c.level != Level::Good && c.level != Level::Problem && c.action == ActionKind::None; }));
    EF_CHECK(firmware_change_suggestable({2, true, false}));
}

EF_TEST(x09_unknown_security_state_blocks_firmware_suggestions) {
    const PlatformSecurity s{2, true, std::nullopt};  // BitLocker okunamadi
    EF_CHECK(!firmware_change_suggestable(s));
    const auto v = platform_security_checks(s);
    EF_CHECK(std::any_of(v.begin(), v.end(), [](const Check& c) { return c.id == "hardware.firmware_gate"; }));
}

EF_TEST(x09_t03_cstate_never_invented) {
    for (const auto& s : {PlatformSecurity{2, true, true}, PlatformSecurity{}}) {
        const auto v = platform_security_checks(s);
        const auto it = std::find_if(v.begin(), v.end(), [](const Check& c) { return c.id == "hardware.cstate"; });
        EF_CHECK(it != v.end() && it->text_key == "chk.platform.cstate_unknown" && it->level == Level::Unknown);
    }
}

EF_TEST(x09_t02_no_recovery_secret_field_exists) {
    // Durum modeli yalniz acik/kapali tasir: rapor/disa aktarim icin kodlanan hicbir alan anahtar/kurtarma icermez.
    const auto v = platform_security_checks({2, true, true});
    for (const auto& c : v) {
        EF_CHECK(c.text_key.find("recovery") == std::string::npos);
        for (const auto& [k, val] : c.args) EF_CHECK(k.find("key") == std::string::npos && k.find("recovery") == std::string::npos);
    }
}

EF_TEST(x10_mpo_only_in_display_symptom_context_and_write_closed) {
    EF_CHECK(mpo_checks(std::nullopt, false, true).empty());  // Boost varsayilani degil
    EF_CHECK(mpo_checks(std::nullopt, true, true).at(0).text_key == "chk.display.mpo_default");
    EF_CHECK(mpo_checks(5u, true, true).at(0).text_key == "chk.display.mpo_off");
    EF_CHECK(mpo_checks(std::nullopt, true, false).at(0).level == Level::Unknown);
    EF_CHECK(mpo_checks(1u, true, true).at(0).text_key == "chk.display.mpo_other");  // baska deger: etkisi bilinmiyor
    EF_CHECK(mpo_checks(std::nullopt, true, true).at(0).action == ActionKind::None);  // yazma kapali: eylem yok
    EF_CHECK(!kMpoWriteVerified);
}

EF_TEST(x10_t03_no_ulps_or_display_class_target_reaches_broker) {
    using ef::contracts::OperationId;
    using ef::contracts::OperationScope;
    for (const char* t : {"ulps:enable", "reg:HKLM\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000:EnableUlps"})
        for (auto id : {OperationId::VendorProfileValue, OperationId::PowerSettingSet, OperationId::MaintenanceJob})
            EF_CHECK(!ef::core::broker::admin_operation_allowed({id, OperationScope::Machine, t, "0", false, false}));
}
