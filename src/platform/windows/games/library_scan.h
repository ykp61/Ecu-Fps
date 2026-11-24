#pragma once
// DIAG-08 platform: kurulu oyunlar (Steam + Epic) — salt okuma, boyut sinirli dosya okuma, ag yok.
// Tarama kurallari core/games/library_scan.*; burada gercek kayit defteri ve dosya sistemi (LibraryFs) vardir.

#include <string>
#include <vector>

#include "core/games/library_scan.h"
#include "core/games/manual_games.h"

namespace ef::platform::games {

using core::games::LibraryScanResult;
using core::games::LibraryScanState;
using core::games::WatchBootstrap;
using core::games::watch_bootstrap;

class WinLibraryFs final : public core::games::LibraryFs {
public:
    // steam_user_sid: bos = HKCU; broker gibi yukseltilmis surecte hedef kullanicinin HKEY_USERS\<SID> kovani.
    explicit WinLibraryFs(std::wstring steam_user_sid = {}) : sid_(std::move(steam_user_sid)) {}
    // Yalniz testler: kayit defteri/ProgramData yerine gercek dosya sisteminde gecici kokler.
    void set_overrides(std::string steam_root, std::string epic_dir);

    core::games::SourceValue steam_root() override;
    core::games::SourceValue epic_manifest_dir() override;
    core::games::SourceValue riot_metadata_dir() override;
    core::games::PathKind probe(const std::string& path) override;
    core::games::DirList list(const std::string& dir) override;
    core::games::FileRead read_file(const std::string& path, std::size_t max_bytes) override;
    core::games::RegistryList registry_installs(std::string_view provider) override;
    core::games::PackageList user_packages() override;                      // xbox_packages.cpp
    std::string package_display_name(const std::string& family) override;  // xbox_packages.cpp

private:
    core::games::RegistryList battlenet_installs();
    core::games::RegistryList ea_installs();  // P9-B3: yalniz kayit defteri kurulum yolu; DOGRULANMADI, urunde kapali
    std::wstring sid_;
    bool overridden_ = false;
    std::string steam_override_, epic_override_;
};

LibraryScanResult scan_installed_library(const std::wstring& steam_user_sid = {}, const std::vector<std::string>& manual_exes = {});
// EF-X11: elle EXE ekleme icin Windows/ortak klasor baglami ve dosya denetimi (salt okuma).
core::games::ManualExeContext manual_exe_context();
enum class ExeFileCheck { Ok, Missing, NotFile, NotExecutable, Unreadable };
ExeFileCheck check_exe_file(const std::string& path_utf8);
// Geriye uyum: listeyi isteyen salt-okuma cagiranlari icin (guvenilirlik bilgisi tasimaz).
std::vector<core::inventory::GameInstall> scan_installed_games();

}  // namespace ef::platform::games
