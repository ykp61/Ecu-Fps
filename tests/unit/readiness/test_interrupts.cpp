// B5 (ADR-040 m.5): kesme (MSI) durumu teshisi. YALNIZ OKUMA; bulguda "Duzelt" yoktur. Saf kurallar, Win32 yok.
// Uc durum (Acik / Kapali / veri yetersiz) ve "desteklenmiyor" ayri metin anahtarlaridir.
#include <optional>
#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "core/readiness/fix_plan.h"
#include "core/readiness/readiness_interrupts.h"
#include "ef_test.h"
#include "platform/windows/readiness/interrupt_reader.h"

using namespace ef::core::readiness;

namespace {

InterruptDeviceInfo dev(InterruptDeviceRole role, bool readable, std::optional<std::uint32_t> msi,
                        std::optional<std::uint32_t> prio = std::nullopt) {
    InterruptDeviceInfo d;
    d.role = role;
    d.name = role == InterruptDeviceRole::GameGpu ? "Test GPU" : "Test NIC";
    d.readable = readable;
    d.msi_supported = msi;
    d.priority = prio;
    return d;
}

}  // namespace

EF_TEST(msi_check_states) {
    {
        const auto v = interrupt_checks({dev(InterruptDeviceRole::GameGpu, true, 1u)});
        EF_CHECK(v.size() == 1);
        EF_CHECK(v[0].id == "interrupt.gpu.msi" && v[0].text_key == "chk.interrupt.msi_on");
        EF_CHECK(v[0].level == Level::Info && v[0].section == Section::Graphics);
        EF_CHECK(v[0].args.at("name") == "Test GPU");
    }
    {
        const auto v = interrupt_checks({dev(InterruptDeviceRole::GameGpu, true, 0u)});
        EF_CHECK(v.size() == 1 && v[0].text_key == "chk.interrupt.msi_off" && v[0].level == Level::Info);
    }
    {
        const auto v = interrupt_checks({dev(InterruptDeviceRole::GameGpu, false, std::nullopt)});  // okunamadi: veri yetersiz
        EF_CHECK(v.size() == 1 && v[0].text_key == "chk.interrupt.no_data" && v[0].level == Level::Unknown);
    }
    {
        const auto v = interrupt_checks({dev(InterruptDeviceRole::GameGpu, true, std::nullopt)});  // okundu, deger yok: desteklenmiyor
        EF_CHECK(v.size() == 1 && v[0].text_key == "chk.interrupt.unsupported" && v[0].level == Level::Info);
    }
    {
        const auto v = interrupt_checks({dev(InterruptDeviceRole::GameGpu, true, 7u)});  // tanimsiz deger: tahmin yok
        EF_CHECK(v.size() == 1 && v[0].text_key == "chk.interrupt.no_data");
    }
    EF_CHECK(interrupt_checks({}).empty());  // aygit yok: bulgu yok
}

EF_TEST(msi_check_states_use_distinct_messages) {
    const auto on = interrupt_checks({dev(InterruptDeviceRole::GameGpu, true, 1u)});
    const auto off = interrupt_checks({dev(InterruptDeviceRole::GameGpu, true, 0u)});
    const auto nodata = interrupt_checks({dev(InterruptDeviceRole::GameGpu, false, std::nullopt)});
    const auto unsup = interrupt_checks({dev(InterruptDeviceRole::GameGpu, true, std::nullopt)});
    EF_CHECK(on[0].text_key != off[0].text_key && on[0].text_key != nodata[0].text_key);
    EF_CHECK(nodata[0].text_key != unsup[0].text_key && off[0].text_key != unsup[0].text_key);
}

EF_TEST(msi_check_priority_variant_only_when_measured) {
    const auto on = interrupt_checks({dev(InterruptDeviceRole::GameGpu, true, 1u, 3u)});
    EF_CHECK(on.size() == 1 && on[0].text_key == "chk.interrupt.msi_on_prio" && on[0].args.at("priority") == "3");
    const auto off = interrupt_checks({dev(InterruptDeviceRole::GameGpu, true, 0u, 2u)});
    EF_CHECK(off.size() == 1 && off[0].text_key == "chk.interrupt.msi_off_prio" && off[0].args.at("priority") == "2");
    const auto none = interrupt_checks({dev(InterruptDeviceRole::GameGpu, true, 1u)});
    EF_CHECK(none[0].args.count("priority") == 0);
}

EF_TEST(msi_check_roles_map_to_sections_and_ids) {
    const auto v = interrupt_checks({dev(InterruptDeviceRole::GameGpu, true, 1u), dev(InterruptDeviceRole::ActiveNetwork, true, 0u)});
    EF_CHECK(v.size() == 2);
    EF_CHECK(v[0].id == "interrupt.gpu.msi" && v[0].section == Section::Graphics);
    EF_CHECK(v[1].id == "interrupt.net.msi" && v[1].section == Section::Network);
}

EF_TEST(msi_check_has_no_fix) {
    const std::vector<InterruptDeviceInfo> all = {
        dev(InterruptDeviceRole::GameGpu, true, 1u), dev(InterruptDeviceRole::GameGpu, true, 0u, 1u),
        dev(InterruptDeviceRole::ActiveNetwork, false, std::nullopt), dev(InterruptDeviceRole::ActiveNetwork, true, std::nullopt)};
    const auto v = interrupt_checks(all);
    EF_CHECK(v.size() == 4);
    for (const auto& c : v) {
        EF_CHECK(c.action == ActionKind::None && c.action_arg.empty());
        EF_CHECK(!is_auto_fixable(c) && !row_action_shown(c) && !auto_fix_item(c).has_value());
        EF_CHECK(c.level != Level::Advice && c.level != Level::Problem);  // tavsiye degil, yalniz durum
    }
    EF_CHECK(plan_fix_all(v, {}).empty());
}

EF_TEST(msi_check_keys_exist_in_catalog) {
    for (const char* k : {"chk.interrupt.msi_on", "chk.interrupt.msi_on_prio", "chk.interrupt.msi_off", "chk.interrupt.msi_off_prio",
                          "chk.interrupt.no_data", "chk.interrupt.unsupported"})
        EF_CHECK(ef::core::i18n::id_of(k).has_value());
}

EF_TEST(pci_hardware_id_match_is_exact) {
    using ef::platform::readiness::hardware_id_matches_pci;
    EF_CHECK(hardware_id_matches_pci("PCI\\VEN_10DE&DEV_2B85&SUBSYS_00000000&REV_A1", 0x10DE, 0x2B85));
    EF_CHECK(hardware_id_matches_pci("pci\\ven_10de&dev_2b85", 0x10DE, 0x2B85));            // buyuk/kucuk harf duyarsiz
    EF_CHECK(!hardware_id_matches_pci("PCI\\VEN_10DE&DEV_2B85&SUBSYS_0", 0x10DE, 0x2B86));  // aygit farkli
    EF_CHECK(!hardware_id_matches_pci("PCI\\VEN_10DE&DEV_2B85", 0x1002, 0x2B85));           // uretici farkli
    EF_CHECK(!hardware_id_matches_pci("PCI\\VEN_10DE&DEV_2B850", 0x10DE, 0x2B85));          // on ek eslesmesi yok
    EF_CHECK(!hardware_id_matches_pci("USB\\VID_10DE&PID_2B85", 0x10DE, 0x2B85));
    EF_CHECK(!hardware_id_matches_pci("", 0x10DE, 0x2B85));
}
