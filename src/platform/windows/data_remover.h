#pragma once
// REC-06 platform: gercek veri silici. Yollar enjekte edilebilir (testler gercek AppData'ya dokunmaz).

#include <filesystem>
#include <string>

#include "core/recovery/uninstall.h"

namespace ef::platform {

class FsDataRemover final : public core::recovery::DataRemover {
public:
    FsDataRemover(std::filesystem::path user_dir, std::filesystem::path machine_dir, std::wstring run_key)
        : user_(std::move(user_dir)), machine_(std::move(machine_dir)), run_key_(std::move(run_key)) {}
    // Uretim yollari: %LOCALAPPDATA%\EcuFps, %ProgramData%\EcuFps, HKCU Run.
    static FsDataRemover production();

    contracts::Status remove_autostart() override;
    contracts::Status remove_user_data() override;
    contracts::Status remove_machine_data() override;

private:
    std::filesystem::path user_, machine_;
    std::wstring run_key_;
};

}  // namespace ef::platform
