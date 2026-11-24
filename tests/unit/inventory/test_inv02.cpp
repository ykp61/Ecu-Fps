#include <cstdint>
#include <string>

#include "core/inventory/cpu_topology.h"
#include "core/inventory/smbios.h"
#include "ef_test.h"

using namespace ef::core::inventory;
using ef::contracts::ErrorCode;

namespace {
// Sentetik SMBIOS yapisi olusturucu.
struct Builder {
    std::string table;
    void add(std::uint8_t type, std::string formatted_tail, std::initializer_list<const char*> strings) {
        std::string s;
        s += static_cast<char>(type);
        s += static_cast<char>(4 + formatted_tail.size());
        s += '\x01';
        s += '\x00';
        s += formatted_tail;
        if (strings.size() == 0) {
            s += std::string(2, '\0');
        } else {
            for (const char* str : strings) { s += str; s += '\0'; }
            s += '\0';
        }
        table += s;
    }
    std::string raw() const {
        std::string r(8, '\0');
        r[1] = 3; r[2] = 6;
        const auto n = static_cast<std::uint32_t>(table.size());
        for (int i = 0; i < 4; ++i) r[4 + static_cast<std::size_t>(i)] = static_cast<char>((n >> (8 * i)) & 0xFF);
        return r + table;
    }
};
std::string le16(std::uint16_t v) { return {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)}; }
std::string le32(std::uint32_t v) { return le16(static_cast<std::uint16_t>(v & 0xFFFF)) + le16(static_cast<std::uint16_t>(v >> 16)); }

// Tip 17 bicimli alan (0x04'ten itibaren), 0x5C uzunluga kadar.
std::string type17(std::uint16_t size, std::uint8_t mtype, std::uint16_t speed, std::uint16_t conf, std::uint32_t ext = 0,
                   std::uint8_t bank_idx = 0) {
    std::string f(0x5C - 4, '\0');
    auto put = [&f](std::size_t off, const std::string& b) { f.replace(off - 4, b.size(), b); };
    put(0x0C, le16(size));
    put(0x10, std::string(1, '\x01'));  // locator = #1
    if (bank_idx) put(0x11, std::string(1, static_cast<char>(bank_idx)));  // bank locator (F-62)
    put(0x12, std::string(1, static_cast<char>(mtype)));
    put(0x15, le16(speed));
    put(0x17, std::string(1, '\x02'));  // manufacturer = #2
    put(0x18, std::string(1, '\x03'));  // serial = #3 (okunmamali)
    put(0x1A, std::string(1, '\x04'));  // part = #4
    put(0x1C, le32(ext));
    put(0x20, le16(conf));
    return f;
}
}  // namespace

EF_TEST(smbios_decodes_memory_and_board) {
    Builder b;
    b.add(0, std::string(1, '\x01') + std::string(1, '\x02') + std::string(2, '\0') + std::string(1, '\x03'),
          {"American Megatrends", "1.40", "05/12/2026"});
    b.add(2, std::string(1, '\x01') + std::string(1, '\x02'), {"ASUSTeK", "ROG STRIX X870E-E"});
    std::string t16(0x0F - 4, '\0');
    t16.replace(0x0D - 4, 2, le16(4));
    b.add(16, t16, {});
    b.add(17, type17(16384, 0x22, 6000, 6000, 0, 5), {"DIMM_A2", "G.Skill", "SERIAL123", "F5-6000J3038F16G", "Channel A"});
    b.add(17, type17(0, 0, 0, 0), {"DIMM_A1"});  // bos yuva
    b.add(17, type17(0x7FFF, 0x22, 0xFFFF, 6000, 65536), {"DIMM_B2", "G.Skill", "SERIAL456", "  F5-6000  "});
    b.add(127, "", {});
    auto t = parse_smbios(b.raw());
    EF_CHECK(t.ok());
    if (!t.ok()) return;
    const auto mem = decode_memory(t.value());
    EF_CHECK(mem.total_slots.value == 4u);
    EF_CHECK(mem.populated_slots.value == 2u);
    EF_CHECK(mem.populated.size() == 2);
    EF_CHECK(mem.populated[0].size_mib.value == 16384u);
    EF_CHECK(mem.populated[0].type.value == std::string("DDR5"));
    EF_CHECK(mem.populated[0].speed_mts.value == 6000u);
    EF_CHECK(mem.populated[0].part_number.value == std::string("F5-6000J3038F16G"));
    EF_CHECK(mem.populated[0].bank_locator.value == std::string("Channel A"));  // F-62 kanal izi
    EF_CHECK(mem.populated[1].size_mib.value == 65536u);           // genisletilmis boyut
    EF_CHECK(!mem.populated[1].speed_mts.has());                     // 0xFFFF + kisa uzanti alani yok -> 0
    EF_CHECK(mem.populated[1].part_number.value == std::string("F5-6000"));  // kirpildi
    EF_CHECK(!mem.channel_count.has());                              // DIMM sayisindan uydurulmaz
    EF_CHECK(!mem.channel_count.reason.empty());
    const auto board = decode_board(t.value());
    EF_CHECK(board.board_vendor.value == std::string("ASUSTeK"));
    EF_CHECK(board.bios_version.value == std::string("1.40"));
    EF_CHECK(board.bios_date.value == std::string("05/12/2026"));
}

