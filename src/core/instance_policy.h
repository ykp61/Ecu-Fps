#pragma once
// Uyumluluk B2 (bosluk 4, 5): cikis ve tek ornek kurallari. Win32 icermez; platform cagrilari fonksiyonla verilir.

#include <cstdint>
#include <functional>

namespace ef::core {

// --- Ikinci baslatma (bosluk 4) ---
// Tek ornek kilidi alinamadiginda: calisan ornek yanit veriyorsa one getirilir; vermiyorsa (cikiyor: pencere yok veya
// mesaj dongusu bitti) eski surecin kilidi birakmasi sinirli sure beklenir, birakirsa normal baslanir. Sure dolarsa
// ikinci ornek acilmaz (es zamanli iki journal yazicisi olmaz).
// Blocked: mesaj isletim sistemince reddedildi (UIPI: calisan ornek yonetici, ikinci baslatma orta butunlukte). Beklemek
// sonucu degistirmez; ikinci ornek hemen vazgecer (iki journal yazicisi olmaz).
enum class ActivateTry { Activated, NoWindow, NotResponding, Blocked };

// Calisan ornegin ana penceresi aktivasyon mesajina bu degerle yanit verir (App::host). Baska deger (ornek: DefWindowProc
// 0'i, cikmakta olan surecin yok olmakta olan penceresi) aktivasyon sayilmaz.
inline constexpr std::uintptr_t kActivateReply = 1;
inline ActivateTry classify_activate_reply(bool sent, std::uintptr_t reply, bool access_denied) {
    if (sent) return reply == kActivateReply ? ActivateTry::Activated : ActivateTry::NotResponding;
    return access_denied ? ActivateTry::Blocked : ActivateTry::NotResponding;
}
enum class RelaunchOutcome { Activated, Acquired, GaveUp };

struct RelaunchOps {
    std::function<ActivateTry()> activate;          // tek deneme: pencereyi bul + SendMessageTimeout
    std::function<bool(std::uint32_t)> wait_lock;   // kilidi en cok ms bekle; alindiysa true
    std::function<std::uint64_t()> now_ms;          // monoton saat
};

inline constexpr std::uint32_t kRelaunchBudgetMs = 15000;
inline constexpr std::uint32_t kRelaunchSliceMs = 500;

inline RelaunchOutcome resolve_second_launch(const RelaunchOps& ops, std::uint32_t budget_ms = kRelaunchBudgetMs,
                                             std::uint32_t slice_ms = kRelaunchSliceMs) {
    const std::uint64_t start = ops.now_ms();
    for (;;) {
        const ActivateTry a = ops.activate();
        if (a == ActivateTry::Activated) return RelaunchOutcome::Activated;
        if (a == ActivateTry::Blocked) return RelaunchOutcome::GaveUp;
        if (ops.wait_lock(slice_ms)) return RelaunchOutcome::Acquired;
        if (ops.now_ms() - start >= budget_ms) return RelaunchOutcome::GaveUp;
    }
}

// --- Cikis (bosluk 4, 5) ---
// Tek ornek kilidi, hicbir yazma isi surmuyorsa pencere/tepsi kalkar kalkmaz birakilir; suren yazma isi varsa o is
// bitince (yeni ornegin acilis kurtarmasi yarim yazmayla yarismasin).
inline bool release_instance_early(bool any_write_worker_running) { return !any_write_worker_running; }

// Zorunlu cikista yonetici isi: sinirli bekleme dolunca is parcacigi hala broker/hizmet cagrisindaysa beklenmez (journal
// ve makine kilidi broker'da; sonraki acilista uzlastirilir). Yerel yazma suruyorsa (broker cagrisi yok) beklemeye devam.
enum class AdminExitWait { Joined, LeaveToBroker, KeepWaiting };
inline AdminExitWait admin_exit_decision(bool finished, int broker_calls) {
    if (finished) return AdminExitWait::Joined;
    return broker_calls > 0 ? AdminExitWait::LeaveToBroker : AdminExitWait::KeepWaiting;
}
inline constexpr std::uint32_t kAdminExitWaitMs = 10000;

}  // namespace ef::core
