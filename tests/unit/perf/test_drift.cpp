#include "core/perf/drift.h"
#include "ef_test.h"

using namespace ef::core::perf;
using ef::contracts::ErrorCode;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::Result;
using ef::contracts::SettingOperation;

namespace {
ActiveStep step(OperationId id, const char* target, const char* value, OperationScope sc = OperationScope::CurrentUser, bool reboot = false) {
    ActiveStep s;
    s.op = SettingOperation{id, sc, target, value, reboot, false};
    s.group_key = drift_group_key(s.op);
    if (s.group_key.empty()) s.group_key = "plan.vfx";
    return s;
}
using Live = std::optional<std::string>;
}  // namespace

EF_TEST(drift_none_when_values_match) {
    const std::vector<ActiveStep> steps{step(OperationId::VisualEffectsSet, "a", "1"), step(OperationId::VisualEffectsSet, "b", "1")};
    const auto d = find_drift(steps, [](const SettingOperation&) { return Result<Live>(Live("1")); });
    EF_CHECK(d.empty());
}

EF_TEST(drift_detected_when_value_changed_or_absent) {
    const std::vector<ActiveStep> steps{step(OperationId::VisualEffectsSet, "a", "1"), step(OperationId::VisualEffectsSet, "b", "1"),
                                        step(OperationId::VisualEffectsSet, "c", "1")};
    const auto d = find_drift(steps, [](const SettingOperation& op) {
        if (op.target == "a") return Result<Live>(Live("1"));
        if (op.target == "b") return Result<Live>(Live("0"));
        return Result<Live>(Live{});  // deger yok
    });
    EF_CHECK(d.size() == 2);
    EF_CHECK(d[0].op.target == "b" && d[0].current && *d[0].current == "0" && d[0].group_key == "plan.vfx");
    EF_CHECK(d[1].op.target == "c" && !d[1].current);
}

// B9 (R-B1, R-B6 notlari): yeni hedeflerin sapma grubu kendi plan grubudur (onay ekraninda dogru satir).
EF_TEST(drift_group_key_maps_usb_and_nvidia_extra_targets) {
    const auto key = [](OperationId id, const char* target) {
        return drift_group_key(SettingOperation{id, OperationScope::Machine, target, "0", false, false});
    };
    EF_CHECK(key(OperationId::PowerSettingSet, "power:usbss:ac") == "plan.power_usb");
    EF_CHECK(key(OperationId::PowerSettingSet, "power:aspm:ac") == "plan.power_pcie");
    EF_CHECK(key(OperationId::PowerSettingSet, "power:boost:ac") == "plan.power_cpu");
    EF_CHECK(key(OperationId::VendorProfileValue, "drs:global:20C1221E") == "plan.nvidia_extra");  // G13
    EF_CHECK(key(OperationId::VendorProfileValue, "drs:global:1074C972") == "plan.nvidia_extra");  // G14
    EF_CHECK(key(OperationId::VendorProfileValue, "drs:global:1057EB71") == "plan.nvidia");        // G01 degismedi
    EF_CHECK(key(OperationId::VendorProfileValue, "drs:app:game.exe:007BA09E") == "plan.nvidia");  // G02 degismedi
}

EF_TEST(drift_unreadable_ignored) {
    const std::vector<ActiveStep> steps{step(OperationId::VisualEffectsSet, "a", "1")};
    const auto d = find_drift(steps, [](const SettingOperation&) { return Result<Live>::fail(ErrorCode::Denied, "needs admin"); });
    EF_CHECK(d.empty());  // okunamayan adim sapmis sayilmaz
}

EF_TEST(drift_ignores_process_scope_session_and_reboot_steps) {
    std::vector<ActiveStep> steps{step(OperationId::BackgroundProcessLower, "bgproc:1", "1", OperationScope::Process),
                                  step(OperationId::VisualEffectsSet, "p", "1", OperationScope::Process),
                                  step(OperationId::ServiceSessionStop, "svc", "stopped", OperationScope::Machine),
                                  step(OperationId::UpdatePauseSession, "u", "1", OperationScope::Machine),
                                  step(OperationId::RestorePointCreate, "r", "1", OperationScope::Machine),
                                  step(OperationId::MaintenanceJob, "m", "1", OperationScope::Machine),
                                  step(OperationId::HagsSet, "hags", "2", OperationScope::Machine, true)};
    for (auto& s : steps) s.group_key = "plan.x";
    int reads = 0;
    const auto d = find_drift(steps, [&](const SettingOperation&) {
        ++reads;
        return Result<Live>(Live("changed"));
    });
    EF_CHECK(d.empty() && reads == 0);  // yoksayilan adimlar hic okunmaz
}

EF_TEST(drift_last_written_counts_as_ours_and_unknown_group_skipped) {
    auto a = step(OperationId::VisualEffectsSet, "a", "1");
    a.written = "7";
    ActiveStep none = step(OperationId::VisualEffectsSet, "z", "1");
    none.group_key.clear();
    const auto d = find_drift({a, none}, [](const SettingOperation& op) { return Result<Live>(Live(op.target == "a" ? "7" : "0")); });
    EF_CHECK(d.empty());
}

#include "core/settings/preferences.h"

EF_TEST(drift_last_os_build_preference_round_trips) {
    ef::core::settings::Preferences p;
    EF_CHECK(p.last_os_build == 0);
    EF_CHECK(p.serialize().find("last_os_build") == std::string::npos);  // kaydedilmediyse yazilmaz
    p.last_os_build = 26300;
    const auto r = ef::core::settings::Preferences::parse(p.serialize());
    EF_CHECK(r.ok() && r.value().last_os_build == 26300);
    p.last_os_build = 0;
    const auto z = ef::core::settings::Preferences::parse(p.serialize());
    EF_CHECK(z.ok() && z.value().last_os_build == 0);
}
