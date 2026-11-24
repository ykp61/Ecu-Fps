// A3 (P12 karar 7): melez GPU kapisi. TUM girdiler SAHTE (MOCK): gercek DXGI/NVAPI/DRS okuma ya da yazma yok.
// Kural: hedef adaptor NVIDIA olarak dogrulanmadan NVIDIA surucu profili plana (ve onaya) girmez; neden kayitlidir.
#include <optional>
#include <string>
#include <vector>

#include "core/adapters/nvidia/drs.h"
#include "core/perf/admin_plan.h"
#include "core/perf/controller.h"
#include "ef_test.h"

using namespace ef::core;
using inventory::GpuAdapter;
using inventory::GpuKind;
namespace nv = ef::core::adapters::nvidia;
using ef::contracts::Field;
using ef::contracts::Provenance;

namespace {
GpuAdapter mock_gpu(std::uint32_t index, std::uint32_t vendor, GpuKind kind) {  // MOCK
    GpuAdapter g;
    g.index = index;
    g.vendor_id = Field<std::uint32_t>::known(vendor, Provenance::Api);
    g.kind = kind;
    return g;
}
GpuAdapter mock_gpu_vendor_unread(std::uint32_t index, GpuKind kind) {  // MOCK
    GpuAdapter g;
    g.index = index;
    g.vendor_id = Field<std::uint32_t>::unknown("mock: vendor id not read");
    g.kind = kind;
    return g;
}
// Surucu varsayilanlari hedeften farkli: kapi acik olsa 2 oge (guc + kuyruk) uretilir.
nv::FakeDrs mock_drs() {
    nv::FakeDrs d;
    d.defaults[0x1057EB71] = 5;
    d.defaults[0x007BA09E] = 0;
    d.defaults[0x00AC8497] = 0x4000;
    return d;
}
}  // namespace

EF_TEST(a3_single_nvidia_gate_allows_nvidia_items) {  // MOCK
    const auto gate = perf::nvidia_adapter_gate({mock_gpu(0, inventory::kVendorNvidia, GpuKind::Discrete)}, std::nullopt, true);
    EF_CHECK(gate.allowed() && gate.reason_key.empty() && gate.adapter_index == std::optional<std::uint32_t>(0));
    EF_CHECK(!perf::nvidia_omission(gate).has_value());
    auto d = mock_drs();
    const auto items = perf::nvidia_plan_items(&d, gate);
    EF_CHECK(items.size() == 2);
    for (const auto& i : items) EF_CHECK(i.group() == "plan.nvidia" && i.needs_admin);
}

EF_TEST(a3_hybrid_igpu_dgpu_unknown_game_adapter_drops_nvidia_part) {  // MOCK: sahip PC'sinin sekli (AMD iGPU + NVIDIA dGPU)
    for (const bool nv_first : {true, false}) {  // DXGI sirasi kimlik degil: iki dizilimde de ayni sonuc
        std::vector<GpuAdapter> gpus;
        const auto nvg = mock_gpu(nv_first ? 0 : 1, inventory::kVendorNvidia, GpuKind::Discrete);
        const auto amd = mock_gpu(nv_first ? 1 : 0, inventory::kVendorAmd, GpuKind::Integrated);
        if (nv_first) gpus = {nvg, amd};
        else gpus = {amd, nvg};
        gpus.push_back(mock_gpu(2, inventory::kVendorMicrosoft, GpuKind::Software));  // WARP kimlik sayilmaz
        const auto gate = perf::nvidia_adapter_gate(gpus, std::nullopt, true);
        EF_CHECK(!gate.allowed());
        EF_CHECK(gate.state == perf::GpuGateState::Insufficient);  // veri yetersiz; "desteklenmiyor" DEGIL
        EF_CHECK(gate.reason_key == "plan.nvidia.gate.hybrid_target_unknown");
        const auto om = perf::nvidia_omission(gate);
        EF_CHECK(om.has_value() && om->kind == perf::OmissionKind::Insufficient && om->group_key == "plan.nvidia" &&
                  om->reason_key == gate.reason_key);
        auto d = mock_drs();
        EF_CHECK(perf::nvidia_plan_items(&d, gate).empty());  // plan ogesi yok -> Evet/Hayir onayinda da yok
        perf::Plan p;
        p.items = perf::nvidia_plan_items(&d, gate);
        EF_CHECK(perf::group_field_keys(p, "plan.nvidia").empty());
    }
}

