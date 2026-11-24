// ADR-033 / spec 5: yardimci hizmet pipe oturumu (saf durum makinesi). Gercek pipe acilmaz.
#include <optional>
#include <string>

#include "core/ipc/protocol.h"
#include "core/service/service_session.h"
#include "ef_test.h"

using namespace ef::core;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;
using service::ServiceSession;

namespace {
constexpr std::uint32_t kPid = 4242;

SettingOperation hags_op() { return {OperationId::HagsSet, OperationScope::Machine, "hags:hwschmode", "2", true, false}; }

struct Calls {
    int n = 0;
    std::string rid;
    ipc::RequestAction action = ipc::RequestAction::Apply;
    SettingOperation op;
};

ServiceSession::Handler recorder(Calls& c) {
    return [&c](const std::string& rid, ipc::RequestAction a, const SettingOperation& op) {
        ++c.n;
        c.rid = rid;
        c.action = a;
        c.op = op;
        return std::string(R"({"ok":true})");
    };
}

ipc::Frame hello_payload(const std::string& payload, std::uint64_t seq = 1) { return ipc::Frame{ipc::MsgType::Hello, seq, payload}; }
}  // namespace

EF_TEST(service_session_hello_then_request_calls_handler) {
    Calls c;
    ServiceSession s(kPid, recorder(c));
    auto ack = s.on_frame(service::make_service_hello(kPid, 1));
    EF_CHECK(ack.type == ipc::MsgType::HelloAck);
    EF_CHECK(s.ready());
    auto r = s.on_frame(ipc::make_request("r1", hags_op(), 2, ipc::RequestAction::Fix));
    EF_CHECK(r.type == ipc::MsgType::Response);
    EF_CHECK(r.payload == R"({"ok":true})");
    EF_CHECK(c.n == 1);
    EF_CHECK(c.rid == "r1");
    EF_CHECK(c.action == ipc::RequestAction::Fix);
    EF_CHECK(c.op.id == OperationId::HagsSet && c.op.target == "hags:hwschmode" && c.op.new_value == "2");
    EF_CHECK(r.seq == 2);
}

EF_TEST(service_session_vendor_install_rejected) {
    Calls c;
    ServiceSession s(kPid, recorder(c));
    EF_CHECK(s.on_frame(service::make_service_hello(kPid, 1)).type == ipc::MsgType::HelloAck);
    EF_CHECK(s.on_frame(ipc::make_request("r1", hags_op(), 2, ipc::RequestAction::VendorInstall)).type == ipc::MsgType::Error);
    EF_CHECK(c.n == 0);
}

// Sahip karari 3.1: DNS degistirme ve surucu kurulumu hizmet tarafinda da reddedilir (UI atlatilsa bile);
// DNS'in kayitli onceki degere geri donusu hizmette kalir.
EF_TEST(service_session_dns_change_and_driver_install_rejected) {
    const SettingOperation dns{OperationId::DnsServersSet, OperationScope::Machine, "dns:{00000000-0000-0000-0000-000000000001}",
                               "1.1.1.1,1.0.0.1", false, false};
    {
        Calls c;
        ServiceSession s(kPid, recorder(c));
        EF_CHECK(s.on_frame(service::make_service_hello(kPid, 1)).type == ipc::MsgType::HelloAck);
        EF_CHECK(s.on_frame(ipc::make_request("r1", dns, 2, ipc::RequestAction::Fix)).type == ipc::MsgType::Error);
        EF_CHECK(c.n == 0);
    }
    {
        Calls c;
        ServiceSession s(kPid, recorder(c));
        EF_CHECK(s.on_frame(service::make_service_hello(kPid, 1)).type == ipc::MsgType::HelloAck);
        EF_CHECK(s.on_frame(ipc::make_request("r1", hags_op(), 2, ipc::RequestAction::DriverInstall)).type == ipc::MsgType::Error);
        EF_CHECK(c.n == 0);
    }
    {
        Calls c;
        ServiceSession s(kPid, recorder(c));
        EF_CHECK(s.on_frame(service::make_service_hello(kPid, 1)).type == ipc::MsgType::HelloAck);
        EF_CHECK(s.on_frame(ipc::make_request("r1", dns, 2, ipc::RequestAction::UndoFix)).type == ipc::MsgType::Response);
        EF_CHECK(c.n == 1);
    }
}

