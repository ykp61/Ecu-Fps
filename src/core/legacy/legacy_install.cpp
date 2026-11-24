// R-F2 (ADR-043): eski kurulum yerlesimi ve eski cagri tanima (saf).
#include "core/legacy/legacy_install.h"

#include "core/legacy/legacy_names.h"
#include "core/roles.h"
#include "core/update/update_check.h"

namespace ef::core::legacy {

namespace fs = std::filesystem;

namespace {
fs::path join(const fs::path& base, std::wstring_view leaf) { return base.empty() ? fs::path{} : base / fs::path(leaf); }
std::string utf8(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
}  // namespace

setup::InstallLayout legacy_layout(const setup::KnownRoots& r) {
    setup::InstallLayout l;
    l.dir = join(r.program_files, kOldInstallDirName);
    l.exe = join(l.dir, kOldExeName);
    l.shortcut = join(r.common_programs, kOldShortcutName);
    l.desktop_shortcut = join(r.common_desktop, kOldShortcutName);
    l.machine_dir = join(r.program_data, kOldDataDirName);
    l.updates_dir = join(l.machine_dir, L"updates");
    l.update_state = join(l.updates_dir, L"state.json");
    l.setup_dir = join(l.machine_dir, L"setup");
    l.setup_journal = join(l.setup_dir, L"setup.journal");
    l.user_dir = join(r.local_app_data, kOldDataDirName);
    l.user_setup_dir = join(l.user_dir, L"setup");
    if (!l.exe.empty()) l.swap = update::update_swap_paths(utf8(l.exe));
    return l;
}

std::wstring legacy_arp_key() { return L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + std::wstring(kOldUninstallKeyName); }

std::vector<fs::path> legacy_owned_files(const setup::InstallLayout& legacy) {
    if (legacy.exe.empty()) return {};
    const auto e = legacy.exe.native();
    return {fs::path(e), fs::path(e + L".new"), fs::path(e + L".old"), fs::path(e + L".updated"), fs::path(e + L".new.failed")};
}

std::string_view to_string(LegacyInvocation k) noexcept {
    switch (k) {
        case LegacyInvocation::None: return "none";
        case LegacyInvocation::HealthHandshake: return "health_handshake";
        case LegacyInvocation::ServiceHost: return "service_host";
        case LegacyInvocation::Tray: return "tray";
        case LegacyInvocation::Shortcut: return "shortcut";
        case LegacyInvocation::Uninstall: return "uninstall";
        case LegacyInvocation::Setup: return "setup";
        case LegacyInvocation::Recovery: return "recovery";
        case LegacyInvocation::Other: return "other";
    }
    return "?";
}

LegacyInvocation classify_legacy_invocation(std::wstring_view command_line, const fs::path& self_exe, const setup::KnownRoots& r) {
    const auto legacy = legacy_layout(r);
    if (!setup::is_installed_location(self_exe, legacy)) return LegacyInvocation::None;
    // Eski surum ayni bayrak yazimlarini kullanir (roles.cpp 0.9.5 ile degismedi); ayni kati ayristirici.
    const auto a = parse_launch_args(command_line);
    if (!a.ok()) return LegacyInvocation::Other;
    const auto& v = a.value();
    switch (v.role) {
        case Role::Service: return LegacyInvocation::ServiceHost;
        case Role::Uninstall: return LegacyInvocation::Uninstall;
        case Role::Setup: return LegacyInvocation::Setup;
        case Role::Recovery: return LegacyInvocation::Recovery;
        case Role::Ui:
            if (!v.updated_from.empty()) return LegacyInvocation::HealthHandshake;
            if (v.tray) return LegacyInvocation::Tray;
            if (v.bare) return LegacyInvocation::Shortcut;
            return LegacyInvocation::Other;
        default: return LegacyInvocation::Other;
    }
}

LegacyAction legacy_action(LegacyInvocation k) noexcept {
    switch (k) {
        case LegacyInvocation::None: return LegacyAction::Normal;
        case LegacyInvocation::ServiceHost: return LegacyAction::UnattendedUpgrade;
        case LegacyInvocation::HealthHandshake: return LegacyAction::UiSignalOldService;
        case LegacyInvocation::Tray:
        case LegacyInvocation::Shortcut:
        case LegacyInvocation::Setup:
        case LegacyInvocation::Recovery: return LegacyAction::Ui;
        case LegacyInvocation::Uninstall:
        case LegacyInvocation::Other: return LegacyAction::RefuseUpgradePending;
    }
    return LegacyAction::RefuseUpgradePending;
}

LegacyRoute legacy_launch_route(LegacyInvocation k, bool upgrade_record_present) noexcept {
    switch (legacy_action(k)) {
        case LegacyAction::RefuseUpgradePending: return upgrade_record_present ? LegacyRoute::RefuseUpgradePending : LegacyRoute::Normal;
        case LegacyAction::Ui: return (k == LegacyInvocation::Shortcut || k == LegacyInvocation::Tray) ? LegacyRoute::ForceUi : LegacyRoute::Normal;
        case LegacyAction::Normal:
        case LegacyAction::UnattendedUpgrade:
        case LegacyAction::UiSignalOldService: return LegacyRoute::Normal;
    }
    return LegacyRoute::Normal;
}

std::string_view to_string(LegacyRoute r) noexcept {
    switch (r) {
        case LegacyRoute::Normal: return "normal";
        case LegacyRoute::ForceUi: return "force_ui";
        case LegacyRoute::RefuseUpgradePending: return "refuse_upgrade_pending";
    }
    return "?";
}

std::wstring_view legacy_health_pipe() noexcept { return kOldHelperPipeName; }
std::wstring_view legacy_health_service() noexcept { return kOldServiceName; }

}  // namespace ef::core::legacy