EF_TEST(a3_hybrid_with_verified_target_adapter_follows_target_vendor) {  // MOCK
    const std::vector<GpuAdapter> gpus{mock_gpu(0, inventory::kVendorAmd, GpuKind::Integrated),
                                       mock_gpu(1, inventory::kVendorNvidia, GpuKind::Discrete)};
    auto d = mock_drs();
    const auto on_nv = perf::nvidia_adapter_gate(gpus, std::optional<std::uint32_t>(1), true);  // oyun NVIDIA'da kosuyor
    EF_CHECK(on_nv.allowed() && on_nv.adapter_index == std::optional<std::uint32_t>(1));
    EF_CHECK(perf::nvidia_plan_items(&d, on_nv).size() == 2);
    const auto on_amd = perf::nvidia_adapter_gate(gpus, std::optional<std::uint32_t>(0), true);  // oyun AMD iGPU'da
    EF_CHECK(!on_amd.allowed() && on_amd.state == perf::GpuGateState::Unsupported && on_amd.reason_key == "plan.nvidia.gate.target_not_nvidia");
    EF_CHECK(perf::nvidia_plan_items(&d, on_amd).empty());
    const auto bad_idx = perf::nvidia_adapter_gate(gpus, std::optional<std::uint32_t>(7), true);  // olmayan indeks "ilk kart" sayilmaz
    EF_CHECK(!bad_idx.allowed() && bad_idx.state == perf::GpuGateState::Insufficient && bad_idx.reason_key == "plan.nvidia.gate.target_not_found");
}

EF_TEST(a3_amd_only_and_intel_only_have_no_nvidia_part_unsupported) {  // MOCK
    for (const auto vendor : {inventory::kVendorAmd, inventory::kVendorIntel}) {
        const auto gate = perf::nvidia_adapter_gate({mock_gpu(0, vendor, GpuKind::Integrated)}, std::nullopt, true);
        EF_CHECK(!gate.allowed() && gate.state == perf::GpuGateState::Unsupported && gate.reason_key == "plan.nvidia.gate.no_nvidia_adapter");
        const auto om = perf::nvidia_omission(gate);
        EF_CHECK(om.has_value() && om->kind == perf::OmissionKind::Unsupported);
        auto d = mock_drs();  // surucu okunabilse bile (baska arac/sahte) plan ogesi yok
        EF_CHECK(perf::nvidia_plan_items(&d, gate).empty());
    }
    // AMD + Intel (NVIDIA hic yok), hedef bilinmese de kesin "desteklenmiyor"
    const auto two = perf::nvidia_adapter_gate({mock_gpu(0, inventory::kVendorAmd, GpuKind::Integrated), mock_gpu(1, inventory::kVendorIntel, GpuKind::Integrated)},
                                               std::nullopt, true);
    EF_CHECK(two.state == perf::GpuGateState::Unsupported);
}

