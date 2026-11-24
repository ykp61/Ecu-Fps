#pragma once
// EF-X01: Windows olay gunlugu okuyucusu (yalniz okuma; gunluk veya dump silmez/degistirmez).
#include "core/diagnostics/incident_engine.h"

namespace ef::platform::diagnostics {

core::diagnostics::EventReadResult read_system_incidents(int days = core::diagnostics::kDefaultDays);

// Son olumcul WHEA kaydinin numarasi: test sirasinda "yeni" hata yuksek su isaretiyle bulunur, sayimla degil.
struct FatalWheaMark {
    bool ok = false;
    std::uint64_t record = 0;  // 0: kayit yok
};
FatalWheaMark newest_fatal_whea();

// Spec 2026-11-12 C1: "bellenim islemci hizini sinirliyor" kayitlari (Microsoft-Windows-Kernel-Processor-Power,
// olay 37), Windows calisma suresi (son acilis) icinde. Olay islemci basina yazilir: sayi "kac kez" degil "kac
// kayit"tir. Yalniz sayilir (icerik okunmaz); kMaxSpeedLimitEvents'te kesilir (capped). Bu kayit NEDEN tasimaz.
inline constexpr std::uint32_t kMaxSpeedLimitEvents = 512;
struct ProcessorSpeedLimitEvents {
    bool ok = false;  // gunluk okunabildi
    std::uint32_t count = 0;
    bool capped = false;
};
ProcessorSpeedLimitEvents count_processor_speed_limit_events_since_boot();

}  // namespace ef::platform::diagnostics
