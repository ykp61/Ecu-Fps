#pragma once
// GPU-01: ayni sistemde cok uretici dagitimi. Her GPU adaptoru kendi ureticisinin resmi runtime'ina
// eslenir; runtime yoksa/guvenilmezse o adaptorun ozellikleri Unsupported/Denied olur, digerleri calisir.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/support.h"
#include "core/inventory/devices.h"

namespace ef::core::vendors {

enum class Vendor { Nvidia, Amd, Intel, None };
std::string_view to_string(Vendor v) noexcept;
Vendor vendor_of(std::uint32_t pci_vendor_id) noexcept;

// Resmi surucu runtime DLL adi (System32). Bos: runtime yok.
std::wstring_view runtime_dll(Vendor v) noexcept;

enum class RuntimeState { Loaded, Missing, Untrusted, LoadFailed };
std::string_view to_string(RuntimeState s) noexcept;

struct RuntimeInfo {
    Vendor vendor = Vendor::None;
    RuntimeState state = RuntimeState::Missing;
    std::string file_version;  // gozlenen surum (tahmin yok)
    std::string path;          // yuklenen gercek yol
    std::string detail;
};

struct AdapterRoute {
    std::uint32_t adapter_index = 0;
    Vendor vendor = Vendor::None;
    contracts::SupportState support = contracts::SupportState::Unknown;
    std::string reason;
};

// Adaptorleri runtime durumuna gore yonlendirir. Yazilim/uzak adaptorler ve bilinmeyen ureticiler
// Unsupported; runtime yok -> Unsupported; guvenilmez -> Denied; yuklu -> Supported (ozellik bazli
// destek yine ilgili adaptor gorevinde sorgulanir).
std::vector<AdapterRoute> route_adapters(const std::vector<inventory::GpuAdapter>& gpus, const std::vector<RuntimeInfo>& runtimes);

// P9-B8: hibrit GPU kimligi. Kimlik YALNIZ secilen bagdastiricinin PCI uretici numarasindan gelir (ad, DXGI sirasi veya
// "ilk kart" kimlik degildir). `adapter_index` GpuAdapter::index'tir (vektor konumu degil). Bulunamayan indeks, okunamayan
// uretici numarasi, yazilim/uzak bagdastirici ve bilinmeyen uretici -> Vendor::None (fail-closed).
Vendor adapter_vendor(const std::vector<inventory::GpuAdapter>& gpus, std::uint32_t adapter_index) noexcept;

// P9-B8: uretici-surucu (NVAPI/ADLX/IGCL) YAZMA yolu bu bagdastirici ve ozellik icin acik mi? Fail-closed: yalniz
// (a) rota Supported (runtime guvenilir ve yuklu), (b) uretici NVIDIA/AMD/Intel'den biri, (c) capabilities/vendor_matrix
// tablosunda bu uretici x ozellik satiri "yazma" + "vendor_driver" kanali. AMD ve Intel icin tabloda boyle satir YOKTUR;
// yonetici hakki, mock Supported sonucu veya yuklu runtime bunu degistirmez. Windows kanalli yazmalar bu kapidan gecmez.
bool vendor_driver_write_allowed(const AdapterRoute& route, std::string_view feature_id) noexcept;

}  // namespace ef::core::vendors