EF_TEST(a3_uncertain_inputs_are_insufficient_with_recorded_reason) {  // MOCK
    // Adaptor listesi okunamadi.
    const auto none = perf::nvidia_adapter_gate({}, std::nullopt, true);
    EF_CHECK(!none.allowed() && none.state == perf::GpuGateState::Insufficient && none.reason_key == "plan.nvidia.gate.adapters_unread");
    // Tek adaptor, ureticisi okunamadi: NVIDIA oldugu kanitlanamaz.
    const auto unread = perf::nvidia_adapter_gate({mock_gpu_vendor_unread(0, GpuKind::Discrete)}, std::nullopt, true);
    EF_CHECK(!unread.allowed() && unread.state == perf::GpuGateState::Insufficient && unread.reason_key == "plan.nvidia.gate.vendor_unread");
    // Bir adaptorun ureticisi okunamadi, digeri AMD: NVIDIA olmadigi kanitlanamaz -> yetersiz, "desteklenmiyor" degil.
    const auto mixed = perf::nvidia_adapter_gate({mock_gpu(0, inventory::kVendorAmd, GpuKind::Integrated), mock_gpu_vendor_unread(1, GpuKind::Discrete)},
                                                 std::nullopt, true);
    EF_CHECK(mixed.state == perf::GpuGateState::Insufficient && mixed.reason_key == "plan.nvidia.gate.vendor_unread");
    // Yalniz yazilim adaptoru: donanim GPU yok -> desteklenmiyor.
    const auto warp = perf::nvidia_adapter_gate({mock_gpu(0, inventory::kVendorMicrosoft, GpuKind::Software)}, std::nullopt, true);
    EF_CHECK(warp.state == perf::GpuGateState::Unsupported && warp.reason_key == "plan.nvidia.gate.no_hardware_gpu");
    // Hedef olarak yazilim adaptoru gosterilirse: desteklenmiyor.
    const auto sw_target = perf::nvidia_adapter_gate({mock_gpu(0, inventory::kVendorNvidia, GpuKind::Discrete), mock_gpu(1, inventory::kVendorMicrosoft, GpuKind::Software)},
                                                     std::optional<std::uint32_t>(1), true);
    EF_CHECK(sw_target.state == perf::GpuGateState::Unsupported && sw_target.reason_key == "plan.nvidia.gate.target_not_hardware");
    for (const auto* g : {&none, &unread, &mixed, &warp, &sw_target}) {
        EF_CHECK(!g->reason_key.empty());  // sessiz dusme yok: her kapali kapi neden tasir
        EF_CHECK(perf::nvidia_omission(*g).has_value());
    }
}

EF_TEST(a3_nvidia_runtime_missing_is_unsupported_even_for_single_nvidia) {  // MOCK
    const auto gate = perf::nvidia_adapter_gate({mock_gpu(0, inventory::kVendorNvidia, GpuKind::Discrete)}, std::nullopt, false);
    EF_CHECK(!gate.allowed() && gate.state == perf::GpuGateState::Unsupported && gate.reason_key == "plan.nvidia.gate.driver_runtime");
    auto d = mock_drs();
    EF_CHECK(perf::nvidia_plan_items(&d, gate).empty());
}

EF_TEST(a3_gate_uses_vendor_driver_write_allowed_not_admin_or_runtime_alone) {  // MOCK
    // Tablo AMD/Intel icin vendor_driver yazma satiri icermez: runtime "yuklu" gorunse de hedef AMD ise kapi acilmaz.
    const auto amd_target = perf::nvidia_adapter_gate({mock_gpu(0, inventory::kVendorAmd, GpuKind::Integrated), mock_gpu(1, inventory::kVendorNvidia, GpuKind::Discrete)},
                                                      std::optional<std::uint32_t>(0), true);
    EF_CHECK(!amd_target.allowed());
    // Kapinin kullandigi ozellikler gercekten NVIDIA yazma kapisinda acik (tablo ile tutarli).
    vendors::AdapterRoute r;
    r.vendor = vendors::Vendor::Nvidia;
    r.support = ef::contracts::SupportState::Supported;
    EF_CHECK(vendors::vendor_driver_write_allowed(r, nv::catalog_id(nv::DrsSetting::PowerMode)));
    EF_CHECK(vendors::vendor_driver_write_allowed(r, nv::catalog_id(nv::DrsSetting::MaxPrerendered)));
}

// ---- B6 (ADR-040 m.8): ek NVIDIA ogeleri (plan.nvidia_extra). Tum girdiler SAHTE (MOCK). ----
namespace {
nv::FakeDrs mock_drs_extra() {  // MOCK: G01/G02 zaten hedefte; is parcacikli iyilestirme varsayilan (0), FXAA acik (1)
    nv::FakeDrs d;
    d.defaults[0x1057EB71] = 1;
    d.defaults[0x007BA09E] = 1;
    d.defaults[0x00AC8497] = 0x4000;
    d.defaults[0x20C1221E] = 0;
    d.global[0x1074C972] = 1;
    return d;
}
constexpr perf::NvidiaExtraFlags kExtraOn{true, true};
}  // namespace

