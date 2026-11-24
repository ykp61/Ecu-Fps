#include "core/inventory/smbios.h"

namespace ef::core::inventory {

using contracts::ErrorCode;
using contracts::Field;
using contracts::Provenance;

std::uint8_t SmbiosStructure::byte_at(std::size_t off, bool& ok) const noexcept {
    if (off + 1 > formatted.size()) { ok = false; return 0; }
    return static_cast<std::uint8_t>(formatted[off]);
}
std::uint16_t SmbiosStructure::word_at(std::size_t off, bool& ok) const noexcept {
    if (off + 2 > formatted.size()) { ok = false; return 0; }
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(formatted[off]) |
                                      (static_cast<std::uint8_t>(formatted[off + 1]) << 8));
}
std::uint32_t SmbiosStructure::dword_at(std::size_t off, bool& ok) const noexcept {
    if (off + 4 > formatted.size()) { ok = false; return 0; }
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<std::uint8_t>(formatted[off + static_cast<std::size_t>(i)]);
    return v;
}
std::string SmbiosStructure::str_at(std::size_t off) const {
    bool ok = true;
    const auto idx = byte_at(off, ok);
    if (!ok || idx == 0 || idx > strings.size()) return {};
    // Bosluklari kirp; bazi BIOS'lar "Default string" veya bosluk dolgusu yazar.
    std::string s = strings[idx - 1];
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    std::size_t b = 0;
    while (b < s.size() && s[b] == ' ') ++b;
    s.erase(0, b);
    if (s == "Default string" || s == "To Be Filled By O.E.M." || s == "Not Specified" || s == "Unknown") return {};
    return s;
}

contracts::Result<SmbiosTable> parse_smbios(std::string_view raw) {
    using R = contracts::Result<SmbiosTable>;
    if (raw.size() < 8) return R::fail(ErrorCode::ParseError, "header");
    if (raw.size() > (16u << 20)) return R::fail(ErrorCode::ParseLimit, "table too large");
    SmbiosTable t;
    t.major = static_cast<std::uint8_t>(raw[1]);
    t.minor = static_cast<std::uint8_t>(raw[2]);
    std::uint32_t len = 0;
    for (int i = 7; i >= 4; --i) len = (len << 8) | static_cast<std::uint8_t>(raw[static_cast<std::size_t>(i)]);
    if (len > raw.size() - 8) return R::fail(ErrorCode::ParseError, "length exceeds buffer");
    const std::string_view d = raw.substr(8, len);

    std::size_t p = 0;
    while (p + 4 <= d.size()) {
        if (t.structures.size() >= 4096) return R::fail(ErrorCode::ParseLimit, "too many structures");
        SmbiosStructure s;
        s.type = static_cast<std::uint8_t>(d[p]);
        const std::size_t flen = static_cast<std::uint8_t>(d[p + 1]);
        if (flen < 4 || p + flen > d.size()) return R::fail(ErrorCode::ParseError, "structure length");
        s.handle = static_cast<std::uint16_t>(static_cast<std::uint8_t>(d[p + 2]) | (static_cast<std::uint8_t>(d[p + 3]) << 8));
        s.formatted.assign(d.substr(p, flen));
        std::size_t q = p + flen;
        // String bolumu: \0 ile ayrilmis, \0\0 ile biter.
        if (q + 1 < d.size() && d[q] == '\0' && d[q + 1] == '\0') {
            q += 2;
        } else {
            for (;;) {
                const std::size_t z = d.find('\0', q);
                if (z == std::string_view::npos) return R::fail(ErrorCode::ParseError, "unterminated strings");
                if (s.strings.size() >= 255) return R::fail(ErrorCode::ParseLimit, "too many strings");
                s.strings.emplace_back(d.substr(q, z - q));
                q = z + 1;
                if (q < d.size() && d[q] == '\0') { ++q; break; }
                if (q >= d.size()) return R::fail(ErrorCode::ParseError, "unterminated string set");
            }
        }
        const bool end = s.type == 127;
        t.structures.push_back(std::move(s));
        p = q;
        if (end) break;
    }
    return t;
}

std::string memory_type_name(std::uint8_t code) {
    switch (code) {
        case 0x12: return "DDR";
        case 0x13: return "DDR2";
        case 0x18: return "DDR3";
        case 0x1A: return "DDR4";
        case 0x1B: return "LPDDR";
        case 0x1C: return "LPDDR2";
        case 0x1D: return "LPDDR3";
        case 0x1E: return "LPDDR4";
        case 0x22: return "DDR5";
        case 0x23: return "LPDDR5";
        default: return {};
    }
}

namespace {
Field<std::string> sfield(const std::string& s, const char* why) {
    return s.empty() ? Field<std::string>::unknown(why) : Field<std::string>::known(s, Provenance::Smbios);
}
}  // namespace

