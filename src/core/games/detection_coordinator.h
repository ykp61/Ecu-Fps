#pragma once
// Oyun tespit yasam dongusu (saf; birim testlenir). App'in eski install_watch_state mantigi buradadir:
//  - Tazelik GOZLEM zamanindan hesaplanir; UI kuyrugunda gec islenen sonuc yeniymis gibi kabul edilmez.
//  - Eski nesil / sira disi sonuc atilir.
//  - Son oyun kapandiginda veya ilk guvenilir taramada borclanan isler (otomatik geri alma, bekleyen cikis,
//    kurtarma) tazelik/kapi gecici kapaliysa KAYBOLMAZ; ilk taze guvenilir "oyun yok" sonucunda BIR kez calisir.
//  - Engelin kullaniciya yazilacak nedeni tek yerden uretilir (BlockCause).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/games/library_scan.h"
#include "core/games/protection_gate.h"
#include "core/games/watch_state.h"

namespace ef::core::games {

enum class BlockCause { None, GameRunning, FirstScan, LibraryError, ProcessIdentity, ProcessListFailed, Stale };
std::string_view reason_key(BlockCause c) noexcept;  // gate.*; None -> bos
std::string_view title_key(BlockCause c) noexcept;

class DetectionCoordinator {
public:
    using Clock = std::function<std::int64_t()>;  // monoton ms
    struct Hooks {
        std::function<void()> auto_restore;
        std::function<void()> pending_exit;
        std::function<void()> pending_recovery;
    };

    DetectionCoordinator(Clock clock, Hooks hooks) : clock_(std::move(clock)), hooks_(std::move(hooks)) {}

    // Yeni izleyici oturumu: durum Unknown, onceki nesil sonuclari yok sayilir.
    void begin_session(std::uint64_t generation);
    void set_library(const LibraryScanResult& r);
    // Sonuc UI is parcaciginda uygulanir; yan etkiler (hooks) durum yerlestikten sonra cagrilir.
    WatchInstall on_result(std::uint64_t generation, WatchResult result);
    // Ertelenmis kurtarma/cikis isini sonraki taze guvenilir sonuca borclandirir.
    void owe_recovery() { owed_closed_ = true; }
    // Otomatik geri alma islem sinirinda reddedildi: kaybolmaz, sonraki taze guvenilir "oyun yok" sonucunda denenir.
    void owe_restore() { owed_restore_ = true; }

    BlockCause cause() const;
    GateContext context() const;
    GateDecision gate(GuardedAction a, const std::vector<CompatEntry>& matrix = builtin_matrix()) const;

    std::int64_t detection_age_ms() const;
    bool fresh() const { return detection_fresh(detection_age_ms()); }
    DetectionState detection() const { return state_.detection(); }
    const std::vector<std::string>& running() const { return state_.running(); }
    const std::optional<GameStart>& active() const { return state_.active(); }
    bool library_known() const { return library_known_; }
    bool library_trusted() const { return library_trusted_; }
    const std::vector<LibraryIssue>& library_issues() const { return library_issues_; }

private:
    void settle_owed();

    Clock clock_;
    Hooks hooks_;
    WatchState state_;
    std::uint64_t generation_ = 0;
    std::int64_t last_trusted_ms_ = -1;
    bool library_known_ = false, library_trusted_ = false;
    std::vector<LibraryIssue> library_issues_;
    bool owed_restore_ = false, owed_closed_ = false;
};

}  // namespace ef::core::games
