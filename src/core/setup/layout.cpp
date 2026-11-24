#include "core/setup/layout.h"

#include <cwctype>

#include "core/update/update_check.h"

namespace ef::core::setup {

namespace fs = std::filesystem;

namespace {
fs::path join(const fs::path& base, std::wstring_view leaf) { return base.empty() ? fs::path{} : base / fs::path(leaf); }

std::string utf8(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::wstring canonical_text(const fs::path& p) {
    std::wstring s = p.native();
    if (s.starts_with(L"\\\\?\\UNC\\")) s = L"\\\\" + s.substr(8);
    else if (s.starts_with(L"\\\\?\\")) s = s.substr(4);
    s = fs::path(s).lexically_normal().native();
    for (auto& c : s) {
        if (c == L'/') c = L'\\';
        c = static_cast<wchar_t>(std::towupper(static_cast<std::wint_t>(c)));
    }
    while (s.size() > 1 && s.back() == L'\\' && !(s.size() == 3 && s[1] == L':')) s.pop_back();
    return s;
}

std::wstring quoted_with(const InstallLayout& l, std::wstring_view flag) {
    if (l.exe.empty()) return {};
    return L"\"" + l.exe.native() + L"\" " + std::wstring(flag);
}
}  // namespace

InstallLayout make_layout(const KnownRoots& r) {
    InstallLayout l;
    l.dir = join(r.program_files, kInstallDirName);
    l.exe = join(l.dir, kExeName);
    l.shortcut = join(r.common_programs, kShortcutName);
    l.desktop_shortcut = join(r.common_desktop, kShortcutName);
    l.machine_dir = join(r.program_data, L"EcuFps");
    l.updates_dir = join(l.machine_dir, L"updates");
    l.update_state = join(l.updates_dir, L"state.json");
    l.setup_dir = join(l.machine_dir, L"setup");
    l.setup_journal = join(l.setup_dir, L"setup.journal");
    l.user_dir = join(r.local_app_data, L"EcuFps");
    l.user_setup_dir = join(l.user_dir, L"setup");
    if (!l.exe.empty()) l.swap = update::update_swap_paths(utf8(l.exe));
    return l;
}

bool same_path(const fs::path& a, const fs::path& b) {
    if (a.empty() || b.empty()) return false;
    return canonical_text(a) == canonical_text(b);
}

bool is_installed_location(const fs::path& self_exe, const InstallLayout& l) {
    if (l.exe.empty() || self_exe.empty()) return false;
    return same_path(self_exe, l.exe);
}

std::wstring service_image_path(const InstallLayout& l) { return quoted_with(l, L"--service"); }
std::wstring uninstall_command(const InstallLayout& l) { return quoted_with(l, L"--uninstall"); }

}  // namespace ef::core::setup