EF_TEST(service_session_hello_wrong_pid) {
    Calls c;
    ServiceSession s(kPid, recorder(c));
    auto e = s.on_frame(service::make_service_hello(kPid + 1, 1));
    EF_CHECK(e.type == ipc::MsgType::Error);
    EF_CHECK(s.closed());
    EF_CHECK(s.on_frame(ipc::make_request("r1", hags_op(), 2)).type == ipc::MsgType::Error);
    EF_CHECK(c.n == 0);
}

EF_TEST(service_session_hello_zero_observed_pid_rejected) {
    Calls c;
    ServiceSession s(0, recorder(c));
    EF_CHECK(s.on_frame(hello_payload(R"({"proto":1,"pid":0})")).type == ipc::MsgType::Error);
}

EF_TEST(service_session_hello_bad_json) {
    for (const char* p : {"", "{", "[]", R"({"proto":1})", R"({"pid":4242})", R"({"proto":"1","pid":4242})",
                          R"({"proto":2,"pid":4242})", R"({"proto":1,"pid":"4242"})"}) {
        Calls c;
        ServiceSession s(kPid, recorder(c));
        EF_CHECK(s.on_frame(hello_payload(p)).type == ipc::MsgType::Error);
        EF_CHECK(s.closed());
    }
}

EF_TEST(service_session_hello_extra_field_rejected) {
    Calls c;
    ServiceSession s(kPid, recorder(c));
    // Broker'in nonce'lu Hello'su da reddedilir (yalniz {"proto","pid"}).
    EF_CHECK(s.on_frame(hello_payload(R"({"proto":1,"pid":4242,"nonce":"0123456789abcdef0123456789abcdef"})")).type ==
              ipc::MsgType::Error);
    ServiceSession s2(kPid, recorder(c));
    EF_CHECK(s2.on_frame(ipc::make_hello("0123456789abcdef0123456789abcdef", kPid, 1)).type == ipc::MsgType::Error);
}

EF_TEST(service_session_request_before_hello) {
    Calls c;
    ServiceSession s(kPid, recorder(c));
    EF_CHECK(s.on_frame(ipc::make_request("r1", hags_op(), 1)).type == ipc::MsgType::Error);
    EF_CHECK(s.closed());
    EF_CHECK(c.n == 0);
}

EF_TEST(service_session_seq_replay_and_gap) {
    Calls c;
    ServiceSession s(kPid, recorder(c));
    (void)s.on_frame(service::make_service_hello(kPid, 1));
    EF_CHECK(s.on_frame(ipc::make_request("r1", hags_op(), 2)).type == ipc::MsgType::Response);
    EF_CHECK(s.on_frame(ipc::make_request("r1", hags_op(), 2)).type == ipc::MsgType::Error);  // tekrar
    EF_CHECK(s.closed());
    EF_CHECK(c.n == 1);
    ServiceSession s2(kPid, recorder(c));
    (void)s2.on_frame(service::make_service_hello(kPid, 1));
    EF_CHECK(s2.on_frame(ipc::make_request("r1", hags_op(), 4)).type == ipc::MsgType::Error);  // atlama
    ServiceSession s3(kPid, recorder(c));
    EF_CHECK(s3.on_frame(service::make_service_hello(kPid, 0)).type == ipc::MsgType::Error);  // seq 0
}

EF_TEST(service_session_max_64_requests) {
    Calls c;
    ServiceSession s(kPid, recorder(c));
    (void)s.on_frame(service::make_service_hello(kPid, 1));
    std::uint64_t seq = 1;
    for (int i = 0; i < service::kMaxServiceRequests; ++i)
        EF_CHECK(s.on_frame(ipc::make_request("r" + std::to_string(i), hags_op(), ++seq)).type == ipc::MsgType::Response);
    EF_CHECK(c.n == 64);
    EF_CHECK(s.on_frame(ipc::make_request("r65", hags_op(), ++seq)).type == ipc::MsgType::Error);
    EF_CHECK(s.closed());
    EF_CHECK(c.n == 64);
}

EF_TEST(service_session_bye_closes_cleanly) {
    Calls c;
    ServiceSession s(kPid, recorder(c));
    (void)s.on_frame(service::make_service_hello(kPid, 1));
    auto b = s.on_frame(ipc::Frame{ipc::MsgType::Bye, 2, "{}"});
    EF_CHECK(b.type == ipc::MsgType::Bye);
    EF_CHECK(s.closed());
    EF_CHECK(s.last_error().empty());
    EF_CHECK(s.on_frame(ipc::make_request("r1", hags_op(), 3)).type == ipc::MsgType::Error);
    EF_CHECK(c.n == 0);
}

