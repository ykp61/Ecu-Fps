#include "core/vendors/dispatch.h"
#include "ef_test.h"

using namespace ef::core::vendors;
using namespace ef::core::inventory;
using ef::contracts::Field;
using ef::contracts::Provenance;
using ef::contracts::SupportState;

namespace {
GpuAdapter gpu(std::uint32_t idx, std::uint32_t vendor, GpuKind k) {
    GpuAdapter g;
    g.index = idx;
    g.vendor_id = Field<std::uint32_t>::known(vendor, Provenance::Api);
    g.kind = k;
    return g;
}
}  // namespace

EF_TEST(multi_vendor_routing_isolates_failures) {
    // Bu PC: NVIDIA dGPU + AMD iGPU + WARP; Intel runtime yok
    const std::vector<GpuAdapter> gpus{gpu(0, kVendorNvidia, GpuKind::Discrete), gpu(1, kVendorAmd, GpuKind::Integrated),
                                       gpu(2, kVendorMicrosoft, GpuKind::Software), gpu(3, kVendorIntel, GpuKind::Integrated)};
    const std::vector<RuntimeInfo> rts{{Vendor::Nvidia, RuntimeState::Loaded, "32.0.16.2101", "", ""},
                                       {Vendor::Amd, RuntimeState::Untrusted, "", "", "signature invalid"},
                                       {Vendor::Intel, RuntimeState::Missing, "", "", ""}};
    const auto r = route_adapters(gpus, rts);
    EF_CHECK(r.size() == 4);
    EF_CHECK(r[0].vendor == Vendor::Nvidia && r[0].support == SupportState::Supported);
    EF_CHECK(r[1].support == SupportState::Denied);       // guvenilmez runtime: izin reddi ayri
    EF_CHECK(r[2].support == SupportState::Unsupported);  // WARP
    EF_CHECK(r[3].support == SupportState::Unsupported);  // surucu yok: cokme yok, digerleri etkilenmez
    EF_CHECK(r[3].reason == "vendor driver runtime not installed");
}

EF_TEST(runtime_dll_names_and_vendor_ids) {
    EF_CHECK(runtime_dll(Vendor::Nvidia) == L"nvapi64.dll");
    EF_CHECK(runtime_dll(Vendor::Amd) == L"amdadlx64.dll");
    EF_CHECK(runtime_dll(Vendor::Intel) == L"ControlLib.dll");
    EF_CHECK(runtime_dll(Vendor::None).empty());
    EF_CHECK(vendor_of(0x10DE) == Vendor::Nvidia && vendor_of(0x1002) == Vendor::Amd && vendor_of(0x8086) == Vendor::Intel);
    EF_CHECK(vendor_of(0x1414) == Vendor::None);
}

// ---- P9-B8: hibrit GPU kimlik ayrimi ve fail-closed yazma kapisi ----
#include "core/capabilities/registry.h"
#include "core/capabilities/vendor_matrix.h"

namespace {
RuntimeInfo loaded_rt(Vendor v) { return {v, RuntimeState::Loaded, "test", "", ""}; }
const std::vector<RuntimeInfo> kAllLoaded{loaded_rt(Vendor::Nvidia), loaded_rt(Vendor::Amd), loaded_rt(Vendor::Intel)};

std::size_t allowed_count(const std::vector<GpuAdapter>& gpus, std::uint32_t adapter_index, const std::vector<RuntimeInfo>& rts) {
    const auto routes = route_adapters(gpus, rts);
    for (const auto& r : routes) {
        if (r.adapter_index != adapter_index) continue;
        std::size_t n = 0;
        for (const auto& f : ef::core::capabilities::embedded_features())
            if (vendor_driver_write_allowed(r, f.id)) ++n;
        return n;
    }
    return 0;
}
}  // namespace

EF_TEST(hybrid_gpu_identity_follows_pci_vendor_of_the_chosen_adapter_not_order) {
    // Bu PC'nin duzeni: AMD iGPU (0) + NVIDIA dGPU (1). Oyunun adaptoru 0 ise NVIDIA yazma yolu ACILMAZ.
    const std::vector<GpuAdapter> amd_first{gpu(0, kVendorAmd, GpuKind::Integrated), gpu(1, kVendorNvidia, GpuKind::Discrete)};
    EF_CHECK(adapter_vendor(amd_first, 0) == Vendor::Amd);
    EF_CHECK(adapter_vendor(amd_first, 1) == Vendor::Nvidia);
    EF_CHECK(allowed_count(amd_first, 0, kAllLoaded) == 0);  // AMD iGPU: hicbir uretici yazma yolu yok
    EF_CHECK(allowed_count(amd_first, 1, kAllLoaded) == 4);  // NVIDIA: G01 G02 G05 G06 (G07 kapali)
    // Ters sira: kimlik konumdan degil adaptor indeksinden gelir.
    const std::vector<GpuAdapter> nv_first{gpu(0, kVendorNvidia, GpuKind::Discrete), gpu(1, kVendorAmd, GpuKind::Integrated)};
    EF_CHECK(adapter_vendor(nv_first, 0) == Vendor::Nvidia && adapter_vendor(nv_first, 1) == Vendor::Amd);
    EF_CHECK(allowed_count(nv_first, 0, kAllLoaded) == 4 && allowed_count(nv_first, 1, kAllLoaded) == 0);
    // Intel iGPU + NVIDIA dGPU (tipik dizustu)
    const std::vector<GpuAdapter> intel_nv{gpu(0, kVendorIntel, GpuKind::Integrated), gpu(1, kVendorNvidia, GpuKind::Discrete)};
    EF_CHECK(allowed_count(intel_nv, 0, kAllLoaded) == 0 && allowed_count(intel_nv, 1, kAllLoaded) == 4);
    // Indeksler vektor konumuyla ayni olmak zorunda degil.
    const std::vector<GpuAdapter> sparse{gpu(5, kVendorNvidia, GpuKind::Discrete), gpu(2, kVendorAmd, GpuKind::Integrated)};
    EF_CHECK(adapter_vendor(sparse, 5) == Vendor::Nvidia && adapter_vendor(sparse, 2) == Vendor::Amd);
    EF_CHECK(adapter_vendor(sparse, 0) == Vendor::None);  // olmayan indeks "ilk kart" sayilmaz
}

