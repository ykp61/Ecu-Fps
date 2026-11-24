// ADR-033 / spec 4: kurulum iscisi istek/ilerleme/yanit kodlayicisi ve sunucu oturumu (saf; pipe/surec yok).
#include <string>

#include "core/ipc/protocol.h"
#include "core/setup/setup_protocol.h"
#include "ef_test.h"

using namespace ef::core::setup;
namespace ipc = ef::core::ipc;

namespace {
const std::string kNonce = "0123456789abcdef0123456789abcdef";
constexpr std::uint32_t kPid = 4242;

ipc::Frame request(std::uint64_t seq, const std::string& payload) { return ipc::Frame{ipc::MsgType::Request, seq, payload}; }

SetupServerSession ready_session() {
    SetupServerSession s(kNonce, kPid);
    auto st = s.on_frame(ipc::make_hello(kNonce, kPid, 1), kPid);
    EF_CHECK(st.send && st.send->type == ipc::MsgType::HelloAck && !st.run);
    EF_CHECK(s.ready());
    return s;
}
}  // namespace

EF_TEST(setup_protocol_request_round_trip) {
    for (const auto k : {SetupKind::Install, SetupKind::Uninstall, SetupKind::Repair}) {
        auto d = decode_request(encode_request(SetupRequest{k, false}));
        EF_CHECK(d.ok() && d.value().kind == k && !d.value().autostart_unused);
    }
    EF_CHECK(encode_request(SetupRequest{SetupKind::Repair, true}) == R"({"kind":"repair"})");  // autostart tele konmaz
}

EF_TEST(setup_protocol_request_rejects_extra_unknown_and_paths) {
    EF_CHECK(!decode_request(R"({"kind":"install","path":"C:\\evil.exe"})").ok());
    EF_CHECK(!decode_request(R"({"kind":"install","autostart":true})").ok());
    EF_CHECK(!decode_request(R"({"kind":"format"})").ok());
    EF_CHECK(!decode_request(R"({"kind":"C:\\Windows\\System32\\cmd.exe"})").ok());
    EF_CHECK(!decode_request(R"({"exe":"x"})").ok());
    EF_CHECK(!decode_request(R"({})").ok());
    EF_CHECK(!decode_request(R"(["install"])").ok());
    EF_CHECK(!decode_request(R"({"kind":1})").ok());
    EF_CHECK(!decode_request("not json").ok());
}

EF_TEST(setup_protocol_progress_round_trip) {
    for (const auto rec : {StepRecord::Started, StepRecord::Done, StepRecord::Undone, StepRecord::UndoFailed, StepRecord::Committed}) {
        auto d = decode_response(encode_progress(SetupProgress{SetupStep::CopyExe, rec}));
        EF_CHECK(d.ok() && d.value().progress && !d.value().reply);
        EF_CHECK(d.ok() && d.value().progress->step == SetupStep::CopyExe && d.value().progress->rec == rec);
    }
    EF_CHECK(!decode_response(R"({"progress":{"step":"Bogus","rec":"Done"}})").ok());
    EF_CHECK(!decode_response(R"({"progress":{"step":"Arp","rec":"Done","x":1}})").ok());
    EF_CHECK(!decode_response(R"({"progress":{"step":"Arp","rec":"Done"},"result":{}})").ok());
}

EF_TEST(setup_protocol_reply_round_trip) {
    SetupReply ok;
    ok.result.ok = true;
    ok.result.done = {SetupStep::Probe, SetupStep::CreateDir, SetupStep::Commit};
    auto d = decode_response(encode_reply(ok));
    EF_CHECK(d.ok() && d.value().reply && !d.value().progress);
    const auto& r = d.value().reply->result;
    EF_CHECK(r.ok && !r.failed_at && r.rollback_clean && r.undo_failed.empty() && !r.reboot_pending && !r.machine_data_kept);
    EF_CHECK(r.done == ok.result.done && d.value().reply->error.empty());

    SetupReply bad;
    bad.result.ok = false;
    bad.result.failed_at = SetupStep::ServiceStart;
    bad.result.rollback_clean = false;
    bad.result.undo_failed = {SetupStep::Shortcut};
    bad.result.reboot_pending = true;
    bad.result.machine_data_kept = true;
    bad.error = "busy";
    auto e = decode_response(encode_reply(bad));
    EF_CHECK(e.ok() && e.value().reply);
    const auto& b = e.value().reply->result;
    EF_CHECK(!b.ok && b.failed_at == SetupStep::ServiceStart && !b.rollback_clean && b.undo_failed == bad.result.undo_failed);
    EF_CHECK(b.reboot_pending && b.machine_data_kept && b.done.empty() && e.value().reply->error == "busy");
}

EF_TEST(setup_protocol_reply_long_or_non_ascii_error_still_decodes) {
    // Yerel kod sayfasi (CP1254 "Erisim engellendi") veya uzun ayrinti: yanit yine cozulebilir, sonuc kaybolmaz.
    SetupReply r;
    r.result.ok = false;
    r.result.failed_at = SetupStep::CopyExe;
    r.error = std::string("remove: Eri\xfEim engellendi\r\n") + std::string(2000, 'x');
    auto d = decode_response(encode_reply(r));
    EF_CHECK(d.ok() && d.value().reply);
    if (!d.ok() || !d.value().reply) return;
    const auto& e = d.value().reply->error;
    EF_CHECK(e.size() == kMaxReplyError);
    EF_CHECK(e.rfind("remove: Eri?im engellendi??", 0) == 0);
    EF_CHECK(!d.value().reply->result.ok && d.value().reply->result.failed_at == SetupStep::CopyExe);
}

