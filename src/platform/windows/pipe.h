#pragma once
// SEC-02 platform: ACL'li adlandirilmis pipe sunucu/istemci, zaman asimli cerceve G/C, karsi uc kimligi.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/ipc/protocol.h"

namespace ef::platform {

std::wstring pipe_name_for(const std::string& nonce);  // \\.\pipe\EcuFps.<nonce>

struct PeerIdentity {
    std::uint32_t pid = 0;
    std::uint32_t session = 0;
    std::uint64_t create_time = 0;  // FILETIME (100 ns); PID yeniden kullanimi ayrimi
    std::wstring image;             // tam EXE yolu
};

// Istemci kimligi baslangicta dogrulanan ust surecle birebir ayni mi: PID + olusturma zamani +
// oturum + EXE yolu (harf duyarsiz). Yalniz PID karsilastirmasi PID yeniden kullanimiyla atlatilabilir.
// Saf fonksiyon; Win32 cagrisi yapmaz, birim testlenir. Sifir olusturma zamani/ bos yol eslesmez.
bool peer_matches(const PeerIdentity& peer, std::uint32_t expected_pid, std::uint64_t expected_create_time,
                  std::uint32_t expected_session, std::wstring_view expected_image);

class PipeEnd {
public:
    explicit PipeEnd(void* h) : h_(h) {}
    ~PipeEnd();
    PipeEnd(const PipeEnd&) = delete;
    PipeEnd& operator=(const PipeEnd&) = delete;

    contracts::Status write_frame(const core::ipc::Frame& f, std::uint32_t timeout_ms);
    contracts::Result<core::ipc::Frame> read_frame(std::uint32_t timeout_ms);
    // Bloklamadan: tam cerceve hazirsa okur, hazir degilse nullopt (hata yalniz bozuk akis/boru kopmasi).
    // Baslik PeekNamedPipe ile kopyalanir, boylece yarim cerceve tuketilmez.
    contracts::Result<std::optional<core::ipc::Frame>> poll_frame();
    // Ham bayt (testlerde bozuk girdi gondermek icin)
    contracts::Status write_raw(const std::string& bytes, std::uint32_t timeout_ms);
    void* handle() const noexcept { return h_; }

protected:
    contracts::Status io(bool write, char* buf, std::uint32_t len, std::uint32_t timeout_ms);
    void* h_;
};

class PipeServer : public PipeEnd {
public:
    // allowed_user_sid: istemci kullanicisi (oturum kullanicisi). Ayrica SYSTEM ve Yoneticiler.
    // FIRST_PIPE_INSTANCE: ad onceden alinmissa basarisiz (ad gaspi engeli). Uzak istemci reddi.
    static contracts::Result<std::unique_ptr<PipeServer>> create(const std::wstring& name, const std::string& allowed_user_sid);
    // ADR-033 yardimci hizmet pipe'i: DACL SYSTEM + Yoneticiler tam, yalniz ETKILESIMLI kullanicilar (IU) okuma/yazma;
    // orta butunluk NW etiketi. FILE_FLAG_FIRST_PIPE_INSTANCE (ad onceden alinmissa basarisiz: gasp engeli),
    // PIPE_REJECT_REMOTE_CLIENTS, tek ornek. Istemci kimligi her baglantida ayrica dogrulanir (service/client_facts).
    static contracts::Result<std::unique_ptr<PipeServer>> create_service(const std::wstring& name);
    contracts::Status accept(std::uint32_t timeout_ms);
    // Baglantiyi keser (ayni ornek sonraki istemciyi kabul edebilir). Okunmamis veri atilir.
    void disconnect();
    contracts::Result<PeerIdentity> client_identity() const;

private:
    using PipeEnd::PipeEnd;
};

class PipeClient : public PipeEnd {
public:
    // Baglanir ve sunucunun beklenen PID oldugunu dogrular (gasp edilmis pipe'a veri gonderilmez).
    static contracts::Result<std::unique_ptr<PipeClient>> connect(const std::wstring& name, std::uint32_t expected_server_pid,
                                                                 std::uint32_t timeout_ms);
    // Yardimci hizmet pipe'ina baglanir; GetNamedPipeServerProcessId == expected_server_pid (SCM'nin bildirdigi hizmet
    // PID'i) degilse veri gondermeden kapatir (Denied). expected 0 ise reddedilir.
    static contracts::Result<std::unique_ptr<PipeClient>> connect_service(const std::wstring& name, std::uint32_t expected_server_pid,
                                                                         std::uint32_t timeout_ms);
    std::uint32_t server_pid() const;  // 0 = okunamadi

private:
    using PipeEnd::PipeEnd;
};

}  // namespace ef::platform
