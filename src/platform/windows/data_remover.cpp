#include "platform/windows/data_remover.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include "platform/windows/elevation.h"
#include "platform/windows/run_key_store.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/win_env.h"

namespace ef::platform {

using contracts::ErrorCode;
using contracts::Status;

namespace {
Status remove_tree(const std::filesystem::path& p) {
    std::error_code ec;
    if (p.empty() || !std::filesystem::exists(p, ec)) return contracts::ok_status();
    // Guvenlik: yalniz "EcuFps" ile baslayan klasor silinir (yanlis yolla kullanici verisi silinmez).
    if (p.filename().wstring().rfind(L"EcuFps", 0) != 0)
        return Status::fail(ErrorCode::Denied, "refusing to delete a non-EcuFps folder");
    std::filesystem::remove_all(p, ec);
    if (ec) return Status::fail(ec == std::errc::permission_denied ? ErrorCode::Denied : ErrorCode::IoError, ec.message());
    return contracts::ok_status();
}
}  // namespace

FsDataRemover FsDataRemover::production() {
    std::filesystem::path user, machine;
    if (auto d = app_data_dir()) user = *d;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &p))) {
        machine = std::filesystem::path(p) / L"EcuFps";
        CoTaskMemFree(p);
    }
    return FsDataRemover(user, machine, kRunKey);
}

// ADR-030: yeni ("EcuFps") ve eski ("Ecu Fps") adli kayitlarin ikisi de silinir ve yoklugu geri okunur.
Status FsDataRemover::remove_autostart() {
    WinRunKeyStore store(run_key_);
    return core::settings::autostart_remove_all(store);
}

Status FsDataRemover::remove_user_data() { return remove_tree(user_); }

Status FsDataRemover::remove_machine_data() {
    std::error_code ec;
    if (machine_.empty() || !std::filesystem::exists(machine_, ec)) return contracts::ok_status();
    if (!is_process_elevated()) return Status::fail(ErrorCode::Denied, "machine data requires administrator");
    return remove_tree(machine_);
}

}  // namespace ef::platform
