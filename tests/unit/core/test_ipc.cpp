#include <string>

#include "core/ipc/protocol.h"
#include "ef_test.h"

using namespace ef::core::ipc;
using ef::contracts::ErrorCode;

namespace {
const std::string kNonce = "0123456789abcdef0123456789abcdef";
const ef::contracts::SettingOperation kOp{ef::contracts::OperationId::GameModeSet, ef::contracts::OperationScope::CurrentUser,
                                           "gamemode", "on", false, false};
ServerSession session(int* calls = nullptr) {
    return ServerSession(kNonce, 4242, [calls](const std::string&, ef::core::ipc::RequestAction, const ef::contracts::SettingOperation&) {
        if (calls) ++*calls;
        return std::string("{}");
    });
}
}  // namespace

EF_TEST(ipc_frame_round_trip_and_limits) {
    Frame f{MsgType::Request, 7, R"({"a":1})"};
    const auto bytes = encode(f);
    EF_CHECK(bytes.size() == kHeaderSize + f.payload.size());
    auto d = decode(bytes);
    EF_CHECK(d.ok() && d.value().seq == 7 && d.value().payload == f.payload && d.value().type == MsgType::Request);
    EF_CHECK(!decode(bytes.substr(0, 10)).ok());
    EF_CHECK(!decode(bytes + "x").ok());
    std::string bad = bytes;
    bad[0] = 'Z';
    EF_CHECK(!decode(bad).ok());
    std::string newer = bytes;
    newer[4] = 9;
    EF_CHECK(decode(newer).code() == ErrorCode::SchemaTooNew);
    std::string badtype = bytes;
    badtype[6] = 99;
    EF_CHECK(decode(badtype).code() == ErrorCode::InvalidEnum);
    std::string big = bytes;
    big[10] = 0x10;  // 1 MiB
    MsgType t;
    std::uint64_t s;
    EF_CHECK(parse_header(big, t, s).code() == ErrorCode::ParseLimit);
}

EF_TEST(ipc_session_happy_path) {
    int calls = 0;
    auto ss = session(&calls);
    EF_CHECK(ss.on_frame(make_hello(kNonce, 4242, 1), 4242).type == MsgType::HelloAck);
    EF_CHECK(ss.ready());
    EF_CHECK(ss.on_frame(make_request("r1", kOp, 2), 4242).type == MsgType::Response);
    EF_CHECK(ss.on_frame(make_request("r2", kOp, 3), 4242).type == MsgType::Response);
    EF_CHECK(calls == 2);
    EF_CHECK(ss.on_frame(Frame{MsgType::Bye, 4, "{}"}, 4242).type == MsgType::Bye);
    EF_CHECK(ss.closed() && ss.last_error().empty());
}

EF_TEST(ipc_session_rejects_identity_and_replay) {
    {
        auto ss = session();
        EF_CHECK(ss.on_frame(make_hello(kNonce, 4242, 1), 9999).type == MsgType::Error);  // baska surec
        EF_CHECK(ss.closed());
    }
    {
        auto ss = session();
        EF_CHECK(ss.on_frame(make_hello(std::string(32, 'a'), 4242, 1), 4242).type == MsgType::Error);  // yanlis nonce
    }
    {
        auto ss = session();
        EF_CHECK(ss.on_frame(make_request("r1", kOp, 1), 4242).type == MsgType::Error);  // hello yok
    }
    {
        int calls = 0;
        auto ss = session(&calls);
        EF_CHECK(ss.on_frame(make_hello(kNonce, 4242, 1), 4242).type == MsgType::HelloAck);
        EF_CHECK(ss.on_frame(make_request("r1", kOp, 2), 4242).type == MsgType::Response);
        EF_CHECK(ss.on_frame(make_request("r1", kOp, 2), 4242).type == MsgType::Error);  // replay
        EF_CHECK(ss.on_frame(make_request("r2", kOp, 3), 4242).type == MsgType::Error);  // kapandi
        EF_CHECK(calls == 1);
    }
    {
        auto ss = session();
        EF_CHECK(ss.on_frame(make_hello(kNonce, 4242, 1), 4242).type == MsgType::HelloAck);
        EF_CHECK(ss.on_frame(make_hello(kNonce, 4242, 2), 4242).type == MsgType::Error);  // ikinci hello
    }
}

EF_TEST(ipc_session_rejects_unlisted_operations_and_fields) {
    auto ss = session();
    EF_CHECK(ss.on_frame(make_hello(kNonce, 4242, 1), 4242).type == MsgType::HelloAck);
    Frame shell{MsgType::Request, 2, R"({"request_id":"x","op":{"op":"run_command","scope":"machine","target":"a","value":"cmd"}})"};
    EF_CHECK(ss.on_frame(shell, 4242).type == MsgType::Error);
    auto ss2 = session();
    EF_CHECK(ss2.on_frame(make_hello(kNonce, 4242, 1), 4242).type == MsgType::HelloAck);
    Frame extra{MsgType::Request, 2, R"({"request_id":"x","op":{"op":"game_mode_set","scope":"current_user","target":"g","value":"1"},"path":"C:\\x"})"};
    EF_CHECK(ss2.on_frame(extra, 4242).type == MsgType::Error);
    auto ss3 = session();
    EF_CHECK(ss3.on_frame(make_hello(kNonce, 4242, 1), 4242).type == MsgType::HelloAck);
    Frame path{MsgType::Request, 2, R"({"request_id":"x","op":{"op":"game_mode_set","scope":"current_user","target":"C:\\Windows","value":"1"}})"};
    EF_CHECK(ss3.on_frame(path, 4242).type == MsgType::Error);
}

// ADR-033 / spec 7: guncelleme cerceveleri (8..12) cozulur ama UAC broker oturumu onlari reddeder ve kapanir.
EF_TEST(ipc_update_frames_parse_but_broker_rejects) {
    for (auto t : {MsgType::UpdateBegin, MsgType::UpdateChunk, MsgType::UpdateCommit, MsgType::UpdateHealthy, MsgType::UpdateStatus}) {
        const auto bytes = encode(Frame{t, 2, "{}"});
        auto d = decode(bytes);
        EF_CHECK(d.ok() && d.value().type == t);
        int calls = 0;
        auto ss = session(&calls);
        EF_CHECK(ss.on_frame(make_hello(kNonce, 4242, 1), 4242).type == MsgType::HelloAck);
        EF_CHECK(ss.on_frame(Frame{t, 2, "{}"}, 4242).type == MsgType::Error);
        EF_CHECK(ss.closed() && calls == 0);
    }
    std::string badtype = encode(Frame{MsgType::Bye, 1, "{}"});
    badtype[6] = 13;
    EF_CHECK(decode(badtype).code() == ErrorCode::InvalidEnum);
}
