#pragma once
// ADR-033 / spec 5: yardimci hizmet pipe oturumu (saf durum makinesi; ipc::Frame uzerinde).
//  AwaitHello -> Hello yuku TAM OLARAK {"proto":1,"pid":n}, n == gozlenen istemci PID'i -> HelloAck -> Ready.
//  Ready: Request (ipc::ServerSession ile ayni cozum: request_id, RequestAction, SettingOperation) -> Handler;
//         seq kesin artan (her cerceve bir oncekinin +1'i), baglanti basina en fazla 64 istek; Bye -> Closed.
//  Baska her sey -> Error + Closed. Gorev 5 (guncelleme cerceveleri) icin `extra` uzanti noktasi; varsayilan reddeder;
//         extra cerceveleri baglanti basina en fazla kMaxServiceExtraFrames.
// Istemci dogrulamasi (check_client) Hello'dan ONCE platformda yapilir; bu oturum yalniz cerceve kurallarini uygular.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <string_view>

#include "core/catalog/envelope.h"
#include "core/ipc/protocol.h"

namespace ef::core::service {

inline constexpr std::int64_t kServiceProto = 1;
inline constexpr int kMaxServiceRequests = 64;
// Gorev 5: `extra` cerceveleri (guncelleme aktarimi: 64 MiB / 32 KiB parca + begin/commit) ayri sinira tabidir.
inline constexpr int kMaxServiceExtraFrames = 4096;

// Denetim 3.2: hizmet baglantilari sirayla islenir; asili ya da oyalayan bir istemci digerlerini bekletmesin.
//  - Hello gelmeden (oturum Ready degil) en fazla kHelloMs.
//  - Ready iken iki cerceve arasi en fazla kIdleMs (uzun islem on_frame icinde senkron; bekleme sayilmaz).
//  - Baglanti toplam omru en fazla kLifetimeMs (DISM/SFC onarimi dahil en uzun isin ustunde).
struct ConnectionTimer {
    static constexpr std::uint64_t kHelloMs = 5000;
    static constexpr std::uint64_t kIdleMs = 60000;
    static constexpr std::uint64_t kLifetimeMs = 2ull * 60 * 60 * 1000;  // DISM/SFC onarimi uzun surebilir
    std::uint64_t start = 0, last = 0;
    explicit ConnectionTimer(std::uint64_t now) : start(now), last(now) {}
    void frame(std::uint64_t now) { last = now; }
    // Baglanti kesilmeli mi (ready: oturum Hello'yu kabul etti).
    bool expired(std::uint64_t now, bool ready) const {
        if (now - start > kLifetimeMs) return true;
        return now - last > (ready ? kIdleMs : kHelloMs);
    }
};

class ServiceSession {
public:
    using Handler = ipc::ServerSession::Handler;
    // nullopt -> reddedilir (Error + Closed). Donen cercevenin seq'i oturumca atanir.
    using Extra = std::function<std::optional<ipc::Frame>(const ipc::Frame&)>;

    ServiceSession(std::uint32_t observed_client_pid, Handler handler, Extra extra = {})
        : pid_(observed_client_pid), handler_(std::move(handler)), extra_(std::move(extra)) {}

    ipc::Frame on_frame(const ipc::Frame& in);

    bool ready() const noexcept { return state_ == State::Ready; }
    bool closed() const noexcept { return state_ == State::Closed; }
    const std::string& last_error() const noexcept { return error_; }
    int requests() const noexcept { return requests_; }

private:
    enum class State { AwaitHello, Ready, Closed };
    ipc::Frame fail(std::string why);
    ipc::Frame reply(ipc::MsgType t, std::string payload);

    std::uint32_t pid_;
    Handler handler_;
    Extra extra_;
    State state_ = State::AwaitHello;
    std::uint64_t last_seq_ = 0;
    std::uint64_t out_seq_ = 0;
    int requests_ = 0;
    int extra_frames_ = 0;
    std::string error_;
};

ipc::Frame make_service_hello(std::uint32_t pid, std::uint64_t seq);

// ADR-037 acil fren (savunma derinligi): UpdateBegin yukundeki zarf imzasi dogrulanir ve imzali manifestte "halt": true
// (ya da bool olmayan halt) varsa true. Zarf acilamazsa false (koordinator zaten reddeder).
bool update_begin_halted(std::string_view begin_payload, const catalog::KeyLookup& keys, const catalog::RawVerifier& verify);
// Uzanti noktasini sarar: halt'li UpdateBegin uzantiya (koordinatore) ulasmadan reddedilir (nullopt -> Error + Closed).
ServiceSession::Extra halt_guarded(ServiceSession::Extra inner, catalog::KeyLookup keys, catalog::RawVerifier verify);

}  // namespace ef::core::service
