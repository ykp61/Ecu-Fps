#pragma once
// ADR-033 / spec 2: kurulum ve makine klasorlerinin ACL kurali (saf). Platform GetNamedSecurityInfoW ile okudugu
// gercekleri AclFacts'e cevirir; karar burada verilir ve birim testlenir. Gorev 4 (hizmet istemci denetimi) de kullanir.

#include <cstdint>
#include <string>
#include <vector>

namespace ef::core::setup {

inline constexpr char kSidSystem[] = "S-1-5-18";
inline constexpr char kSidAdministrators[] = "S-1-5-32-544";
inline constexpr char kSidTrustedInstaller[] = "S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";

// Yazma sayilan erisim bitleri: FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_DELETE_CHILD|FILE_WRITE_ATTRIBUTES|
// DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_ALL|GENERIC_WRITE (+ MAXIMUM_ALLOWED: belirsiz genis istek).
inline constexpr std::uint32_t kWriteAccessMask =
    0x00000002u | 0x00000004u | 0x00000010u | 0x00000040u | 0x00000100u | 0x00010000u | 0x00040000u | 0x00080000u |
    0x10000000u | 0x40000000u | 0x02000000u;

struct AceFact {
    std::string sid;          // "S-1-..." metni
    std::uint32_t mask = 0;
    bool allow = true;        // izin veren ACE turu (callback/object dahil); false: reddeden
};

struct AclFacts {
    bool dacl_present = false;   // NULL DACL = herkese tam erisim -> reddedilir
    bool dacl_protected = false; // SE_DACL_PROTECTED (ust klasorden miras yok)
    std::string owner_sid;
    std::vector<AceFact> aces;
};

// Korumali DACL, sahip SY/BA/TrustedInstaller ve bu ucu disinda hicbir SID'e yazma bitli izin ACE'si yok.
bool install_dir_acl_ok(const AclFacts& f);

}  // namespace ef::core::setup
