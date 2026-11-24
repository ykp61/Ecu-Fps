// ADR-033 / spec 5: UI kanal secimi ve hizmet yolunda run_admin_batch (UAC yok). Gercek SCM, pipe veya UAC kullanilmaz:
// bellek ici ServiceSession'a ileten sahte tasiyici + NoUacLauncher.
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "core/broker/admin.h"
#include "core/ipc/protocol.h"
#include "core/service/channel_choice.h"
#include "core/service/service_session.h"
#include "ef_test.h"

using namespace ef::core;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;
using service::Path;
using service::ServiceProbe;

namespace {
constexpr std::uint32_t kUiPid = 4242;
const std::string kNonce = "0123456789abcdef0123456789abcdef";

ServiceProbe all_ok() {
    ServiceProbe p;
    p.ui_installed = p.admin_member = p.exists = p.running = p.config_ok = p.connected = p.hello_ok = true;
    p.scm_pid = p.pipe_server_pid = 900;
    return p;
}

SettingOperation hags_op() { return {OperationId::HagsSet, OperationScope::Machine, "hags:hwschmode", "2", true, false}; }

// Hizmet oturumuna (gercek kodlama/cozme) ileten tasiyici. Hello kanal seciminde yapilmis gibi baslar.
class SessionTransport final : public broker::FrameTransport {
public:
    SessionTransport(service::ServiceSession& s, bool drop_responses, std::vector<ipc::Frame>& sent_log)
        : sent(sent_log), s_(s), drop_(drop_responses) {}
    ef::contracts::Status send(const ipc::Frame& f) override {
        auto back = ipc::decode(ipc::encode(f));
        if (!back.ok()) return ef::contracts::Status(back.error());
        sent.push_back(back.value());
        auto r = s_.on_frame(back.value());
        if (!drop_) replies_.push_back(r);
        return ef::contracts::ok_status();
    }
    ef::contracts::Result<ipc::Frame> receive() override {
        if (replies_.empty()) return ef::contracts::Result<ipc::Frame>::fail(ef::contracts::ErrorCode::IoError, "timeout");
        auto f = replies_.front();
        replies_.pop_front();
        return f;
    }
    std::vector<ipc::Frame>& sent;  // Rig sahibi: tasiyici run_admin_batch sonunda yikilir

private:
    service::ServiceSession& s_;
    bool drop_;
    std::deque<ipc::Frame> replies_;
};

struct Rig {
    int handled = 0;
    service::ServiceSession session{kUiPid, [this](const std::string& rid, ipc::RequestAction, const SettingOperation& op) {
                                        ++handled;
                                        broker::StepResult r{op.target, broker::StepState::Applied, true, "ok"};
                                        return broker::encode_result(rid, r);
                                    }};
    std::vector<ipc::Frame> sent;
    std::unique_ptr<broker::FrameTransport> pending;

    explicit Rig(bool drop) {
        // Kanal secimindeki Hello (seq 1) — ServiceFirstChannel'in yaptigi gibi.
        auto ack = session.on_frame(service::make_service_hello(kUiPid, 1));
        EF_CHECK(ack.type == ipc::MsgType::HelloAck);
        auto t = std::make_unique<SessionTransport>(session, drop, sent);
        pending = std::make_unique<service::HandshakenTransport>(std::move(t), 1);
    }
    broker::Connector connector() {
        return [this]() -> ef::contracts::Result<std::unique_ptr<broker::FrameTransport>> {
            if (!pending) return ef::contracts::Result<std::unique_ptr<broker::FrameTransport>>::fail(ef::contracts::ErrorCode::IoError, "used");
            return std::move(pending);
        };
    }
};
}  // namespace

EF_TEST(channel_choice_vendor_install_never_via_service) {
    EF_CHECK(!service::service_handles(ipc::RequestAction::VendorInstall));
    EF_CHECK(!service::service_handles(ipc::RequestAction::DriverInstall));  // sahip karari 3.1: her zaman UAC
}

// Sahip karari 3.1: DNS degistirme ve surucu kurulumu hizmetten (UAC'siz) gecmez; DNS geri alma ve gunluk ayarlar gecer.
EF_TEST(channel_choice_sensitive_ops_need_full_elevation) {
    using A = ipc::RequestAction;
    using O = ef::contracts::OperationId;
    EF_CHECK(!service::service_handles_op(A::Fix, O::DnsServersSet));
    EF_CHECK(!service::service_handles_op(A::Apply, O::DnsServersSet));
    EF_CHECK(service::service_handles_op(A::UndoFix, O::DnsServersSet));
    EF_CHECK(service::service_handles_op(A::Restore, O::DnsServersSet));
    EF_CHECK(service::service_handles_op(A::Recover, O::DnsServersSet));
    EF_CHECK(!service::service_handles_op(A::Fix, O::DriverInstall));
    EF_CHECK(!service::service_handles_op(A::Fix, O::VendorDriverInstall));
    EF_CHECK(service::service_handles_op(A::Apply, O::PowerSettingSet));
    std::vector<ef::contracts::SettingOperation> ops(2);
    ops[0].id = O::PowerSettingSet;
    ops[1].id = O::DnsServersSet;
    EF_CHECK(!service::service_handles_ops(A::Fix, ops));  // biri UAC isterse tumu UAC
    ops.pop_back();
    EF_CHECK(service::service_handles_ops(A::Fix, ops));
    EF_CHECK(service::service_handles_ops(A::Fix, {}));
}

