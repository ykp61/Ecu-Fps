// T3b (ADR-043 ek): eski hizmete saglik sinyali, Win32 tasima.
#include "platform/windows/legacy_health_signal.h"

#include <windows.h>

#include <memory>

#include "core/legacy/legacy_entry.h"
#include "core/legacy/legacy_install.h"
#include "core/legacy/legacy_upgrade.h"
#include "platform/windows/elevation.h"
#include "platform/windows/install/win_setup_ops.h"
#include "platform/windows/known_roots.h"
#include "platform/windows/pipe.h"
#include "platform/windows/win_env.h"

namespace ef::platform {

namespace {

constexpr std::uint32_t kConnectMs = 3000;
constexpr std::uint32_t kSendMs = 5000;

class WinLegacyHealthPort final : public core::legacy::LegacyHealthPort {
public:
    core::legacy::LegacyServiceView query_old_service() override {
        core::legacy::LegacyServiceView v;
        const auto q = install::query_named_service(core::legacy::legacy_health_service());
        if (!q || !q.value().exists) return v;
        v.exists = true;
        v.running = q.value().state == SERVICE_RUNNING;
        v.pid = q.value().pid;
        v.image_path = q.value().image_path;
        return v;
    }
    bool connect(std::uint32_t expected_server_pid) override {
        auto c = PipeClient::connect_service(std::wstring(core::legacy::legacy_health_pipe()), expected_server_pid, kConnectMs);
        if (!c.ok()) return false;
        client_ = std::move(c).value();
        return client_->server_pid() == expected_server_pid;
    }
    bool write(const core::ipc::Frame& f) override { return client_ && client_->write_frame(f, kSendMs).ok(); }
    std::optional<core::ipc::Frame> read(std::uint32_t timeout_ms) override {
        if (!client_) return std::nullopt;
        auto r = client_->read_frame(timeout_ms);
        if (!r.ok()) return std::nullopt;
        return std::move(r).value();
    }

private:
    std::unique_ptr<PipeClient> client_;
};

}  // namespace

bool launched_from_legacy_install() {
    const auto legacy = core::legacy::legacy_layout(known_roots());
    return core::legacy::legacy_service_candidate(self_exe_path().native(), legacy) &&
           core::legacy::same_canonical_path(normalized_self_exe().native(), legacy.exe.native());
}

core::legacy::LegacyRoute legacy_launch_route(std::wstring_view command_line) {
    const auto roots = known_roots();
    const auto self = normalized_self_exe();
    const auto kind = core::legacy::classify_legacy_invocation(command_line, self, roots);
    if (kind == core::legacy::LegacyInvocation::None) return core::legacy::LegacyRoute::Normal;
    bool record = false;
    if (core::legacy::legacy_action(kind) == core::legacy::LegacyAction::RefuseUpgradePending) {
        const auto file = to_wide(core::legacy::make_upgrade_paths(roots, self).record_file());
        if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) {
            record = true;
        } else {
            const DWORD e = GetLastError();
            record = e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND;  // erisim reddi vb.: var say (reddet)
        }
    }
    const auto route = core::legacy::legacy_launch_route(kind, record);
    OutputDebugStringA(("legacy_launch kind=" + std::string(core::legacy::to_string(kind)) + " route=" + std::string(core::legacy::to_string(route)) +
                        "\n").c_str());
    return route;
}

core::legacy::LegacyHealthOutcome send_legacy_health_signal(std::string_view version) {
    if (!launched_from_legacy_install()) return core::legacy::LegacyHealthOutcome::NoOldService;
    WinLegacyHealthPort port;
    const auto out = core::legacy::send_legacy_health(port, core::legacy::legacy_layout(known_roots()),
                                                      static_cast<std::uint32_t>(GetCurrentProcessId()), version);
    OutputDebugStringA(("legacy_health outcome=" + std::string(core::legacy::to_string(out)) + "\n").c_str());
    return out;
}

}  // namespace ef::platform