EF_TEST(setup_protocol_reply_rejects_malformed) {
    EF_CHECK(!decode_response(R"({"result":{"ok":true},"error":""})").ok());  // eksik alan
    EF_CHECK(!decode_response(
                   R"({"result":{"ok":true,"rollback_clean":true,"undo_failed":[],"reboot_pending":false,"done":[],"machine_data_kept":false,"cmd":"x"},"error":""})")
                   .ok());
    EF_CHECK(!decode_response(
                   R"({"result":{"ok":true,"rollback_clean":true,"undo_failed":[],"reboot_pending":false,"done":[],"machine_data_kept":false},"error":"","path":"C:\\x"})")
                   .ok());
    EF_CHECK(!decode_response(
                   R"({"result":{"ok":true,"failed_at":"Arp","rollback_clean":true,"undo_failed":[],"reboot_pending":false,"done":[],"machine_data_kept":false},"error":""})")
                   .ok());  // ok + failed_at celiskili
    EF_CHECK(!decode_response(R"({})").ok());
}

EF_TEST(setup_session_happy_path_one_request) {
    auto s = ready_session();
    auto st = s.on_frame(request(2, encode_request(SetupRequest{SetupKind::Install, false})), kPid);
    EF_CHECK(!st.send && st.run && st.run->kind == SetupKind::Install && s.request_taken());
    const auto p = s.progress_frame(SetupProgress{SetupStep::Probe, StepRecord::Started});
    const auto r = s.reply_frame(SetupReply{});
    EF_CHECK(p.type == ipc::MsgType::Response && r.type == ipc::MsgType::Response && r.seq > p.seq);
    st = s.on_frame(ipc::Frame{ipc::MsgType::Bye, 3, "{}"}, kPid);
    EF_CHECK(st.send && st.send->type == ipc::MsgType::Bye && s.closed() && s.last_error().empty());
}

EF_TEST(setup_session_wrong_nonce_or_pid_rejected) {
    {
        SetupServerSession s(kNonce, kPid);
        auto st = s.on_frame(ipc::make_hello("ffffffffffffffffffffffffffffffff", kPid, 1), kPid);
        EF_CHECK(st.send && st.send->type == ipc::MsgType::Error && s.closed() && !st.run);
    }
    {
        SetupServerSession s(kNonce, kPid);
        auto st = s.on_frame(ipc::make_hello(kNonce, kPid + 1, 1), kPid);  // Hello icindeki PID yanlis
        EF_CHECK(st.send && st.send->type == ipc::MsgType::Error && s.closed());
    }
    {
        SetupServerSession s(kNonce, kPid);
        auto st = s.on_frame(ipc::make_hello(kNonce, kPid, 1), kPid + 7);  // baska surec
        EF_CHECK(st.send && st.send->type == ipc::MsgType::Error && s.closed());
    }
    {
        auto s = ready_session();
        auto st = s.on_frame(request(2, encode_request(SetupRequest{})), kPid + 1);  // hello sonrasi baska surec
        EF_CHECK(st.send && st.send->type == ipc::MsgType::Error && !st.run && s.closed());
    }
    {
        SetupServerSession s(kNonce, kPid);
        auto st = s.on_frame(request(1, encode_request(SetupRequest{})), kPid);  // Hello olmadan istek
        EF_CHECK(st.send && st.send->type == ipc::MsgType::Error && !st.run && s.closed());
    }
}

EF_TEST(setup_session_sequence_rules) {
    {
        SetupServerSession s(kNonce, kPid);
        auto st = s.on_frame(ipc::make_hello(kNonce, kPid, 2), kPid);  // ilk seq 1 olmali
        EF_CHECK(st.send && st.send->type == ipc::MsgType::Error && s.closed());
    }
    {
        auto s = ready_session();
        auto st = s.on_frame(request(1, encode_request(SetupRequest{})), kPid);  // tekrar (replay)
        EF_CHECK(st.send && st.send->type == ipc::MsgType::Error && !st.run && s.closed());
    }
    {
        auto s = ready_session();
        auto st = s.on_frame(request(5, encode_request(SetupRequest{})), kPid);  // atlama
        EF_CHECK(st.send && st.send->type == ipc::MsgType::Error && !st.run && s.closed());
    }
}

EF_TEST(setup_session_second_request_and_bad_payload_rejected) {
    {
        auto s = ready_session();
        auto st = s.on_frame(request(2, encode_request(SetupRequest{SetupKind::Repair, false})), kPid);
        EF_CHECK(st.run);
        st = s.on_frame(request(3, encode_request(SetupRequest{SetupKind::Uninstall, false})), kPid);
        EF_CHECK(st.send && st.send->type == ipc::MsgType::Error && !st.run && s.closed());
        EF_CHECK(s.last_error() == "one request per session");
    }
    {
        auto s = ready_session();
        auto st = s.on_frame(request(2, R"({"kind":"install","target":"C:\\x"})"), kPid);
        EF_CHECK(st.send && st.send->type == ipc::MsgType::Error && !st.run && s.closed() && !s.request_taken());
    }
    {
        auto s = ready_session();
        auto st = s.on_frame(ipc::Frame{ipc::MsgType::Cancel, 2, "{}"}, kPid);  // iptal desteklenmez (islem yarida kalmaz)
        EF_CHECK(st.send && st.send->type == ipc::MsgType::Error && s.closed());
    }
    {
        SetupServerSession s(kNonce, kPid);
        auto st = s.on_frame(ipc::Frame{ipc::MsgType::Hello, 1, R"({"nonce":")" + kNonce + R"(","pid":4242,"x":1})"}, kPid);
        EF_CHECK(st.send && st.send->type == ipc::MsgType::Error && s.closed());  // Hello'da fazladan alan
    }
}
