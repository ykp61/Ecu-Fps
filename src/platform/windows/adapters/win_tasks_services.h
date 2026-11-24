#pragma once
// WIN-04 platform: Gorev Zamanlayici (COM ITaskService) ve SCM. Okuma yukseltmesiz; yazma yonetici ister.

#include "core/adapters/startup/tasks_services.h"

namespace ef::platform::adapters {

class WinTaskApi final : public core::adapters::startup::TaskApi {
public:
    contracts::Result<std::vector<core::adapters::startup::TaskInfo>> list() override;
    contracts::Result<core::adapters::startup::TaskInfo> get(const std::string& path) override;
    contracts::Status set_enabled(const std::string& path, bool enabled) override;
};

class WinServiceApi final : public core::adapters::startup::ServiceApi {
public:
    contracts::Result<std::vector<core::adapters::startup::ServiceInfo>> list() override;
    contracts::Result<core::adapters::startup::ServiceInfo> get(const std::string& name) override;
    contracts::Status set_start_type(const std::string& name, core::adapters::startup::StartType t) override;
    contracts::Status start(const std::string& name) override;
    contracts::Status stop(const std::string& name) override;
};

}  // namespace ef::platform::adapters
