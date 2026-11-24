#pragma once
// Islem siniri izni: onbellekteki son karara degil, O ANKI surec listesine ve guncel kitapliga dayanir.
// Evet/Hayir, UAC ve broker bekleyislerinden sonra yeniden sorulur; her is parcacigindan cagrilabilir.

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "core/games/action_permit.h"
#include "platform/windows/games/library_scan.h"

namespace ef::platform::games {

class LiveGamePermit final : public core::games::ActionPermit {
public:
    // steam_user_sid: bos = HKCU; yukseltilmis broker hedef kullanicinin kovanini okur.
    explicit LiveGamePermit(std::wstring steam_user_sid = {}) : sid_(std::move(steam_user_sid)) {}
    // Daha once gozlenen oyun EXE adlari (yolu okunamazsa belirsizlik sayilir).
    void set_known_exes(std::vector<std::string> names);
    // EF-X11: elle eklenen oyun EXE'leri (tercih dosyasi); degisince kitaplik onbellegi bosaltilir.
    void set_manual_exes(std::vector<std::string> paths);
    // Uyumluluk B8 (bosluk 20): izleyici is parcaciginin kitaplik taramasi (HKCU, ayni elle eklenen liste). Tarama
    // BASLAMADAN once alinan GetTickCount64 zamaniyla verilir; verify() bu sonucu tazelik penceresinde yeniden kullanir ve
    // yavas surucu / AV altinda kitaplik G/C'sini UI is parcacigina tasimaz. Elle eklenen liste farkliysa yok sayilir.
    void offer_library(const LibraryScanResult& library, std::int64_t scanned_at_tick_ms, const std::vector<std::string>& manual_exes);
    core::games::GateDecision verify(core::games::GuardedAction action) override;

private:
    core::games::GateDecision verify_locked(core::games::GuardedAction action);
    std::mutex mu_;
    std::wstring sid_;
    std::vector<std::string> known_exes_;
    std::vector<std::string> manual_exes_;
    LibraryScanResult library_;
    std::int64_t library_at_ms_ = -1;
    core::games::PermitLibrarySource library_source_ = core::games::PermitLibrarySource::None;
};

}  // namespace ef::platform::games
