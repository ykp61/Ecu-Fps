#pragma once
// ADR-033 / spec 2: yonetici-yalniz klasorler (kurulum klasoru, ProgramData\EcuFps\{setup,updates}). Korumali DACL
// SetNamedSecurityInfoW ile uygulanir (sahip BA), GetNamedSecurityInfoW ile geri okunur ve saf install_dir_acl_ok ile
// karar verilir. Yeniden ayristirma noktasi (junction/symlink) reddedilir. Bu oturumda calistirilmaz (yukseltme ister).

#include <filesystem>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/setup/acl_rules.h"

namespace ef::platform::install {

inline constexpr wchar_t kInstallDirSddl[] = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)";
inline constexpr wchar_t kAdminOnlySddl[] = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
// ProgramData\EcuFps koku: makine journal'lariyla ayni kural (journal_fs.cpp kMachineDirSddl).
inline constexpr wchar_t kMachineRootSddl[] = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FR;;;BU)";

contracts::Result<core::setup::AclFacts> read_acl_facts(const std::filesystem::path& p);

// Yoksa SDDL ile olusturur; varsa (reparse degilse) ayni korumali DACL'i ve BA sahipligini yeniden uygular.
// Sonra geri okur: install_dir_acl_ok degilse Denied.
contracts::Status ensure_protected_dir(const std::filesystem::path& dir, const wchar_t* sddl);

// ProgramData\EcuFps: yoksa kMachineRootSddl ile olusturulur; varsa yalniz dogrulanir (makine journal kurali).
contracts::Status ensure_machine_root(const std::filesystem::path& machine_dir);

bool is_reparse_or_missing_dir(const std::filesystem::path& p);
std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view w);

}  // namespace ef::platform::install