EF_TEST(smbios_kib_unit_bit) {
    // 0x8000 biti: boyut KiB biriminde (ornegin 0x8000 | 512 = 512 KiB -> 0 MiB tam sayi)
    Builder b;
    b.add(17, type17(static_cast<std::uint16_t>(0x8000 | 2048), 0x1A, 3200, 3200), {"X"});
    b.add(127, "", {});
    auto t = parse_smbios(b.raw());
    EF_CHECK(t.ok());
    const auto mem = decode_memory(t.value());
    EF_CHECK(mem.populated.size() == 1);
    EF_CHECK(mem.populated[0].size_mib.value == 2u);
}

EF_TEST(smbios_missing_fields_are_unknown_and_safe) {
    Builder b;
    b.add(2, std::string(1, '\x01') + std::string(1, '\x00'), {"Default string"});
    b.add(127, "", {});
    auto t = parse_smbios(b.raw());
    EF_CHECK(t.ok());
    const auto board = decode_board(t.value());
    EF_CHECK(!board.board_vendor.has());   // yer tutucu metin bilinmeyen sayilir
    EF_CHECK(!board.board_product.has());  // string indeksi 0
    EF_CHECK(!board.bios_vendor.has());    // tip 0 yok
    const auto mem = decode_memory(t.value());
    EF_CHECK(!mem.total_slots.has());
    EF_CHECK(!mem.populated_slots.has());
}

EF_TEST(smbios_malformed_input_rejected) {
    EF_CHECK(!parse_smbios("").ok());
    EF_CHECK(!parse_smbios(std::string(7, '\0')).ok());
    std::string bad_len(8, '\0');
    bad_len[4] = '\x10';  // 16 bayt iddia, 0 bayt var
    EF_CHECK(!parse_smbios(bad_len).ok());
    Builder b;
    b.add(17, type17(16384, 0x1A, 3200, 3200), {"X"});
    std::string raw = b.raw();
    raw.pop_back();
    raw.pop_back();  // string sonlandirici kesik
    raw[4] = static_cast<char>(raw.size() - 8);
    EF_CHECK(!parse_smbios(raw).ok());
    // Mutasyon: asla cokmemeli
    std::uint32_t rng = 7;
    const std::string seed = b.raw();
    for (int i = 0; i < 5000; ++i) {
        std::string s = seed;
        rng = rng * 1664525u + 1013904223u;
        s[8 + rng % (s.size() - 8)] = static_cast<char>(rng >> 24);
        auto r = parse_smbios(s);
        if (r.ok()) { (void)decode_memory(r.value()); (void)decode_board(r.value()); }
    }
}

EF_TEST(cpu_topology_x3d_like) {
    // 8 cekirdek, SMT, tek L3 dilimi 96 MiB
    std::vector<CoreRecord> cores(8, CoreRecord{2, 0, 0});
    std::vector<CacheRecord> caches{{1, 32768, true}, {2, 1u << 20, true}, {3, 96u << 20, true}};
    const auto t = summarize_cpu(1, cores, caches);
    EF_CHECK(t.physical_cores.value == 8u);
    EF_CHECK(t.logical_processors.value == 16u);
    EF_CHECK(t.smt.value == true);
    EF_CHECK(t.hybrid.value == false);
    EF_CHECK(t.performance_cores.value == 8u);
    EF_CHECK(t.efficiency_cores.value == 0u);
    EF_CHECK(t.l3_total_kib.value == 98304u);
    EF_CHECK(t.l3_slices.value == 1u);
}

EF_TEST(cpu_topology_hybrid_intel_like) {
    std::vector<CoreRecord> cores;
    for (int i = 0; i < 6; ++i) cores.push_back({2, 1, 0});  // P (SMT)
    for (int i = 0; i < 8; ++i) cores.push_back({1, 0, 0});  // E
    const auto t = summarize_cpu(1, cores, {});
    EF_CHECK(t.hybrid.value == true);
    EF_CHECK(t.performance_cores.value == 6u);
    EF_CHECK(t.efficiency_cores.value == 8u);
    EF_CHECK(t.logical_processors.value == 20u);
    EF_CHECK(!t.l3_total_kib.has());
}

EF_TEST(cpu_topology_empty_is_unknown) {
    const auto t = summarize_cpu(0, {}, {});
    EF_CHECK(!t.physical_cores.has());
    EF_CHECK(!t.hybrid.has());
}

EF_TEST(smbios_vm_signatures) {
    Builder vm;
    vm.add(1, std::string(1, '\x01') + std::string(1, '\x02'), {"Microsoft Corporation", "Virtual Machine"});
    vm.add(127, "", {});
    EF_CHECK(smbios_indicates_vm(parse_smbios(vm.raw()).value()));
    Builder q;
    q.add(0, std::string(1, '\x01') + std::string(1, '\x02') + std::string(2, '\0') + std::string(1, '\0'), {"SeaBIOS", "rel-1.16-QEMU"});
    q.add(127, "", {});
    EF_CHECK(smbios_indicates_vm(parse_smbios(q.raw()).value()));
    Builder phys;
    phys.add(1, std::string(1, '\x01') + std::string(1, '\x02'), {"Gigabyte Technology Co., Ltd.", "X870E AORUS ELITE WIFI7 ICE"});
    phys.add(127, "", {});
    EF_CHECK(!smbios_indicates_vm(parse_smbios(phys.raw()).value()));
}
