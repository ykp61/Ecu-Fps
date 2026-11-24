#pragma once
// P2 WP3 platform: BackgroundProcessApi gercek Windows uygulamasi. OpenProcess erisimi YALNIZ
// PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION. Surec kapatma/askiya alma/enjeksiyon yok.
// Bu dosya henuz hicbir yerden CAGRILMAZ (arayuze/plana bagli degil); gercek surecte denenmedi.

#include "core/adapters/windows/background_process.h"

namespace ef::platform::adapters {

class WinBackgroundProcessApi final : public core::adapters::windows::BackgroundProcessApi {
public:
    contracts::Status verify(const core::adapters::windows::ProcessId& id) override;
    contracts::Result<std::string> image_path(const core::adapters::windows::ProcessId& id) override;
    contracts::Result<core::adapters::windows::PriorityClass> get_priority(const core::adapters::windows::ProcessId& id) override;
    contracts::Status set_priority(const core::adapters::windows::ProcessId& id, core::adapters::windows::PriorityClass p) override;
    contracts::Result<core::adapters::windows::ThrottleState> get_throttling(const core::adapters::windows::ProcessId& id) override;
    contracts::Status set_throttling(const core::adapters::windows::ProcessId& id, core::adapters::windows::ThrottleState s) override;
};

}  // namespace ef::platform::adapters
