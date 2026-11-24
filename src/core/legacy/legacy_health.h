#pragma once
// T3b (ADR-043 ek): eski UI "--updated-from=<surum>" ile yeni EXE'yi ESKI yolda baslattiginda eski hizmete giden saglik
// sinyali (saf sira; tasima LegacyHealthPort arkasinda). Tel bicimi 0.9.5 ile birebir (059b3ee3 core/ipc/protocol.cpp,
// core/service/service_session.cpp, core/update/staged_update.cpp; bu dosyalar marka degisikliginde degismedi):
//   cerceve = magic "ITXP"(LE u32) | surum 1 (LE u16) | tur (LE u16) | uzunluk (LE u32) | seq (LE u64) | JSON
//   1) Hello      seq 1 {"proto":1,"pid":<kendi PID>}      -> HelloAck
//   2) UpdateHealthy seq 2 {"version":"<calisan surum>"}   -> UpdateStatus {"state":"committed"}
// Eski hizmet yanittan sonra kendini durdurur (cikis 4); Bye gonderilmez (0.9.5 istemcisi ile ayni).
// Baglanti oncesi: eski hizmet SCM'de var, RUNNING, PID != 0, ImagePath == "\"<eski exe>\" --service"; pipe sunucu
// PID'i == SCM PID'i (ad gaspi: veri gonderilmez). Bu surec yeni bir IPC SUNMAZ; yalniz istemcidir.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/ipc/protocol.h"
#include "core/setup/layout.h"

namespace ef::core::legacy {

struct LegacyServiceView {
    bool exists = false, running = false;
    std::uint32_t pid = 0;
    std::wstring image_path;
};

class LegacyHealthPort {
public:
    virtual ~LegacyHealthPort() = default;
    virtual LegacyServiceView query_old_service() = 0;                  // salt okuma SCM
    virtual bool connect(std::uint32_t expected_server_pid) = 0;        // eski pipe; sunucu PID'i dogrulanir
    virtual bool write(const ipc::Frame& f) = 0;
    virtual std::optional<ipc::Frame> read(std::uint32_t timeout_ms) = 0;
};

enum class LegacyHealthOutcome {
    NoOldService,     // eski hizmet yok (yeni kurulum / eski kaldirilmis): hicbir sey yapilmaz
    NotRunning,       // eski hizmet durmus: sinyal bekleyen yok
    ImageMismatch,    // ImagePath beklenen eski EXE degil: veri gonderilmez
    PipeUnavailable,  // pipe yok / sunucu PID'i uyusmadi
    HelloRejected,
    Rejected,         // UpdateStatus committed degil
    Confirmed,
};
std::string_view to_string(LegacyHealthOutcome o) noexcept;

inline constexpr std::uint32_t kLegacyHealthReplyMs = 30000;  // 0.9.5 kHelloReplyMs / kReplyTimeoutMs ile ayni

std::vector<ipc::Frame> legacy_health_frames(std::uint32_t self_pid, std::string_view version);
LegacyHealthOutcome send_legacy_health(LegacyHealthPort& port, const setup::InstallLayout& legacy, std::uint32_t self_pid,
                                       std::string_view version);

}  // namespace ef::core::legacy
