#pragma once
// ADR-033 / spec 4: yukseltilmis kurulum iscisi (SetupWorker) ile kurulum penceresi arasindaki istek/ilerleme/yanit
// kodlayicisi ve sunucu oturumu (saf). Mevcut ipc::Frame tasinir, protokol surumu 1 kalir:
//   Request  : {"kind":"install"|"uninstall"|"repair"}            (yol/deger tasimaz; baska alan reddedilir)
//   Response : {"progress":{"step":..,"rec":..}}                    (her journal kaydindan sonra)
//              {"result":{...TxResult...},"error":".."}             (son yanit)
// Oturum: Hello (nonce + ust PID; ServerSession kurallari), kesin artan seq, oturum basina TEK istek, sonra Bye.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/ipc/protocol.h"
#include "core/setup/install_tx.h"

namespace ef::core::setup {

enum class SetupKind { Install, Uninstall, Repair };
std::string_view to_string(SetupKind k) noexcept;

struct SetupRequest {
    SetupKind kind = SetupKind::Install;
    bool autostart_unused = false;  // tele konmaz: Run degeri yukseltilmemis setup surecinde yazilir (spec 2)
};

struct SetupProgress {
    SetupStep step{};
    StepRecord rec{};
};

struct SetupReply {
    TxResult result;
    std::string error;  // bos: islem motoru calisti (sonuc result'ta); dolu: calistirilamadi (mesgul, kurtarma vb.)
};

// Tek Response yukunun cozumu: ya ilerleme ya son yanit.
struct SetupResponse {
    std::optional<SetupProgress> progress;
    std::optional<SetupReply> reply;
};

std::string encode_request(const SetupRequest& r);
contracts::Result<SetupRequest> decode_request(std::string_view payload);
std::string encode_progress(const SetupProgress& p);
inline constexpr std::size_t kMaxReplyError = 512;  // decode_response sinirina esit
std::string encode_reply(const SetupReply& r);  // error: yazdirilabilir ASCII'ye indirgenir, kMaxReplyError'a kirpilir
contracts::Result<SetupResponse> decode_response(std::string_view payload);

// Isci tarafi oturum. on_frame:
//   Hello  -> HelloAck (nonce/PID yanlissa Error, oturum kapanir)
//   Request (ilk) -> send yok, run = istek (cagiran islemi calistirip progress_frame/reply_frame gonderir)
//   ikinci Request, Cancel, bilinmeyen tur, seq ihlali, baska PID -> Error, oturum kapanir
//   Bye    -> Bye, oturum kapanir
class SetupServerSession {
public:
    struct Step {
        std::optional<ipc::Frame> send;
        std::optional<SetupRequest> run;
    };

    SetupServerSession(std::string expected_nonce, std::uint32_t expected_client_pid)
        : nonce_(std::move(expected_nonce)), pid_(expected_client_pid) {}

    Step on_frame(const ipc::Frame& in, std::uint32_t observed_client_pid);
    ipc::Frame progress_frame(const SetupProgress& p) { return ipc::Frame{ipc::MsgType::Response, ++out_seq_, encode_progress(p)}; }
    ipc::Frame reply_frame(const SetupReply& r) { return ipc::Frame{ipc::MsgType::Response, ++out_seq_, encode_reply(r)}; }

    bool ready() const noexcept { return state_ == State::Ready; }
    bool closed() const noexcept { return state_ == State::Closed; }
    bool request_taken() const noexcept { return request_taken_; }
    const std::string& last_error() const noexcept { return error_; }

private:
    enum class State { AwaitHello, Ready, Closed };
    Step fail(std::string why);

    std::string nonce_;
    std::uint32_t pid_;
    State state_ = State::AwaitHello;
    bool request_taken_ = false;
    std::uint64_t last_seq_ = 0;
    std::uint64_t out_seq_ = 0;
    std::string error_;
};

}  // namespace ef::core::setup