EF_TEST(drs_new_items_when_enabled) {  // MOCK
    const auto gate = perf::nvidia_adapter_gate({mock_gpu(0, inventory::kVendorNvidia, GpuKind::Discrete)}, std::nullopt, true);
    auto d = mock_drs_extra();
    EF_CHECK(perf::nvidia_plan_items(&d, gate).empty());  // urun yolu: bayraklar kapali
    const auto items = perf::nvidia_plan_items(&d, gate, kExtraOn);
    EF_CHECK(items.size() == 2);
    for (const auto& i : items) {
        EF_CHECK(i.group() == "plan.nvidia_extra" && i.needs_admin && i.op.scope == ef::contracts::OperationScope::Machine);
        EF_CHECK(i.op.id == ef::contracts::OperationId::VendorProfileValue && !i.op.needs_reboot);
    }
    if (items.size() == 2) {
        EF_CHECK(items[0].op.target == "drs:global:20C1221E" && items[0].op.new_value == "00000001" && items[0].field_key == "plan.field.nv_threaded");
        EF_CHECK(items[1].op.target == "drs:global:1074C972" && items[1].op.new_value == "00000000" && items[1].field_key == "plan.field.nv_fxaa");
    }
    // Yalniz hedeften farkli: zaten acik is parcacikli iyilestirme ve kapali/bilinmeyen FXAA oge uretmez.
    d.global[0x20C1221E] = 1;
    d.global[0x1074C972] = 0;
    EF_CHECK(perf::nvidia_plan_items(&d, gate, kExtraOn).empty());
    d.global[0x1074C972] = 5;  // belgede olmayan deger: dokunulmaz
    EF_CHECK(perf::nvidia_plan_items(&d, gate, kExtraOn).empty());
    d.global[0x20C1221E] = 2;  // kullanici kapatmis (DISABLE): tek oge
    const auto one = perf::nvidia_plan_items(&d, gate, kExtraOn);
    EF_CHECK(one.size() == 1 && one[0].op.target == "drs:global:20C1221E");
    // Ayar basina bayrak: yalniz FXAA acik
    d.global[0x1074C972] = 1;
    const auto fx = perf::nvidia_plan_items(&d, gate, perf::NvidiaExtraFlags{false, true});
    EF_CHECK(fx.size() == 1 && fx[0].op.target == "drs:global:1074C972");
}

EF_TEST(drs_new_items_respect_gate) {  // MOCK: sahip PC'sinin sekli (AMD iGPU + NVIDIA dGPU), oyun adaptoru bilinmiyor
    const std::vector<GpuAdapter> gpus{mock_gpu(0, inventory::kVendorAmd, GpuKind::Integrated),
                                       mock_gpu(1, inventory::kVendorNvidia, GpuKind::Discrete)};
    auto d = mock_drs_extra();
    for (const auto& gate : {perf::nvidia_adapter_gate(gpus, std::nullopt, true), perf::nvidia_adapter_gate(gpus, 0u, true),
                             perf::nvidia_adapter_gate({mock_gpu(0, inventory::kVendorNvidia, GpuKind::Discrete)}, std::nullopt, false)}) {
        EF_CHECK(!gate.allowed());
        EF_CHECK(perf::nvidia_plan_items(&d, gate, kExtraOn).empty());  // bayrak acik olsa bile kapi kapali -> oge yok
        const auto om = perf::nvidia_omission(gate);
        EF_CHECK(om.has_value() && !om->reason_key.empty());
    }
    // Oyun AMD iGPU'da: kesin "desteklenmiyor"
    const auto amd = perf::nvidia_adapter_gate(gpus, 0u, true);
    EF_CHECK(amd.state == perf::GpuGateState::Unsupported && amd.reason_key == "plan.nvidia.gate.target_not_nvidia");
    EF_CHECK(perf::nvidia_plan_items(nullptr, perf::nvidia_adapter_gate(gpus, 1u, true), kExtraOn).empty());  // DRS yok
}