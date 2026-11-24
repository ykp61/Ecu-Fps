#pragma once
// MEAS-01: Intel PresentMon PresentData (v2.6.0, MIT) uzerinden gercek zamanli ETW kare oturumu.
// - Servis yok, ek EXE yok; ETW oturumu bu surecte acilir ve stop()/yikicida kapatilir.
// - StartTrace ERROR_ACCESS_DENIED donerse AccessDenied bildirilir. Yukseltme istenmez,
//   yonetici olarak yeniden baslatilmaz, kullanici "Performance Log Users" grubuna eklenmez.
// - Oturum adi "EcuFps-" onekiyle baslamak zorundadir; yalniz bu onekli (bize ait) bir
//   yetim oturum, yeniden baslatmadan once durdurulur. Baska bir aracin oturumuna dokunulmaz.
// - Tamamlanan present olaylari poll() ile duzenli cekilmelidir (tuketici halka tamponu sinirli).
// - Spec 2026-11-12 B: GPU is paketi izleme (PresentData mTrackGPU) aciktir; her sunum kare basi GPU mesgul suresini
//   (GPUDuration, QPC tik) tasir. Oyun surecine dokunulmaz: yalniz cekirdek (DxgKrnl) ETW olaylari okunur.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/measure/frames.h"

namespace ef::measurement {

inline constexpr wchar_t kFrameSessionName[] = L"EcuFps-Frames";
inline constexpr wchar_t kFrameSessionPrefix[] = L"EcuFps-";

enum class FrameSessionError { None, AccessDenied, AlreadyRunning, Other };

struct FrameSessionStart {
    FrameSessionError error = FrameSessionError::Other;
    unsigned long win32 = 0;  // StartTrace/OpenTrace/EnableTrace donus kodu (0 = basari)
    std::string detail;
    bool ok() const noexcept { return error == FrameSessionError::None; }
};

struct FrameSessionStats {
    std::uint64_t present_events = 0;  // poll()/stop() ile cekilen tamamlanmis present sayisi
    std::uint32_t events_lost = 0;     // ETW'nin bildirdigi kayip olay (stop sonrasi)
    std::uint32_t buffers_lost = 0;    // ETW'nin bildirdigi kayip tampon (stop sonrasi)
    std::uint32_t overflowed_presents = 0;  // tuketici halkasi tasmasi
};

enum class SessionPresence { Absent, Present, Unknown };

class FrameSession {
public:
    FrameSession();
    ~FrameSession();  // calisiyorsa stop() cagirir (yetim oturum birakmaz)
    FrameSession(const FrameSession&) = delete;
    FrameSession& operator=(const FrameSession&) = delete;

    // Gercek zamanli oturumu acar, saglayicilari etkinlestirir ve tuketici is parcacigini baslatir.
    FrameSessionStart start_realtime(const wchar_t* session_name = kFrameSessionName);
    // Tamamlanan present olaylarini tuketiciden ceker; bu cagrida cekilen sayiyi dondurur.
    std::size_t poll();
    // Oturumu durdurur, tuketici is parcacigini bekler, kalan olaylari ceker. Tekrar cagrilabilir.
    void stop();

    // MEAS-02: poll() ile cekilen tamamlanmis sunumlar (tum surecler); cagiran hedef secimini yapar.
    // Bellek siniri: en fazla max_buffer olay tutulur, tasarsa eski olay dusurulur ve sayilir.
    std::vector<core::measure::RawPresent> take_presents();
    std::uint64_t dropped_presents() const noexcept;
    double ms_per_tick() const noexcept;      // QPC -> ms
    std::uint64_t qpc_origin() const noexcept;  // oturum baslangic QPC

    bool running() const noexcept;
    FrameSessionStats stats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Adlandirilmis ETW oturumunun varligini sorgular (logman query -ets esdegeri, salt okuma).
SessionPresence query_session(const wchar_t* session_name, unsigned long* win32 = nullptr);

}  // namespace ef::measurement
