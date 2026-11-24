#pragma once
// CHK-03/04 platform: grafik (DXGI + D3D12 ozellik sorgusu + NVAPI VRR) ve yazilim/BIOS okuyuculari.
// SALT OKUMA: ayar yazmaz, yonetici istemez, pencere acmaz. Okunamayan deger nullopt kalir.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/readiness/readiness.h"

namespace ef::platform::readiness {

// NVAPI NV_GET_VRR_INFO ham bitleri (tani/prob icin).
struct NvVrrRaw {
    std::string display;  // GDI adi ("\\.\DISPLAY1")
    bool enabled = false, possible = false, requested = false, indicator = false, in_vrr_mode = false;
};

// Yuksek performansli (yazilim olmayan) adaptor; surucu surum/tarihi SetupAPI'den, D3D12 ozellikleri
// cihaz olusturularak (ozellik duzeyi 11.0 ile) sorgulanir. NVIDIA ise birincil ekranin VRR durumu NVAPI'den.
// raw verilirse ve NVAPI okunabildiyse ham VRR bitleri yazilir.
// D3D12 cihazi olusmazsa D3D11 yedegiyle yalniz d3d11_feature_level doldurulur.
// ENGELLEYICI (D3D cihaz olusturma, SetupAPI, NVAPI yukleme: yuzlerce ms surebilir): UI is parcaciginda
// cagirmayin; arka plan is parcaciginda calistirip sonucu UI'a iletin.
core::readiness::GraphicsInfo read_graphics(std::optional<NvVrrRaw>* raw = nullptr);

// Uninstall kayit defteri (engelleyici; UI is parcacigi disinda cagirin) (HKLM/HKCU, 64/32 bit) + SMBIOS BIOS tarihi ve anakart.
core::readiness::SoftwareInfo read_software();

// Surucusuzluk kaniti (saf): SetupAPI ekran sinifi (DIGCF_PRESENT) aygitlarinin hizmet adlari.
// true yalniz liste bos degilse ve HER aygit "BasicDisplay" hizmetindeyse (Microsoft Temel Goruntu
// Bagdastiricisi). Bos hizmet (surucusu hic yuklenmemis aygit) kanit sayilmaz -> false. Okunamadiysa nullopt.
std::optional<bool> basic_display_only(const std::optional<std::vector<std::string>>& display_services);
// GraphicsInfo::vendor anahtari (saf). hw_vendor_id: secilen yazilim-olmayan DXGI adaptorunun PCI
// uretici kimligi (secilemediyse nullopt). Gercek uretici her zaman kazanir; aksi halde surucusuzluk yalniz
// basic_only == true ise "basic_display"; kanitsiz 0x1414 adaptor "microsoft"; hicbiri yoksa "".
std::string graphics_vendor_key(std::optional<std::uint32_t> hw_vendor_id, std::optional<bool> basic_only);

// Saf yardimcilar (birim testli): SMBIOS "MM/DD/YYYY" (veya "MM/DD/YY") ve Uninstall "YYYYMMDD".
std::optional<std::int64_t> smbios_date_to_unix(std::string_view s);
std::optional<std::int64_t> install_date_to_unix(std::string_view s);
// Uninstall DisplayName -> kanonik urun adi (izlenmeyen urun: bos).
std::string canonical_software_name(std::string_view display_name);
// Noktali surumleri sayisal karsilastirir ("14.9" < "14.10"); sayisal degilse sozluk sirasi.
bool software_version_less(std::string_view a, std::string_view b);
// Ayni kanonik ad zaten varsa daha yuksek surumlu (esitse tarihi olan) kayit kalir; yoksa eklenir.
void merge_software_item(std::vector<core::readiness::SoftwareItem>& items, core::readiness::SoftwareItem it);

}  // namespace ef::platform::readiness
