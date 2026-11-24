#pragma once
// ADR-033: kurulum yerlesimi (saf). Bilinen klasor koklerinden kurulum, makine ve kullanici yollari turetilir;
// platform okuma platform/windows/known_roots ile yapilir. Bu dosya hicbir sey yazmaz.

#include <filesystem>
#include <string>
#include <string_view>

#include "core/catalog/envelope.h"

namespace ef::core::setup {

// ADR-033 (gorev 6): kurulum klasoru disindan bos komut satiriyla acilis kurulum penceresini acar (Setup rolu).
// Ui kurulum yeri disinda (--tray / --updated-from) "Kurulum gerekli" seridini gosterir.
inline constexpr bool kSetupRoleEnabled = true;

inline constexpr std::wstring_view kInstallDirName = L"Ecu Fps";
inline constexpr std::wstring_view kExeName = L"EcuFps.exe";
inline constexpr std::wstring_view kServiceName = L"EcuFpsHelper";
inline constexpr std::wstring_view kServiceDisplayName = L"Ecu Fps Helper";
inline constexpr std::wstring_view kPipeName = L"\\\\.\\pipe\\EcuFps.Helper.v1";
inline constexpr std::wstring_view kArpKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\EcuFps";
inline constexpr std::wstring_view kShortcutName = L"Ecu Fps.lnk";
inline constexpr std::wstring_view kSetupMutex = L"Local\\EcuFps.Setup";

struct KnownRoots {
    std::filesystem::path program_files, program_data, common_programs, local_app_data;
    std::filesystem::path common_desktop;  // FOLDERID_PublicDesktop (tum kullanicilar)
};

struct InstallLayout {
    std::filesystem::path dir, exe, shortcut, desktop_shortcut, machine_dir, updates_dir, update_state, setup_dir, setup_journal, user_dir,
        user_setup_dir;
    catalog::SwapPaths swap;
};

// Bos kok -> ondan turetilen yollar bos.
InstallLayout make_layout(const KnownRoots& r);

// lexically_normal, \\?\ oneki atilir, buyuk/kucuk harf duyarsiz (ASCII + towupper), sondaki ayirac yok sayilir.
bool same_path(const std::filesystem::path& a, const std::filesystem::path& b);
bool is_installed_location(const std::filesystem::path& self_exe, const InstallLayout& l);

std::wstring service_image_path(const InstallLayout& l);  // "\"<exe>\" --service"
std::wstring uninstall_command(const InstallLayout& l);   // "\"<exe>\" --uninstall"

}  // namespace ef::core::setup