EF_TEST(channel_choice_all_ok_is_service) { EF_CHECK(service::choose_path(all_ok()) == Path::Service); }

EF_TEST(channel_choice_each_false_field_falls_back) {
    bool ServiceProbe::*fields[] = {&ServiceProbe::ui_installed, &ServiceProbe::exists,    &ServiceProbe::running,
                                    &ServiceProbe::config_ok,    &ServiceProbe::connected, &ServiceProbe::hello_ok,
                                    &ServiceProbe::admin_member};
    for (auto f : fields) {
        auto p = all_ok();
        p.*f = false;
        EF_CHECK(service::choose_path(p) == Path::UacBroker);
    }
}

EF_TEST(channel_choice_standard_account_uses_uac_broker) {
    // K27: hizmet standart hesaba hizmet vermez; her sey dogru olsa da UI hesabi yonetici grubunda degilse UAC broker.
    auto p = all_ok();
    p.admin_member = false;
    EF_CHECK(service::choose_path(p) == Path::UacBroker);
}

EF_TEST(channel_choice_pid_mismatch_or_zero_falls_back) {
    auto p = all_ok();
    p.pipe_server_pid = 901;  // pipe baska surece ait (ad gaspi)
    EF_CHECK(service::choose_path(p) == Path::UacBroker);
    p = all_ok();
    p.scm_pid = p.pipe_server_pid = 0;
    EF_CHECK(service::choose_path(p) == Path::UacBroker);
    p = all_ok();
    p.scm_pid = 0;
    EF_CHECK(service::choose_path(p) == Path::UacBroker);
}

EF_TEST(service_batch_no_uac_and_requests_reach_session) {
    Rig rig(false);
    service::NoUacLauncher launcher;
    const auto b = broker::run_admin_batch(launcher, kUiPid, kNonce, rig.connector(), ipc::RequestAction::Apply, {hags_op()});
    EF_CHECK(b.elevation == ElevationOutcome::Started);
    EF_CHECK(b.steps.size() == 1 && b.steps[0].state == broker::StepState::Applied && !b.steps[0].unknown);
    EF_CHECK(rig.handled == 1);
    // Batch'in nonce'lu Hello'su hizmete gitmedi; hizmete giden ilk cerceve seq 2 Request, ardindan Bye.
    EF_CHECK(rig.sent.size() == 2);
    if (rig.sent.size() != 2) return;
    EF_CHECK(rig.sent[0].type == ipc::MsgType::Request && rig.sent[0].seq == 2);
    EF_CHECK(rig.sent[1].type == ipc::MsgType::Bye && rig.sent[1].seq == 3);
    for (const auto& f : rig.sent) EF_CHECK(f.payload.find(kNonce) == std::string::npos);
    EF_CHECK(rig.session.closed() && rig.session.last_error().empty());
    // NoUacLauncher hicbir surec baslatmaz: yalniz sayac.
    EF_CHECK(launcher.calls == 1);
}

EF_TEST(service_batch_missing_reply_is_unknown_not_retried) {
    Rig rig(true);  // hizmet istegi aldi ama yanit gelmedi
    service::NoUacLauncher launcher;
    const auto b = broker::run_admin_batch(launcher, kUiPid, kNonce, rig.connector(), ipc::RequestAction::Apply, {hags_op()});
    EF_CHECK(b.elevation == ElevationOutcome::Started);
    EF_CHECK(b.steps.size() == 1);
    EF_CHECK(b.steps[0].state == broker::StepState::Failed && b.steps[0].unknown);
    EF_CHECK(rig.handled == 1);  // hizmet tarafinda islenmis olabilir: "degismedi" denmez
    EF_CHECK(launcher.calls == 1);  // tek "baslatma" (UAC yok), ikinci deneme yok
}

EF_TEST(service_batch_disallowed_op_never_connects) {
    Rig rig(false);
    service::NoUacLauncher launcher;
    SettingOperation bad{OperationId::HagsSet, OperationScope::CurrentUser, "hags:hwschmode", "2", true, false};
    const auto b = broker::run_admin_batch(launcher, kUiPid, kNonce, rig.connector(), ipc::RequestAction::Apply, {bad});
    EF_CHECK(b.steps.size() == 1 && b.steps[0].state == broker::StepState::Rejected);
    EF_CHECK(launcher.calls == 0);
    EF_CHECK(rig.pending != nullptr);  // tasiyici hic alinmadi
    EF_CHECK(rig.handled == 0);
}

EF_TEST(handshaken_transport_requires_hello_first) {
    Rig rig(false);
    auto& t = *rig.pending;
    EF_CHECK(!t.send(ipc::make_request("r1", hags_op(), 2)).ok());
    EF_CHECK(rig.sent.empty());
}
