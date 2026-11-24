#include "core/service/channel_choice.h"

namespace ef::core::service {

using contracts::ErrorCode;

bool service_handles(ipc::RequestAction action) {
    return action != ipc::RequestAction::VendorInstall && action != ipc::RequestAction::DriverInstall;
}

bool service_handles_op(ipc::RequestAction action, contracts::OperationId op) {
    if (!service_handles(action)) return false;
    if (op == contracts::OperationId::DriverInstall || op == contracts::OperationId::VendorDriverInstall) return false;
    if (op == contracts::OperationId::DnsServersSet &&
        (action == ipc::RequestAction::Fix || action == ipc::RequestAction::Apply))
        return false;
    return true;
}

bool service_handles_ops(ipc::RequestAction action, const std::vector<contracts::SettingOperation>& ops) {
    if (!service_handles(action)) return false;
    for (const auto& op : ops)
        if (!service_handles_op(action, op.id)) return false;
    return true;
}

Path choose_path(const ServiceProbe& p) {
    const bool all = p.ui_installed && p.admin_member && p.exists && p.running && p.config_ok && p.connected && p.hello_ok;
    if (!all || p.scm_pid == 0 || p.pipe_server_pid == 0 || p.scm_pid != p.pipe_server_pid) return Path::UacBroker;
    return Path::Service;
}

ElevationOutcome NoUacLauncher::launch_elevated(const LaunchArgs&, std::string& detail) {
    ++calls;
    detail = "helper service";
    return ElevationOutcome::Started;
}

contracts::Status HandshakenTransport::send(const ipc::Frame& f) {
    if (!inner_) return contracts::Status::fail(ErrorCode::IoError, "transport closed");
    if (!hello_seen_) {
        if (f.type != ipc::MsgType::Hello) return contracts::Status::fail(ErrorCode::ParseError, "hello expected");
        hello_seen_ = true;
        ack_pending_ = true;
        return contracts::ok_status();
    }
    if (f.type == ipc::MsgType::Hello || f.seq < 2) return contracts::Status::fail(ErrorCode::ParseError, "unexpected hello");
    ipc::Frame g = f;
    g.seq = used_seq_ + (f.seq - 1);  // batch seq 2 -> hizmet seq used_seq + 1
    return inner_->send(g);
}

contracts::Result<ipc::Frame> HandshakenTransport::receive() {
    if (ack_pending_) {
        ack_pending_ = false;
        return ipc::Frame{ipc::MsgType::HelloAck, 0, "{}"};
    }
    if (!inner_) return contracts::Result<ipc::Frame>::fail(ErrorCode::IoError, "transport closed");
    return inner_->receive();
}

}  // namespace ef::core::service
