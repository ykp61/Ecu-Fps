// F-62: RAM yuva/kanal hazirlik kurallari (saf; Win32 cagrisi yok). Fiziksel kanal sayisi
// DIMM sayisindan kesinlestirilemez; SMBIOS etiketlerinden turetilen sonuc olasilik olarak sunulur.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "contracts/models.h"
#include "core/i18n/catalog.h"
#include "core/maintenance/image_health.h"
#include "core/maintenance/standby_purge.h"
#include "core/readiness/fix_plan.h"
#include "core/readiness/readiness.h"
#include "ef_test.h"

using namespace ef::core::readiness;

namespace {

std::optional<Check> find(const std::vector<Check>& v, const std::string& id) {
    for (const auto& c : v)
        if (c.id == id) return c;
    return std::nullopt;
}

MemoryModuleInfo mod(std::uint64_t size_mib, std::uint32_t mts, std::string locator, std::string bank = {}) {
    MemoryModuleInfo m;
    m.size_mib = size_mib;
    m.configured_mts = mts;
    m.locator = std::move(locator);
    m.bank_locator = std::move(bank);
    return m;
}

MemoryLayoutInfo layout(std::uint32_t total, std::vector<MemoryModuleInfo> modules) {
    MemoryLayoutInfo m;
    m.total_slots = total;
    m.populated_slots = static_cast<std::uint32_t>(modules.size());
    m.modules = std::move(modules);
    return m;
}

}  // namespace

EF_TEST(memory_unknown_makes_no_claim) {
    EF_CHECK(memory_layout_checks(MemoryLayoutInfo{}).empty());
    MemoryLayoutInfo m;
    m.total_slots = 4;
    EF_CHECK(memory_layout_checks(m).empty());
}

EF_TEST(memory_vm_makes_no_claim) {
    auto m = layout(4, {mod(8192, 6000, "DIMM_A1")});
    m.virtual_machine = true;
    EF_CHECK(memory_layout_checks(m).empty());
}

EF_TEST(memory_single_module_on_multi_slot_advises_dual_channel) {
    const auto v = memory_layout_checks(layout(4, {mod(16384, 6000, "DIMM_A1")}));
    EF_CHECK(v.size() == 1);
    const auto c = find(v, "memory.single_module");
    EF_CHECK(c && c->level == Level::Advice && c->section == Section::Hardware);
    EF_CHECK(c && c->text_key == "chk.memory.single_module");
    EF_CHECK(c && c->args.at("populated") == "1" && c->args.at("total") == "4");
    EF_CHECK(c && c->action == ActionKind::None);
    EF_CHECK(ef::core::i18n::id_of("chk.memory.single_module").has_value());
}

EF_TEST(memory_single_module_on_single_slot_is_info) {
    const auto v = memory_layout_checks(layout(1, {mod(8192, 4800, "DIMM0")}));
    EF_CHECK(v.size() == 1);
    const auto c = find(v, "memory.single_slot");
    EF_CHECK(c && c->level == Level::Info && c->text_key == "chk.memory.single_slot");
    EF_CHECK(!find(v, "memory.single_module"));
    EF_CHECK(ef::core::i18n::id_of("chk.memory.single_slot").has_value());
}

EF_TEST(memory_distinct_channel_labels_report_dual_channel_likely) {
    const auto v = memory_layout_checks(
        layout(4, {mod(8192, 6000, "DIMM_A1"), mod(8192, 6000, "DIMM_B1")}));
    EF_CHECK(v.size() == 1);
    const auto c = find(v, "memory.dual_channel");
    EF_CHECK(c && c->level == Level::Info && c->text_key == "chk.memory.dual_channel");
    EF_CHECK(c && c->args.at("labels") == "A, B");
    EF_CHECK(!find(v, "memory.same_channel"));
    EF_CHECK(ef::core::i18n::id_of("chk.memory.dual_channel").has_value());
}

EF_TEST(memory_same_channel_labels_advise_checking_slots) {
    const auto v = memory_layout_checks(
        layout(4, {mod(8192, 6000, "DIMM_A1"), mod(8192, 6000, "DIMM_A2")}));
    EF_CHECK(v.size() == 1);
    const auto c = find(v, "memory.same_channel");
    EF_CHECK(c && c->level == Level::Advice && c->text_key == "chk.memory.same_channel");
    EF_CHECK(c && c->args.at("label") == "A");
    EF_CHECK(ef::core::i18n::id_of("chk.memory.same_channel").has_value());
}

