#pragma once
// SMBIOS ham tablo ayristirici (saf, sinir kontrollu). Kaynak: GetSystemFirmwareTable('RSMB').
// Gizlilik: seri numarasi, UUID (tip 1) ve asset tag okunmaz.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/inventory.h"
#include "contracts/result.h"

namespace ef::core::inventory {

struct SmbiosStructure {
    std::uint8_t type = 0;
    std::uint16_t handle = 0;
    std::string formatted;             // baslik dahil bicimli alan
    std::vector<std::string> strings;  // 1 tabanli string tablosu (strings[0] = #1)

    std::uint8_t byte_at(std::size_t off, bool& ok) const noexcept;
    std::uint16_t word_at(std::size_t off, bool& ok) const noexcept;
    std::uint32_t dword_at(std::size_t off, bool& ok) const noexcept;
    // String indeksi (formatted[off]) ile; 0 veya gecersiz -> bos
    std::string str_at(std::size_t off) const;
};

struct SmbiosTable {
    std::uint8_t major = 0, minor = 0;
    std::vector<SmbiosStructure> structures;
};

// raw: RawSMBIOSData (8 bayt baslik + tablo). Kesik/bozuk girdi kontrollu hata.
contracts::Result<SmbiosTable> parse_smbios(std::string_view raw);

struct MemoryModule {
    contracts::Field<std::uint64_t> size_mib;
    contracts::Field<std::uint32_t> speed_mts;
    contracts::Field<std::uint32_t> configured_mts;
    contracts::Field<std::string> type;          // "DDR5"
    contracts::Field<std::string> manufacturer;
    contracts::Field<std::string> part_number;
    contracts::Field<std::string> locator;
    contracts::Field<std::string> bank_locator;  // tip 17 offset 0x11; kanal izi icin (F-62)
};

struct MemorySmbios {
    std::vector<MemoryModule> populated;
    contracts::Field<std::uint32_t> total_slots;
    contracts::Field<std::uint32_t> populated_slots;
    // Fiziksel kanal sayisi DIMM sayisindan turetilemez: daima gerekceli unknown.
    contracts::Field<std::uint32_t> channel_count;
};

struct BoardSmbios {
    contracts::Field<std::string> board_vendor, board_product;
    contracts::Field<std::string> bios_vendor, bios_version, bios_date;
};

// Sanal makine gostergesi: tip 1 uretici/urun (seri ve UUID okunmaz) ve tip 0 BIOS saglayicisi.
// Bilinen VM imzasi bulunursa true; fiziksel imza veya bilgi yoksa false.
bool smbios_indicates_vm(const SmbiosTable& t);

MemorySmbios decode_memory(const SmbiosTable& t);
BoardSmbios decode_board(const SmbiosTable& t);
std::string memory_type_name(std::uint8_t code);

}  // namespace ef::core::inventory
