// T3b (ADR-043 ek): eski hizmete saglik sinyali (saf sira).
#include "core/legacy/legacy_health.h"

#include "core/legacy/legacy_entry.h"
#include "core/service/service_session.h"
#include "core/update/staged_update.h"

namespace ef::core::legacy {

std::string_view to_string(LegacyHealthOutcome o) noexcept {
    switch (o) {
        case LegacyHealthOutcome::NoOldService: return "no_old_service";
        case LegacyHealthOutcome::NotRunning: return "not_running";
        case LegacyHealthOutcome::ImageMismatch: return "image_mismatch";
        case LegacyHealthOutcome::PipeUnavailable: return "pipe_unavailable";
        case LegacyHealthOutcome::HelloRejected: return "hello_rejected";
        case LegacyHealthOutcome::Rejected: return "rejected";
        case LegacyHealthOutcome::Confirmed: return "confirmed";
    }
    return "?";
}

std::vector<ipc::Frame> legacy_health_frames(std::uint32_t self_pid, std::string_view version) {
    return {service::make_service_hello(self_pid, 1), update::make_update_healthy(version, 2)};
}

LegacyHealthOutcome send_legacy_health(LegacyHealthPort& port, const setup::InstallLayout& legacy, std::uint32_t self_pid,
                                       std::string_view version) {
    const auto svc = port.query_old_service();
    if (!svc.exists) return LegacyHealthOutcome::NoOldService;
    if (!svc.running || svc.pid == 0) return LegacyHealthOutcome::NotRunning;
    const auto want = setup::service_image_path(legacy);
    if (legacy.exe.empty() || svc.image_path.size() != want.size() || !svc.image_path.ends_with(L"\" --service") ||
        !same_canonical_path(image_path_exe(svc.image_path).value_or(L""), legacy.exe.native()))
        return LegacyHealthOutcome::ImageMismatch;
    if (!port.connect(svc.pid)) return LegacyHealthOutcome::PipeUnavailable;
    const auto frames = legacy_health_frames(self_pid, version);
    if (!port.write(frames[0])) return LegacyHealthOutcome::HelloRejected;
    const auto ack = port.read(kLegacyHealthReplyMs);
    if (!ack || ack->type != ipc::MsgType::HelloAck) return LegacyHealthOutcome::HelloRejected;
    if (!port.write(frames[1])) return LegacyHealthOutcome::Rejected;
    const auto st = port.read(kLegacyHealthReplyMs);
    if (!st || st->type != ipc::MsgType::UpdateStatus) return LegacyHealthOutcome::Rejected;
    const auto d = update::decode_status(st->payload);
    return d.ok() && d.value().state == update::StatusState::Committed ? LegacyHealthOutcome::Confirmed : LegacyHealthOutcome::Rejected;
}

}  // namespace ef::core::legacy