EF_TEST(service_session_unknown_or_unexpected_type) {
    for (auto t : {ipc::MsgType::Cancel, ipc::MsgType::Hello, ipc::MsgType::HelloAck, ipc::MsgType::Response, ipc::MsgType::Error}) {
        Calls c;
        ServiceSession s(kPid, recorder(c));
        (void)s.on_frame(service::make_service_hello(kPid, 1));
        EF_CHECK(s.on_frame(ipc::Frame{t, 2, "{}"}).type == ipc::MsgType::Error);
        EF_CHECK(s.closed());
    }
    // Bilinmeyen tur degeri (cozumleyici zaten reddeder; oturum da reddeder).
    Calls c;
    ServiceSession s(kPid, recorder(c));
    (void)s.on_frame(service::make_service_hello(kPid, 1));
    EF_CHECK(s.on_frame(ipc::Frame{static_cast<ipc::MsgType>(42), 2, "{}"}).type == ipc::MsgType::Error);
}

EF_TEST(service_session_malformed_request_rejected) {
    Calls c;
    ServiceSession s(kPid, recorder(c));
    (void)s.on_frame(service::make_service_hello(kPid, 1));
    EF_CHECK(s.on_frame(ipc::Frame{ipc::MsgType::Request, 2, R"({"request_id":"r1","op":{},"cmd":"x"})"}).type == ipc::MsgType::Error);
    EF_CHECK(c.n == 0);
}

EF_TEST(service_session_extra_hook_default_rejects) {
    Calls c;
    ServiceSession s(kPid, recorder(c));
    (void)s.on_frame(service::make_service_hello(kPid, 1));
    EF_CHECK(s.on_frame(ipc::Frame{ipc::MsgType::Cancel, 2, "{}"}).type == ipc::MsgType::Error);

    // Hook nullopt donerse de red.
    int seen = 0;
    ServiceSession s2(kPid, recorder(c), [&seen](const ipc::Frame&) -> std::optional<ipc::Frame> {
        ++seen;
        return std::nullopt;
    });
    (void)s2.on_frame(service::make_service_hello(kPid, 1));
    EF_CHECK(s2.on_frame(ipc::Frame{ipc::MsgType::UpdateChunk, 2, "{}"}).type == ipc::MsgType::Error);
    EF_CHECK(seen == 1);

    // Hook yanit verirse (ADR-037: yalniz guncelleme turleri 8..11 hook'a gider) oturum seq'i atar; Request yolu hook'a gitmez.
    ServiceSession s3(kPid, recorder(c), [](const ipc::Frame& in) -> std::optional<ipc::Frame> {
        return ipc::Frame{ipc::MsgType::Response, 999, "ext:" + in.payload};
    });
    (void)s3.on_frame(service::make_service_hello(kPid, 1));
    auto r = s3.on_frame(ipc::Frame{ipc::MsgType::UpdateChunk, 2, "x"});
    EF_CHECK(r.type == ipc::MsgType::Response && r.payload == "ext:x" && r.seq == 2);
    EF_CHECK(s3.on_frame(ipc::make_request("r1", hags_op(), 3)).type == ipc::MsgType::Response);
    EF_CHECK(c.n == 1);
}

EF_TEST(service_hello_payload_shape) {
    auto h = service::make_service_hello(7, 1);
    EF_CHECK(h.type == ipc::MsgType::Hello && h.seq == 1);
    EF_CHECK(h.payload == R"({"proto":1,"pid":7})");
}

// Denetim 3.2: Hello'suz baglanti 5 sn, Ready iken bos bekleme 60 sn, toplam omur 2 saat ile kesilir.
EF_TEST(service_connection_timer_limits) {
    using T = service::ConnectionTimer;
    T t(1000);
    EF_CHECK(!t.expired(1000 + T::kHelloMs, false));
    EF_CHECK(t.expired(1001 + T::kHelloMs, false));     // Hello gelmedi
    EF_CHECK(!t.expired(1001 + T::kHelloMs, true));     // Ready: uzun bos bekleme hakki
    EF_CHECK(t.expired(1001 + T::kIdleMs, true));
    std::uint64_t now = 1000;
    for (int i = 0; i < 150; ++i) { now += 50000; t.frame(now); }  // her 50 sn cerceve: 125 dk
    EF_CHECK(t.expired(now, true));                     // toplam omur doldu
}