EF_TEST(memory_numeric_dimm_labels_make_no_channel_claim) {
    const auto v = memory_layout_checks(
        layout(4, {mod(8192, 6000, "DIMM0"), mod(8192, 6000, "DIMM1")}));
    EF_CHECK(v.empty());
}

EF_TEST(memory_bank_locator_resolves_ambiguous_device_labels) {
    const auto v = memory_layout_checks(layout(
        4, {mod(8192, 6000, "DIMM0", "Channel A"), mod(8192, 6000, "DIMM1", "Channel B")}));
    EF_CHECK(v.size() == 1);
    const auto c = find(v, "memory.dual_channel");
    EF_CHECK(c && c->args.at("labels") == "A, B");
}

EF_TEST(memory_size_mismatch_advises_matched_pair) {
    const auto v = memory_layout_checks(
        layout(4, {mod(8192, 6000, "DIMM_A1"), mod(16384, 6000, "DIMM_B1")}));
    const auto c = find(v, "memory.mismatch");
    EF_CHECK(c && c->level == Level::Advice && c->text_key == "chk.memory.mismatch");
    EF_CHECK(c && c->action == ActionKind::None);
    EF_CHECK(ef::core::i18n::id_of("chk.memory.mismatch").has_value());
}

EF_TEST(memory_speed_mismatch_advises_matched_pair) {
    const auto v = memory_layout_checks(
        layout(4, {mod(8192, 6000, "DIMM_A1"), mod(8192, 5600, "DIMM_B1")}));
    EF_CHECK(find(v, "memory.mismatch"));
}

EF_TEST(memory_unknown_speed_does_not_claim_mismatch) {
    auto b = mod(8192, 6000, "DIMM_B1");
    b.configured_mts.reset();
    const auto v = memory_layout_checks(layout(4, {mod(8192, 6000, "DIMM_A1"), b}));
    EF_CHECK(!find(v, "memory.mismatch"));
}

EF_TEST(memory_partial_population_count_makes_no_single_claim) {
    MemoryLayoutInfo m = layout(4, {mod(8192, 6000, "DIMM_A1")});
    m.populated_slots = 2;
    EF_CHECK(memory_layout_checks(m).empty());
}

EF_TEST(memory_four_dimms_two_channels_reports_likely_dual_channel) {
    const auto v = memory_layout_checks(layout(
        4, {mod(8192, 6000, "DIMM_A1"), mod(8192, 6000, "DIMM_A2"), mod(8192, 6000, "DIMM_B1"),
            mod(8192, 6000, "DIMM_B2")}));
    EF_CHECK(v.size() == 1);
    const auto c = find(v, "memory.dual_channel");
    EF_CHECK(c && c->args.at("labels") == "A, B");
}

EF_TEST(memory_never_produces_autofix) {
    const auto v = memory_layout_checks(layout(
        4, {mod(8192, 6000, "DIMM_A1"), mod(16384, 5600, "DIMM_A2")}));
    EF_CHECK(!v.empty());
    for (const auto& c : v) EF_CHECK(c.action != ActionKind::AutoFix);
}

// ---- B5 (ADR-040 m.6): bekleme belleginde esik bulgusu. Esik: bekleme > toplamin yarisi VE bos < toplamin onda biri. ----
namespace {
constexpr std::uint64_t kTotal = 17179869184ull;  // 16 GiB (olculen deger gibi; testte sabit)

MemoryPressureInfo pressure(std::optional<std::uint64_t> total, std::optional<std::uint64_t> standby, std::optional<std::uint64_t> freeb) {
    MemoryPressureInfo m;
    m.total_bytes = total;
    m.standby_bytes = standby;
    m.free_bytes = freeb;
    return m;
}
}  // namespace

