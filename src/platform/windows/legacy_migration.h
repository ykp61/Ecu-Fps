#pragma once
// R-F1: marka degisikligi kullanici verisi gocunun Windows tarafi. Saf kural core/legacy/user_data_migration.h'dadir.
// run_legacy_user_migration() surec basina bir kez, kullanici rolu (Ui / Setup / Uninstall / Recovery) veri klasorunu
// acan HER SEYDEN once cagrilir. Test kipi kurallari app_data_dir() ile aynidir:
//  - Default: eski = %LOCALAPPDATA%\<eski ad>, yeni = %LOCALAPPDATA%\EcuFps.
//  - Override (EF_TEST_BACKGROUND=1 + EF_DATA_DIR): eski klasor YALNIZ EF_LEGACY_DATA_DIR verilirse ve ayni %TEMP%
//    kuralindan gecerse kullanilir; yoksa goc calismaz (Skipped). Gercek profile asla dusulmez.
//  - Blocked / klasor cozulemedi: Skipped.

#include <filesystem>

#include "core/legacy/user_data_migration.h"

namespace ef::platform {

// Win32 MigrationFs (UTF-8 yollar). Yeniden ayristirma noktalari izlenmez; kopya FILE_FLAG_WRITE_THROUGH ile yazilir.
class WinMigrationFs final : public core::legacy::MigrationFs {
public:
    Kind kind(const std::string& path) override;
    core::legacy::FsStatus list_files(const std::string& dir, std::vector<core::legacy::FileInfo>& out) override;
    core::legacy::FsStatus hash_file(const std::string& path, core::legacy::FileDigest& out) override;
    core::legacy::FsStatus copy_file(const std::string& from, const std::string& to) override;
    core::legacy::FsStatus read_file(const std::string& path, std::size_t max_bytes, std::string& out) override;
    core::legacy::FsStatus write_file(const std::string& path, const std::string& bytes) override;
    core::legacy::FsStatus make_dirs(const std::string& path) override;
    core::legacy::FsStatus rename_dir(const std::string& from, const std::string& to) override;
    core::legacy::FsStatus move_file(const std::string& from, const std::string& to) override;
    core::legacy::FsStatus remove_file(const std::string& path) override;
    core::legacy::FsStatus remove_tree(const std::string& path) override;
    std::optional<std::uint64_t> free_bytes(const std::string& path) override;
};

// Gocu calistirir (bir kez; sonraki cagrilar ayni raporu doner). Tanilama satiri logs/migration.log'a yazilir.
const core::legacy::MigrationReport& run_legacy_user_migration();
// Bu surecte (calistiysa) goc sonucu; gocu tetiklemez. Sonuc "eski veride geri alinmamis degisiklik aktarilamadi" mi (UI durust kurtarma bildirimi).
bool legacy_user_data_pending();

}  // namespace ef::platform
