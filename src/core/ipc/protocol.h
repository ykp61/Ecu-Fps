#pragma once
// SEC-02: broker IPC cerceve protokolu ve sunucu oturum durum makinesi (platformdan bagimsiz).
// Cerceve: magic(4) | version(2) | type(2) | length(4) | seq(8) | payload(length, JSON)

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "contracts/models.h"
#include "contracts/result.h"

namespace ef::core::ipc {

inline constexpr std::uint32_t kMagic = 0x50585449;  // "ITXP"
inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::size_t kHeaderSize = 20;
inline constexpr std::uint32_t kMaxPayload = 64 * 1024;
// ADR-037: broker oturumu basina en fazla cerceve (Hello dahil); platform dongusuyle ayni sinir, cekirdekte de zorlanir.
inline constexpr int kMaxSessionFrames = 64;

// 8..12: ADR-033 / spec 7 zorunlu guncelleme degisimi (yalniz yardimci hizmet oturumu; broker ServerSession reddeder).
// UpdateBegin {"envelope":...}, UpdateChunk {"offset":n,"data":base64}, UpdateCommit {}, UpdateHealthy {"version":...};
// yanit UpdateStatus {"state":"accepted|swapped|committed|error","error":...}. Kodekler core/update/staged_update.h.
enum class MsgType : std::uint16_t {
    Hello = 1, HelloAck = 2, Request = 3, Response = 4, Cancel = 5, Error = 6, Bye = 7,
    UpdateBegin = 8, UpdateChunk = 9, UpdateCommit = 10, UpdateHealthy = 11, UpdateStatus = 12
};

// OPT-02/OPT-10: istek eylemi. Apply = Performans grubu (makine journal'i), Restore = Kapali (ayni hedefin
// etkin islemini geri al), Fix = Oyun Hazirligi duzeltmesi (ayri makine "fixes" journal'i; Kapali geri almaz).
// UndoFix = Oyun Hazirligi "Geri al" (fixes journal'indaki etkin duzeltmeyi onceki degere dondur).
// Recover = acilista onaylanan kurtarma: oturum basi uzlastirmadan sonra hedefin durumunu bildirir (yazma yok).
// Alan yoksa Apply (SEC-02 surum 1 ile geriye uyumlu).
// KeepCurrent = Oyun Hazirligi "Mevcut degeri koru": fixes journal'inda dis degisiklik bekleyen kaydi ayar YAZMADAN
// kapatir (adim KeptExternal, islem Restored).
enum class RequestAction { Apply, Restore, Fix, UndoFix, Recover, KeepCurrent, RestorePoint, Maintenance, DriverInstall, VendorInstall };
std::string_view to_string(RequestAction a) noexcept;

struct Frame {
    MsgType type = MsgType::Error;
    std::uint64_t seq = 0;
    std::string payload;
};

std::string encode(const Frame& f);
// Basliktan yuk uzunlugu (akis okumasi icin). Bozuk magic/surum/tur/asiri boyut hata.
contracts::Result<std::uint32_t> parse_header(std::string_view header, MsgType& type, std::uint64_t& seq);
// Tam bir cerceve (baslik + yuk) cozer; fazladan bayt hata.
contracts::Result<Frame> decode(std::string_view bytes);

// Request yukunun ortak cozumu (ServerSession ve yardimci hizmet oturumu). Hata ayrintisi oturum hata metnidir.
struct DecodedRequest {
    std::string request_id;
    RequestAction action = RequestAction::Apply;
    contracts::SettingOperation op;
};
contracts::Result<DecodedRequest> decode_request(std::string_view payload);

// Sunucu oturumu: once Hello (dogru nonce + beklenen istemci PID'i), sonra kesin artan seq ile istekler.
// Herhangi bir ihlalde oturum kapanir; sonraki tum cerceveler reddedilir.
class ServerSession {
public:
    using Handler = std::function<std::string(const std::string& request_id, RequestAction action, const contracts::SettingOperation& op)>;

    ServerSession(std::string expected_nonce, std::uint32_t expected_client_pid, Handler handler)
        : nonce_(std::move(expected_nonce)), pid_(expected_client_pid), handler_(std::move(handler)) {}

    // Gelen cerceveye yanit uretir. Hata durumunda Error cercevesi doner ve oturum kapanir.
    Frame on_frame(const Frame& in, std::uint32_t observed_client_pid);

    bool ready() const noexcept { return state_ == State::Ready; }
    bool closed() const noexcept { return state_ == State::Closed; }
    const std::string& last_error() const noexcept { return error_; }

private:
    enum class State { AwaitHello, Ready, Closed };
    Frame fail(std::string why);

    std::string nonce_;
    std::uint32_t pid_;
    Handler handler_;
    State state_ = State::AwaitHello;
    std::uint64_t last_seq_ = 0;
    std::uint64_t out_seq_ = 0;
    int frames_ = 0;  // ADR-037: kMaxSessionFrames
    std::string error_;
};

// Istemci yardimcilari
Frame make_hello(std::string_view nonce, std::uint32_t pid, std::uint64_t seq);
Frame make_request(std::string_view request_id, const contracts::SettingOperation& op, std::uint64_t seq,
                   RequestAction action = RequestAction::Apply);

}  // namespace ef::core::ipc
