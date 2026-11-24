// SEC-02 Windows testi: gercek iki surec arasinda pipe IPC.
//   server <nonce> <expected_client_pid> <dir>   -> dir\server.pid yazar, tek istemci kabul eder
//   client <nonce> <dir> <mode>                  -> mode: good|replay|badnonce|oversize|malformed|badop
//   squat  <nonce> <dir>                          -> pipe adini once alir, dir\squat.ready yazar, 15 sn bekler
// Cikis: 0 beklenen sonuc; diger kodlar hata.

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/ipc/protocol.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/pipe.h"
#include "platform/windows/win_env.h"

using namespace ef;
namespace fs = std::filesystem;

namespace {
constexpr std::uint32_t kTimeout = 5000;

std::string sid_of_self() {
    const auto u = platform::inventory::read_user();
    return u.process_user_sid.value.value_or("");
}

int server(const std::string& nonce, std::uint32_t expected, const fs::path& dir) {
    auto srv = platform::PipeServer::create(platform::pipe_name_for(nonce), sid_of_self());
    if (!srv.ok()) {
        std::printf("CREATE_FAILED %s\n", srv.error().detail.c_str());
        return srv.code() == contracts::ErrorCode::Denied ? 21 : 22;
    }
    std::ofstream(dir / "server.pid") << GetCurrentProcessId();
    auto& p = *srv.value();
    if (!p.accept(15000).ok()) return 23;
    auto id = p.client_identity();
    if (!id.ok()) return 24;
    std::uint32_t mysession = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), reinterpret_cast<DWORD*>(&mysession));
    if (id.value().session != mysession) return 25;
    core::ipc::ServerSession sess(nonce, expected, [](const std::string& rid, core::ipc::RequestAction, const contracts::SettingOperation&) {
        return std::string(R"({"request_id":")") + rid + R"(","status":"accepted"})";
    });
    for (int i = 0; i < 16 && !sess.closed(); ++i) {
        auto f = p.read_frame(kTimeout);
        if (!f.ok()) {
            std::printf("READ_REJECT %s\n", f.error().detail.c_str());
            core::ipc::Frame e{core::ipc::MsgType::Error, 99, R"({"error":"bad frame"})"};
            (void)p.write_frame(e, kTimeout);
            return 30;  // kontrollu red
        }
        auto out = sess.on_frame(f.value(), id.value().pid);
        (void)p.write_frame(out, kTimeout);
    }
    if (!sess.last_error().empty()) {
        std::printf("SESSION_REJECT %s\n", sess.last_error().c_str());
        return 30;
    }
    std::printf("SESSION_OK\n");
    return 0;
}

std::uint32_t wait_pid(const fs::path& f) {
    for (int i = 0; i < 300; ++i) {
        std::ifstream in(f);
        std::uint32_t pid = 0;
        if (in >> pid && pid) return pid;
        Sleep(20);
    }
    return 0;
}

int client(const std::string& nonce, const fs::path& dir, const std::string& mode) {
    const auto spid = wait_pid(dir / "server.pid");
    if (!spid) return 40;
    auto c = platform::PipeClient::connect(platform::pipe_name_for(nonce), spid, kTimeout);
    if (!c.ok()) return 41;
    auto& p = *c.value();
    const contracts::SettingOperation op{contracts::OperationId::PowerModeSet, contracts::OperationScope::Machine,
                                         "overlay:{ded574b5-45a0-4f42-8737-46345c09c238}", "best_performance", false, false};
    auto expect = [&](core::ipc::MsgType t) {
        auto r = p.read_frame(kTimeout);
        return r.ok() && r.value().type == t;
    };
    if (mode == "malformed") {
        (void)p.write_raw(std::string(20, 'Z'), kTimeout);
        return expect(core::ipc::MsgType::Error) ? 0 : 50;
    }
    if (mode == "oversize") {
        core::ipc::Frame big{core::ipc::MsgType::Hello, 1, ""};
        std::string bytes = core::ipc::encode(big);
        bytes[8] = '\x00'; bytes[9] = '\x00'; bytes[10] = '\x10'; bytes[11] = '\x00';  // 1 MiB iddia
        (void)p.write_raw(bytes, kTimeout);
        return expect(core::ipc::MsgType::Error) ? 0 : 51;
    }
    const std::string hello_nonce = mode == "badnonce" ? std::string(32, 'f') : nonce;
    if (!p.write_frame(core::ipc::make_hello(hello_nonce, GetCurrentProcessId(), 1), kTimeout).ok()) return 42;
    if (mode == "badnonce") return expect(core::ipc::MsgType::Error) ? 0 : 52;
    if (!expect(core::ipc::MsgType::HelloAck)) return 43;
    if (mode == "badop") {
        core::ipc::Frame f{core::ipc::MsgType::Request, 2,
                           R"({"request_id":"r1","op":{"op":"run_command","scope":"machine","target":"x","value":"cmd.exe"}})"};
        (void)p.write_frame(f, kTimeout);
        return expect(core::ipc::MsgType::Error) ? 0 : 53;
    }
    if (!p.write_frame(core::ipc::make_request("r1", op, 2), kTimeout).ok()) return 44;
    if (!expect(core::ipc::MsgType::Response)) return 45;
    if (mode == "replay") {
        (void)p.write_frame(core::ipc::make_request("r1", op, 2), kTimeout);  // ayni seq
        return expect(core::ipc::MsgType::Error) ? 0 : 54;
    }
    (void)p.write_frame(core::ipc::Frame{core::ipc::MsgType::Bye, 3, "{}"}, kTimeout);
    return expect(core::ipc::MsgType::Bye) ? 0 : 46;
}

int squat(const std::string& nonce, const fs::path& dir) {
    auto s = platform::PipeServer::create(platform::pipe_name_for(nonce), sid_of_self());
    if (!s.ok()) return 60;
    std::ofstream(dir / "squat.ready") << 1;
    Sleep(15000);
    return 0;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) return 2;
    const std::wstring mode = argv[1];
    const std::string nonce = platform::to_utf8(argv[2]);
    if (mode == L"server" && argc >= 5) return server(nonce, static_cast<std::uint32_t>(std::wcstoul(argv[3], nullptr, 10)), argv[4]);
    if (mode == L"client" && argc >= 5) return client(nonce, argv[3], platform::to_utf8(argv[4]));
    if (mode == L"squat") return squat(nonce, argv[3]);
    return 2;
}