MemorySmbios decode_memory(const SmbiosTable& t) {
    MemorySmbios m;
    std::uint32_t slots = 0;
    bool any16 = false, any17 = false;
    for (const auto& s : t.structures) {
        bool ok = true;
        if (s.type == 16) {
            const auto n = s.word_at(0x0D, ok);
            if (ok) { slots += n; any16 = true; }
        } else if (s.type == 17) {
            any17 = true;
            const auto size = s.word_at(0x0C, ok);
            if (!ok || size == 0 || size == 0xFFFF) continue;  // bos/bilinmeyen yuva
            MemoryModule mod;
            std::uint64_t mib;
            if (size == 0x7FFF) {
                bool ok2 = true;
                const auto ext = s.dword_at(0x1C, ok2);
                if (!ok2) continue;
                mib = ext & 0x7FFFFFFF;
            } else if (size & 0x8000) {
                mib = (size & 0x7FFF) / 1024;  // KiB biriminde
            } else {
                mib = size;
            }
            mod.size_mib = Field<std::uint64_t>::known(mib, Provenance::Smbios);
            bool okt = true;
            const auto tcode = s.byte_at(0x12, okt);
            const auto tname = okt ? memory_type_name(tcode) : std::string();
            mod.type = sfield(tname, "memory type not reported");
            bool oks = true;
            auto speed = static_cast<std::uint32_t>(s.word_at(0x15, oks));
            if (oks && speed == 0xFFFF) { bool oke = true; speed = s.dword_at(0x54, oke); oks = oke; }
            mod.speed_mts = (oks && speed) ? Field<std::uint32_t>::known(speed, Provenance::Smbios)
                                           : Field<std::uint32_t>::unknown("speed not reported");
            bool okc = true;
            auto conf = static_cast<std::uint32_t>(s.word_at(0x20, okc));
            if (okc && conf == 0xFFFF) { bool oke = true; conf = s.dword_at(0x58, oke); okc = oke; }
            mod.configured_mts = (okc && conf) ? Field<std::uint32_t>::known(conf, Provenance::Smbios)
                                               : Field<std::uint32_t>::unknown("configured speed not reported");
            mod.manufacturer = sfield(s.str_at(0x17), "manufacturer not reported");
            mod.part_number = sfield(s.str_at(0x1A), "part number not reported");
            mod.locator = sfield(s.str_at(0x10), "locator not reported");
            mod.bank_locator = sfield(s.str_at(0x11), "bank locator not reported");
            m.populated.push_back(std::move(mod));
        }
    }
    m.total_slots = any16 ? Field<std::uint32_t>::known(slots, Provenance::Smbios)
                          : Field<std::uint32_t>::unknown("no SMBIOS type 16");
    m.populated_slots = any17 ? Field<std::uint32_t>::known(static_cast<std::uint32_t>(m.populated.size()), Provenance::Smbios)
                              : Field<std::uint32_t>::unknown("no SMBIOS type 17");
    m.channel_count = Field<std::uint32_t>::unknown("physical channel count is not derivable from DIMM count");
    return m;
}

bool smbios_indicates_vm(const SmbiosTable& t) {
    static constexpr std::string_view kSigs[] = {"Virtual Machine", "VMware", "VirtualBox", "innotek", "QEMU", "KVM",
                                                 "Xen", "Parallels", "Bochs", "BHYVE", "Amazon EC2", "Google Compute Engine"};
    for (const auto& s : t.structures) {
        std::vector<std::string> fields;
        if (s.type == 1) fields = {s.str_at(0x04), s.str_at(0x05)};       // uretici, urun adi
        else if (s.type == 0) fields = {s.str_at(0x04), s.str_at(0x05)};  // BIOS saglayici, surum
        for (const auto& f : fields)
            for (auto sig : kSigs)
                if (f.find(sig) != std::string::npos) return true;
    }
    return false;
}

BoardSmbios decode_board(const SmbiosTable& t) {
    BoardSmbios b;
    b.board_vendor = Field<std::string>::unknown("no SMBIOS type 2");
    b.board_product = b.board_vendor;
    b.bios_vendor = Field<std::string>::unknown("no SMBIOS type 0");
    b.bios_version = b.bios_vendor;
    b.bios_date = b.bios_vendor;
    for (const auto& s : t.structures) {
        if (s.type == 0) {
            b.bios_vendor = sfield(s.str_at(0x04), "bios vendor not reported");
            b.bios_version = sfield(s.str_at(0x05), "bios version not reported");
            b.bios_date = sfield(s.str_at(0x08), "bios date not reported");
        } else if (s.type == 2) {
            b.board_vendor = sfield(s.str_at(0x04), "board vendor not reported");
            b.board_product = sfield(s.str_at(0x05), "board product not reported");
        }
    }
    return b;
}

}  // namespace ef::core::inventory