EF_TEST(hybrid_gpu_identity_ignores_names_and_fails_closed_on_unknowns) {
    // Ad NVIDIA diyor ama PCI kimligi AMD: kimlik PCI'dan gelir.
    GpuAdapter spoof = gpu(0, kVendorAmd, GpuKind::Discrete);
    spoof.name = Field<std::string>::known("NVIDIA GeForce RTX 5080", Provenance::Api);
    EF_CHECK(adapter_vendor({spoof}, 0) == Vendor::Amd);
    EF_CHECK(allowed_count({spoof}, 0, kAllLoaded) == 0);
    // Kimlik okunamadi: Vendor::None, yazma yok.
    GpuAdapter unknown;
    unknown.index = 0;
    unknown.kind = GpuKind::Discrete;
    unknown.vendor_id = Field<std::uint32_t>::unknown("DXGI desc unreadable");
    EF_CHECK(adapter_vendor({unknown}, 0) == Vendor::None);
    EF_CHECK(allowed_count({unknown}, 0, kAllLoaded) == 0);
    // Yazilim/uzak adaptor NVIDIA kimligi tasisa bile yazma yok.
    EF_CHECK(adapter_vendor({gpu(0, kVendorNvidia, GpuKind::Software)}, 0) == Vendor::None);
    EF_CHECK(adapter_vendor({gpu(0, kVendorNvidia, GpuKind::Remote)}, 0) == Vendor::None);
    EF_CHECK(allowed_count({gpu(0, kVendorMicrosoft, GpuKind::Software)}, 0, kAllLoaded) == 0);
    // Bos adaptor listesi
    EF_CHECK(adapter_vendor({}, 0) == Vendor::None);
}

EF_TEST(vendor_write_gate_needs_trusted_loaded_runtime_and_a_write_row) {
    const std::vector<GpuAdapter> nv{gpu(0, kVendorNvidia, GpuKind::Discrete)};
    EF_CHECK(allowed_count(nv, 0, {loaded_rt(Vendor::Nvidia)}) == 4);
    EF_CHECK(allowed_count(nv, 0, {}) == 0);  // runtime yok
    EF_CHECK(allowed_count(nv, 0, {{Vendor::Nvidia, RuntimeState::Missing, "", "", ""}}) == 0);
    EF_CHECK(allowed_count(nv, 0, {{Vendor::Nvidia, RuntimeState::Untrusted, "", "", "bad signature"}}) == 0);
    EF_CHECK(allowed_count(nv, 0, {{Vendor::Nvidia, RuntimeState::LoadFailed, "", "", "x"}}) == 0);
    // Satir "yazma" degilse (G07 kapali, H06 okuma, bilinmeyen kimlik) runtime yuklu olsa da kapi kapali.
    const auto routes = route_adapters(nv, {loaded_rt(Vendor::Nvidia)});
    EF_CHECK(vendor_driver_write_allowed(routes[0], "G01"));
    EF_CHECK(!vendor_driver_write_allowed(routes[0], "G07"));
    EF_CHECK(!vendor_driver_write_allowed(routes[0], "H06"));
    EF_CHECK(!vendor_driver_write_allowed(routes[0], "W01"));  // Windows yolu uretici-surucu kapisindan gecmez
    EF_CHECK(!vendor_driver_write_allowed(routes[0], "G03"));  // baska uretici ayari
    EF_CHECK(!vendor_driver_write_allowed(routes[0], "Z99"));
    // Elle uretilmis rota: vendor None veya destek Supported degil -> kapali.
    AdapterRoute forged = routes[0];
    forged.vendor = Vendor::None;
    EF_CHECK(!vendor_driver_write_allowed(forged, "G01"));
    forged = routes[0];
    forged.support = SupportState::Unknown;
    EF_CHECK(!vendor_driver_write_allowed(forged, "G01"));
    forged.support = SupportState::Denied;
    EF_CHECK(!vendor_driver_write_allowed(forged, "G01"));
}

EF_TEST(amd_and_intel_identities_never_enable_any_vendor_write_for_any_runtime_state) {
    for (const auto v : {kVendorAmd, kVendorIntel}) {
        for (const auto st : {RuntimeState::Loaded, RuntimeState::Missing, RuntimeState::Untrusted, RuntimeState::LoadFailed}) {
            const std::vector<GpuAdapter> g{gpu(0, v, GpuKind::Discrete)};
            const auto vendor = adapter_vendor(g, 0);
            const std::vector<RuntimeInfo> rt{{vendor, st, "x", "", ""}};
            EF_CHECK(allowed_count(g, 0, rt) == 0);
            // Entegre kart: kapi yonetici bilgisi tasimaz; yalniz tablo + runtime.
            const std::vector<GpuAdapter> gi{gpu(0, v, GpuKind::Integrated)};
            EF_CHECK(allowed_count(gi, 0, rt) == 0);
        }
    }
}
