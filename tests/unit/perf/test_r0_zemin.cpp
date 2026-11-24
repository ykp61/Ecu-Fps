// ADR-040 / B0 (zemin): yeni islem kimligi, broker izin satiri (fail-closed), bes yeni plan grubu ve metinleri,
// TweakRuntime imzasi. Bu dalda hicbir yeni oge plana girmez; testler bunu sabitler.
#include <string>
#include <string_view>

#include "contracts/models.h"
#include "core/adapters/windows/boot_options.h"
#include "core/adapters/windows/tweak_catalog.h"
#include "core/broker/admin.h"
#include "core/i18n/catalog.h"
#include "core/perf/controller.h"
#include "ef_test.h"

using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;
namespace cw = ef::core::adapters::windows;
namespace i18n = ef::core::i18n;
using namespace ef::core::perf;

namespace {
constexpr std::string_view kNewGroups[] = {"plan.power_usb", "plan.foreground_boost", "plan.svchost_split", "plan.boot_tick",
                                           "plan.nvidia_extra"};

const PlanGroup* find_group(std::string_view key) {
    for (const auto& g : plan_groups())
        if (g.key == key) return &g;
    return nullptr;
}

SettingOperation boot_op(const char* target, const char* value, OperationScope sc = OperationScope::Machine) {
    return SettingOperation{OperationId::BootOptionSet, sc, target, value, false, false};
}

bool has_text(const i18n::Catalog& c, std::string_view key) {
    const auto id = i18n::id_of(key);
    return id && !c.get(*id).empty();
}
}  // namespace

EF_TEST(boot_option_admin_denied_by_default) {
    // Bayrak kapali: yeni yazma (Apply/Fix) hicbir boot hedefi icin kabul edilmez.
    static_assert(!cw::kBootOptionItemEnabled, "ADR-040 m.4: VM tam tur kaniti gelene kadar kapali");
    EF_CHECK(!ef::core::broker::admin_apply_allowed(boot_op("boot:dynamictick", "off")));
    // B4: cift tanimli; bicim denetimi kayitli geri alma istegini kabul eder (yeni yazma yukarida reddedildi).
    EF_CHECK(ef::core::broker::admin_operation_allowed(boot_op("boot:dynamictick", "off")));
    // Tablo disi hedef/deger/kapsam hicbir zaman kabul edilmez (B4 sonrasi da).
    EF_CHECK(!ef::core::broker::admin_operation_allowed(boot_op("boot:dynamictick", "on")));
    EF_CHECK(!ef::core::broker::admin_operation_allowed(boot_op("boot:hpet", "off")));
    EF_CHECK(!ef::core::broker::admin_operation_allowed(boot_op("boot:dynamictick", "off", OperationScope::CurrentUser)));
    EF_CHECK(cw::boot_option_admin_allowed(boot_op("boot:dynamictick", "off"), true));  // B4: tek izinli cift
    EF_CHECK(!cw::boot_option_admin_allowed(boot_op("boot:dynamictick", "off"), false));
}

EF_TEST(operation_id_boot_option_roundtrip) {
    EF_CHECK(ef::contracts::to_string(OperationId::BootOptionSet) == "boot_option_set");
    EF_CHECK(ef::contracts::parse_operation_id(ef::contracts::to_string(OperationId::BootOptionSet)) == OperationId::BootOptionSet);
    // Enum SONUNA eklendi: journal'daki eski kimliklerin sayisal sirasi degismez.
    static_assert(static_cast<int>(OperationId::BootOptionSet) == static_cast<int>(OperationId::AppxPackageRemove) + 1,
                  "BootOptionSet enum sonunda olmali");
}

EF_TEST(new_groups_have_strings) {
    for (auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        auto cat = i18n::Catalog::embedded(loc);
        EF_CHECK(cat.ok());
        if (!cat.ok()) continue;
        for (auto key : kNewGroups) {
            const auto* g = find_group(key);
            EF_CHECK(g != nullptr);
            if (!g) continue;
            EF_CHECK(has_text(cat.value(), g->key));
            EF_CHECK(g->desc_key == std::string(key) + ".desc");
            EF_CHECK(has_text(cat.value(), g->desc_key));
        }
        EF_CHECK(has_text(cat.value(), "history.op.boot_option_set"));
    }
}

