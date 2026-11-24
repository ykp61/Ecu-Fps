#pragma once
// WIN-08 platform: gercek CleanupFs (Win32 dosya API'si) ve ortam kesfi.
// Guvenlik: dosyalar FILE_FLAG_OPEN_REPARSE_POINT ile acilir; yeniden ayristirma noktasi, hardlink ve
// cozulmus son yolu kok disinda kalan dosya silinmez. Silme tanitici uzerinden yapilir (TOCTOU yok).

#include <map>
#include <string>

#include "core/cleanup/cleanup.h"

namespace ef::platform::cleanup {

class WinCleanupFs final : public core::cleanup::CleanupFs {
public:
    core::cleanup::DirState probe_dir(const std::wstring& path) override;
    contracts::Result<std::vector<core::cleanup::FsEntry>> list(const std::wstring& dir) override;
    core::cleanup::DeleteResult delete_file(const std::wstring& root, const std::wstring& path, const core::cleanup::FileExpect& expect) override;
    bool remove_empty_dir(const std::wstring& root, const std::wstring& path) override;
    bool remove_owned_empty_dir(const std::wstring& path) override;
    contracts::Result<std::string> read_prefix(const std::wstring& path, std::size_t max_bytes) override;
    std::optional<std::wstring> volume_of(const std::wstring& path) override;
    std::optional<std::uint64_t> free_bytes(const std::wstring& volume) override;
    // Yalniz yerel sabit birim (DRIVE_FIXED); ag/cikarilabilir/optik -> nullopt (temizlik kapali).
    std::optional<core::cleanup::VolumeId> volume_id(const std::wstring& volume) override;
    contracts::Result<core::cleanup::RecycleBinInfo> query_recycle_bin(const std::wstring& volume) override;
    contracts::Result<std::uint64_t> standby_bytes() override;  // T15
    contracts::Status empty_recycle_bin(const std::wstring& volume) override;

private:
    // Kokun cozulmus gercek yolu ("\\?\" oneksiz); kok baglanti ise veya acilamazsa bos.
    const std::wstring& root_final(const std::wstring& root);
    std::map<std::wstring, std::wstring> root_final_;
};

// Gercek ortam: %LOCALAPPDATA%, kullanici TEMP (uzun ad), %WINDIR%, %PROGRAMDATA%, yukseltme durumu.
core::cleanup::CleanupEnv discover_env();

// Sistem birimi koku (ornek C: kok), %WINDIR% uzerinden; okunamazsa bos.
std::wstring system_volume();

}  // namespace ef::platform::cleanup