EF_TEST(standby_threshold_boundary) {
    const std::uint64_t half = kTotal / 2;
    const std::uint64_t low_free = 1717986918ull;  // *10 < kTotal (tam alt sinir)
    EF_CHECK(standby_pressure_checks(pressure(kTotal, half + 1, low_free)).size() == 1);   // tam ustu: bulgu
    EF_CHECK(standby_pressure_checks(pressure(kTotal, half, low_free)).empty());           // tam esik: bulgu yok (siki buyuk)
    EF_CHECK(standby_pressure_checks(pressure(kTotal, half - 1, low_free)).empty());       // tam alti
    EF_CHECK(standby_pressure_checks(pressure(kTotal, half + 1, low_free + 1)).empty());   // bos yeterli (>= onda bir): yok
    EF_CHECK(standby_pressure_checks(pressure(kTotal, half + 1, 0)).size() == 1);          // bos 0 olculdu: gecerli deger
}

EF_TEST(standby_unreadable_or_invalid_gives_no_finding) {
    const std::uint64_t half = kTotal / 2;
    EF_CHECK(standby_pressure_checks(MemoryPressureInfo{}).empty());
    EF_CHECK(standby_pressure_checks(pressure(std::nullopt, half + 1, 0)).empty());
    EF_CHECK(standby_pressure_checks(pressure(kTotal, std::nullopt, 0)).empty());
    EF_CHECK(standby_pressure_checks(pressure(kTotal, half + 1, std::nullopt)).empty());
    EF_CHECK(standby_pressure_checks(pressure(0, 1, 0)).empty());  // toplam 0: anlamsiz olcum
}

EF_TEST(standby_finding_shape_and_fix_mapping) {
    const auto v = standby_pressure_checks(pressure(kTotal, kTotal / 2 + 1, 100));
    EF_CHECK(v.size() == 1);
    const auto& c = v[0];
    EF_CHECK(c.id == "memory.standby_pressure" && c.text_key == "chk.memory.standby_pressure");
    EF_CHECK(c.level == Level::Advice && c.action == ActionKind::AutoFix && c.action_arg == "fix.standby_purge");
    EF_CHECK(ef::core::i18n::id_of(c.text_key).has_value());
    // "Duzelt" gorunur ama toplu (ayar) duzeltmesine girmez: tek Evet/Hayir bakim isi.
    EF_CHECK(row_action_shown(c) && !is_auto_fixable(c) && plan_fix_all(v, {}).empty());
}

EF_TEST(standby_fix_maps_to_maintenance_job) {
    const auto v = standby_pressure_checks(pressure(kTotal, kTotal / 2 + 1, 100));
    EF_CHECK(v.size() == 1);
    const auto op = maintenance_job_operation(v[0]);
    EF_CHECK(op.has_value());
    EF_CHECK(op->id == ef::contracts::OperationId::MaintenanceJob);
    EF_CHECK(op->scope == ef::contracts::OperationScope::Machine);
    EF_CHECK(op->target == ef::core::maintenance::kStandbyPurgeTarget);
    EF_CHECK(op->new_value == ef::core::maintenance::kMaintenanceValue);
    // Baska bulgu / baska arguman is uretmez (hedef tablodan; serbest metin yok).
    Check other = v[0];
    other.action_arg = "fix.standby_purge2";
    EF_CHECK(!maintenance_job_operation(other).has_value());
    EF_CHECK(!maintenance_job_operation(Check{}).has_value());
}

EF_TEST(checks_carry_no_numbers_except_measured) {
    const auto v = standby_pressure_checks(pressure(kTotal, 10ull * 1073741824ull, 1073741824ull));
    EF_CHECK(v.size() == 1);
    const auto& a = v[0].args;
    EF_CHECK(a.size() == 3);  // yalniz olculen uc bayt degeri (GiB, bir basamak); esik / yuzde / oneri sayisi yok
    EF_CHECK(a.at("standby_gib") == "10.0" && a.at("free_gib") == "1.0" && a.at("total_gib") == "16.0");
}

EF_TEST(standby_pressure_replaces_generic_standby_info) {
    std::vector<Check> v;
    Check info;
    info.id = "system.standby";
    v.push_back(info);
    drop_superseded_standby_info(v);  // esik bulgusu yok: genel "normal" bilgisi kalir
    EF_CHECK(v.size() == 1);
    Check press;
    press.id = "memory.standby_pressure";
    v.push_back(press);
    drop_superseded_standby_info(v);  // ikisi birden celisir: yalniz esik bulgusu kalir
    EF_CHECK(v.size() == 1 && v[0].id == "memory.standby_pressure");
}