EF_TEST(new_groups_default_rule) {
    // ADR-035 m.5: Evet yalniz kanit Olculdu/Belgeli VE risk Dusuk ise.
    for (auto key : kNewGroups) {
        const auto* g = find_group(key);
        EF_CHECK(g != nullptr);
        if (!g) continue;
        EF_CHECK(g->default_on == (g->evidence != Evidence::Disputed && g->risk == Risk::Low));
    }
    const auto* usb = find_group("plan.power_usb");
    const auto* fg = find_group("plan.foreground_boost");
    const auto* svc = find_group("plan.svchost_split");
    const auto* boot = find_group("plan.boot_tick");
    const auto* nv = find_group("plan.nvidia_extra");
    // B1 karari: S109 (surucu belgesi) USB secici askiyi kapatmamayi onerir -> Tartismali, varsayilan Hayir.
    EF_CHECK(usb && !usb->default_on && usb->evidence == Evidence::Disputed && usb->risk == Risk::Low);
    EF_CHECK(fg && !fg->default_on && fg->evidence == Evidence::Disputed && fg->risk == Risk::Low);
    EF_CHECK(svc && !svc->default_on && svc->evidence == Evidence::Disputed && svc->risk == Risk::Medium);
    EF_CHECK(boot && !boot->default_on && boot->evidence == Evidence::Disputed && boot->risk == Risk::High);
    EF_CHECK(nv && !nv->default_on && nv->evidence == Evidence::Documented && nv->risk == Risk::Medium);
}

EF_TEST(new_groups_hidden_without_items) {
    // Panel 0.9.2 gorunumu degismez: yeni gruplar ogesi yokken Ayrintilar listesinde de satir almaz.
    for (auto key : kNewGroups) {
        const auto* g = find_group(key);
        EF_CHECK(g && !g->listed_when_empty);
        if (!g) continue;
        EF_CHECK(!group_listed(*g, false));
        EF_CHECK(group_listed(*g, true));
    }
    // Mevcut gruplar eskisi gibi her zaman listelenir.
    for (const auto& g : plan_groups()) {
        bool is_new = false;
        for (auto key : kNewGroups) is_new = is_new || g.key == key;
        if (!is_new) EF_CHECK(g.listed_when_empty && group_listed(g, false));
    }
}

EF_TEST(admin_needs_reboot_comes_from_table_not_client) {
    namespace br = ef::core::broker;
    EF_CHECK(br::admin_needs_reboot(SettingOperation{OperationId::HagsSet, OperationScope::Machine, "hags:hwschmode", "2", false, false}));
    EF_CHECK(br::admin_needs_reboot(boot_op("boot:dynamictick", "off")));
    // Istemci needs_reboot=true dese de tablo karar verir (guc alt ayari reboot istemez).
    EF_CHECK(!br::admin_needs_reboot(SettingOperation{OperationId::PowerSettingSet, OperationScope::Machine, "power:boost:ac", "1", true, false}));
    // Mevcut katalog girdileri reboot istemez (B3 ilk needs_reboot girdisini ekler).
    for (const auto& e : cw::tweak_catalog())
        EF_CHECK(!br::admin_needs_reboot(SettingOperation{OperationId::TweakValueSet, OperationScope::Machine,
                                                           std::string(cw::kTweakTargetPrefix) + std::string(e.id), std::string(e.on_value),
                                                           false, false}));
}

EF_TEST(tweak_runtime_signature_keeps_fixed_behaviour) {
    // B3'e kadar davranis ayni: bos TweakRuntime ile 3 parametreli imza eski sonucu verir; mevcut girdiler reboot istemez.
    const cw::TweakRuntime rt{};
    for (const auto& e : cw::tweak_catalog()) {
        EF_CHECK(!e.needs_reboot);
        for (std::string_view v : {e.on_value, std::string_view("0"), std::string_view("1"), std::string_view("")})
            EF_CHECK(cw::tweak_admin_allowed(&e, v, rt) == cw::tweak_admin_allowed(&e, v));
    }
    EF_CHECK(!cw::tweak_admin_allowed(nullptr, "1", rt));
}
