#pragma once
// WIN-03 platform: gercek Run anahtarlari (64/32 bit gorunum), Startup klasorleri ve StartupApproved.

#include "core/adapters/startup/startup.h"

namespace ef::platform::adapters {

class WinStartupApi final : public core::adapters::startup::StartupApi {
public:
    contracts::Result<std::vector<core::adapters::startup::Entry>> list(core::adapters::startup::Location l) override;
    contracts::Result<bool> exists(core::adapters::startup::Location l, const std::string& name) override;
    contracts::Result<std::optional<std::string>> get_raw(core::adapters::startup::Location l, const std::string& name) override;
    contracts::Status set_raw(core::adapters::startup::Location l, const std::string& name, const std::string& raw_hex) override;
    contracts::Status remove_raw(core::adapters::startup::Location l, const std::string& name) override;
    std::uint64_t now_filetime() override;
};

}  // namespace ef::platform::adapters
