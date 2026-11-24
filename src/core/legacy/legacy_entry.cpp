// T3b (ADR-043 ek): eski yoldan giris kararlari (saf).
#include "core/legacy/legacy_entry.h"

#include "core/update/staged_update.h"

namespace ef::core::legacy {

namespace {
bool drive_letter(wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'); }
wchar_t upper(wchar_t c) { return (c >= L'a' && c <= L'z') ? static_cast<wchar_t>(c - L'a' + L'A') : c; }
}  // namespace

std::optional<std::wstring> canonical_local_path(std::wstring_view p) {
    std::wstring s(p);
    for (auto& c : s)
        if (c == L'/') c = L'\\';
    if (s.starts_with(L"\\\\?\\")) {
        s = s.substr(4);
        if (s.size() >= 4 && upper(s[0]) == L'U' && upper(s[1]) == L'N' && upper(s[2]) == L'C' && s[3] == L'\\') return std::nullopt;
    }
    // Yalniz "X:\..." (UNC, aygit, kok-goreli, surucu-goreli ve goreli yollar reddedilir).
    if (s.size() < 3 || !drive_letter(s[0]) || s[1] != L':' || s[2] != L'\\') return std::nullopt;
    while (s.size() > 3 && s.back() == L'\\') s.pop_back();
    std::wstring out = {upper(s[0]), L':', L'\\'};
    std::size_t i = 3;
    while (i < s.size()) {
        const auto j = s.find(L'\\', i);
        const auto comp = s.substr(i, j == std::wstring::npos ? std::wstring::npos : j - i);
        if (comp.empty() || comp == L"." || comp == L"..") return std::nullopt;
        for (wchar_t c : comp)
            if (c == L':' || c < 0x20) return std::nullopt;  // ADS / denetim karakteri
        if (out.size() > 3) out += L'\\';
        for (wchar_t c : comp) out += upper(c);
        if (j == std::wstring::npos) break;
        i = j + 1;
    }
    return out;
}

bool same_canonical_path(std::wstring_view a, std::wstring_view b) {
    const auto x = canonical_local_path(a), y = canonical_local_path(b);
    return x && y && *x == *y;
}

bool legacy_service_candidate(std::wstring_view self_raw, const setup::InstallLayout& legacy) {
    return !legacy.exe.empty() && same_canonical_path(self_raw, legacy.exe.native());
}

std::string_view to_string(LegacyEntry e) noexcept {
    switch (e) {
        case LegacyEntry::NotLegacy: return "not_legacy";
        case LegacyEntry::RunUpgrade: return "run_upgrade";
        case LegacyEntry::RestoreOldExe: return "restore_old_exe";
        case LegacyEntry::Refuse: return "refuse";
    }
    return "?";
}

LegacyEntryDecision decide_legacy_service_entry(const LegacyServiceFacts& f, const setup::InstallLayout& legacy) {
    auto out = [](LegacyEntry a, std::string why) { return LegacyEntryDecision{a, std::move(why)}; };
    if (!legacy_service_candidate(f.self_raw, legacy)) return out(LegacyEntry::NotLegacy, "not the old install path");
    // Ham yol eski EXE: buradan sonra her red "Refuse" (hicbir sey yazilmaz) veya dogrulanmis kimlikte "RestoreOldExe".
    if (!same_canonical_path(f.self_final, legacy.exe.native())) return out(LegacyEntry::Refuse, "final image path differs (junction/symlink)");
    if (f.path_probe_failed) return out(LegacyEntry::Refuse, "path attributes unreadable");
    if (f.reparse_in_path) return out(LegacyEntry::Refuse, "reparse point in install path");
    if (!f.scm_connected) return out(LegacyEntry::Refuse, "not started by the service control manager");
    if (!f.token_system) return out(LegacyEntry::Refuse, "not running as LocalSystem");
    if (f.self_pid == 0 || f.scm_pid != f.self_pid) return out(LegacyEntry::Refuse, "SCM pid of the old service differs");
    const auto want = setup::service_image_path(legacy);
    if (f.scm_image_path.size() != want.size() || !same_canonical_path(image_path_exe(f.scm_image_path).value_or(L""), legacy.exe.native()) ||
        !f.scm_image_path.ends_with(L"\" --service"))
        return out(LegacyEntry::Refuse, "old service image path unexpected");
    if (!f.image_trusted) return out(LegacyEntry::RestoreOldExe, "running image not trusted");
    return out(LegacyEntry::RunUpgrade, "verified old service host");
}

std::optional<std::wstring> image_path_exe(std::wstring_view image_path) {
    if (image_path.size() < 3 || image_path[0] != L'"') return std::nullopt;
    const auto close = image_path.find(L'"', 1);
    if (close == std::wstring_view::npos || close == 1) return std::nullopt;
    return std::wstring(image_path.substr(1, close - 1));
}

std::optional<std::string> legacy_state_after_restore(std::string_view current_json) {
    const auto cur = update::decode_state(current_json);
    if (!cur.ok()) return std::nullopt;
    update::StagedUpdateState idle;  // 0.9.5 UpdateCoordinator::roll_back ile ayni
    idle.highest_manifest_version = cur.value().highest_manifest_version;
    return update::encode_state(idle);
}

namespace {
std::wstring wide_of(std::string_view u8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(u8.data()), u8.size())).native();
}
bool same_u8(std::string_view a, std::string_view b) { return same_canonical_path(wide_of(a), wide_of(b)); }
}  // namespace

bool path_under(std::string_view path_utf8, std::string_view root_utf8) {
    const auto p = canonical_local_path(wide_of(path_utf8)), r = canonical_local_path(wide_of(root_utf8));
    if (!p || !r || r->size() <= 3) return false;  // surucu kokunun kendisi asla kok sayilmaz
    return *p == *r || (p->size() > r->size() && p->compare(0, r->size(), *r) == 0 && (*p)[r->size()] == L'\\');
}

bool upgrade_name_allowed(SysOp op, std::wstring_view name, const UpgradeNames& n) {
    if (name.empty()) return false;
    switch (op) {
        case SysOp::CreateService:
        case SysOp::StartService: return name == n.new_service;
        case SysOp::StopService:
        case SysOp::DeleteService: return name == n.new_service || name == n.old_service;
        case SysOp::WriteArp: return name == n.new_arp_key;
        case SysOp::DeleteArp: return name == n.new_arp_key || name == n.old_arp_key;
        default: return false;
    }
}

bool upgrade_path_allowed(SysOp op, std::string_view path, const UpgradePaths& p) {
    if (path.empty()) return false;
    switch (op) {
        case SysOp::WriteShortcut: return same_u8(path, p.new_start_lnk) || same_u8(path, p.new_desktop_lnk);
        case SysOp::DeleteShortcut:
            return same_u8(path, p.new_start_lnk) || same_u8(path, p.new_desktop_lnk) || same_u8(path, p.old_start_lnk) ||
                   same_u8(path, p.old_desktop_lnk);
        case SysOp::InstallAcl: return same_u8(path, p.new_dir);
        case SysOp::MachineAcl: return path_under(path, p.new_machine_root);
        case SysOp::RebootDelete: return path_under(path, p.old_dir);
        default: return false;
    }
}

std::optional<std::uint32_t> legacy_cleanup_retry_ms(UpgradeOutcome o, int attempt) noexcept {
    if (attempt < 0) return std::nullopt;
    if (o == UpgradeOutcome::WaitingForHost && attempt < 120) return 5000u;
    if (o == UpgradeOutcome::CleanupPending && attempt < 30) return 60000u;
    return std::nullopt;
}

}  // namespace ef::core::legacy
