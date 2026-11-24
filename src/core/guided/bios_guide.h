#pragma once
// T20: BIOS/UEFI bellek profili (XMP / EXPO / DOCP) icin uretici bazli, yalniz OKUNUR adim adim rehber.
//  - Uygulama firmware'e hicbir sey yazmaz ve BIOS'a girmez; adimlari kullanici kendisi uygular (ADR-035 madde 7: BIOS
//    yazimi ertelendi). Saf: girdi yalniz SMBIOS taban karti uretici metni ve islemci saticisidir.
//  - Menu adlari BIOS surumune gore degisir; metinler "genellikle" der, kesin menu yolu iddia etmez.
//  - Dogrulama adimi yeniden baslatmadan sonra yapilandirilmis bellek hizinin yeniden okunmasina dayanir
//    (catalog::xmp_after_restart); uygulama sonucu tahmin etmez.

#include <string>
#include <string_view>
#include <vector>

namespace ef::core::guided {

struct BiosGuide {
    std::string vendor_key;          // "asus" | "msi" | "gigabyte" | "asrock" | "oem" | "generic"
    std::string enter_key_text_key;  // BIOS'a giris tusu metni
    std::string profile_name_key;    // profilin bu satici/islemcideki adi (XMP / EXPO / DOCP ...)
    std::vector<std::string> step_keys;  // sirali adimlar; hepsi metin katalogunda var
};

// board_manufacturer: SMBIOS taban karti ureticisi ("ASUSTeK COMPUTER INC." ...; buyuk/kucuk harf fark etmez).
// cpu_vendor: "GenuineIntel" / "AuthenticAMD" / "Intel" / "AMD" (bilinmiyorsa bos).
BiosGuide bios_guide_for(std::string_view board_manufacturer, std::string_view cpu_vendor);

// Vendor adi metin anahtari (bios.guide.vendor.<vendor_key>).
std::string bios_vendor_name_key(const BiosGuide& g);

}  // namespace ef::core::guided
