#include "core/games/detection_coordinator.h"

namespace ef::core::games {

std::string_view reason_key(BlockCause c) noexcept {
    switch (c) {
        case BlockCause::None: return {};
        case BlockCause::GameRunning: return "gate.game_running";
        case BlockCause::FirstScan: return "gate.detection_pending";
        case BlockCause::LibraryError: return "gate.library_error";
        case BlockCause::ProcessIdentity: return "gate.detection_incomplete";
        case BlockCause::ProcessListFailed: return "gate.detection_failed";
        case BlockCause::Stale: return "gate.detection_stale";
    }
    return "gate.detection_failed";
}

std::string_view title_key(BlockCause c) noexcept {
    switch (c) {
        case BlockCause::None: return {};
        case BlockCause::GameRunning: return "gate.game_running_title";
        case BlockCause::FirstScan: return "gate.detection_pending_title";
        case BlockCause::LibraryError: return "gate.library_error_title";
        case BlockCause::ProcessIdentity: return "gate.detection_incomplete_title";
        case BlockCause::ProcessListFailed: return "gate.detection_failed_title";
        case BlockCause::Stale: return "gate.detection_stale_title";
    }
    return "gate.detection_failed_title";
}

void DetectionCoordinator::begin_session(std::uint64_t generation) {
    generation_ = generation;
    state_.reset();
    last_trusted_ms_ = -1;
}

void DetectionCoordinator::set_library(const LibraryScanResult& r) {
    library_known_ = true;
    library_trusted_ = r.trusted();
    library_issues_ = r.issues;
}

std::int64_t DetectionCoordinator::detection_age_ms() const {
    if (last_trusted_ms_ < 0) return -1;
    return clock_() - last_trusted_ms_;
}

WatchInstall DetectionCoordinator::on_result(std::uint64_t generation, WatchResult result) {
    if (generation != generation_) {
        WatchInstall dropped;
        dropped.running_dirs = state_.running();
        dropped.active = state_.active();
        dropped.detection = state_.detection();
        dropped.discarded = true;
        return dropped;
    }
    // Kitaplik taramasi guvenilir degilse izleyicinin Ok sonucu "oyun yok" kaniti degildir.
    if ((!library_known_ || !library_trusted_) && result.detection == DetectionState::Ok) result.detection = DetectionState::Incomplete;
    auto install = state_.apply(result);
    if (install.discarded) return install;
    if (install.detection_ok && install.observed_at_ms > last_trusted_ms_) last_trusted_ms_ = install.observed_at_ms;
    if (install.auto_restore) owed_restore_ = true;
    if (install.last_game_closed) owed_closed_ = true;
    if (install.detection_ok) settle_owed();
    return install;
}

void DetectionCoordinator::settle_owed() {
    if (!state_.running().empty() || !fresh()) return;  // oyun yeniden acildi veya kanit bayat: sonraki taze sonuca kalir
    const bool restore = owed_restore_, closed = owed_closed_;
    owed_restore_ = owed_closed_ = false;
    if (restore && hooks_.auto_restore) hooks_.auto_restore();
    if (closed) {
        if (hooks_.pending_exit) hooks_.pending_exit();
        if (hooks_.pending_recovery) hooks_.pending_recovery();
    }
}

BlockCause DetectionCoordinator::cause() const {
    // Once kanitin kendisi: okunamayan kitaplik veya basarisiz tarama varken "oyun calisiyor" eski liste olabilir.
    // Boyle bir durumda kullaniciya gercek neden ve yeniden tarama yolu gosterilir (kapi yine kapali).
    if (library_known_ && !library_trusted_) return BlockCause::LibraryError;
    switch (state_.detection()) {
        case DetectionState::Unknown: return BlockCause::FirstScan;
        case DetectionState::Failed: return BlockCause::ProcessListFailed;
        case DetectionState::Incomplete: return BlockCause::ProcessIdentity;
        case DetectionState::Ok: break;
    }
    if (!fresh()) return BlockCause::Stale;
    return state_.running().empty() ? BlockCause::None : BlockCause::GameRunning;
}

GateContext DetectionCoordinator::context() const {
    GateContext c;
    c.running_game_ids = state_.running();
    c.detection_ok = state_.detection() == DetectionState::Ok && library_known_ && library_trusted_;
    c.detection_age_ms = detection_age_ms();
    if (!c.detection_ok) c.detection_reason_key = std::string(reason_key(cause()));
    return c;
}

GateDecision DetectionCoordinator::gate(GuardedAction a, const std::vector<CompatEntry>& matrix) const { return check(a, context(), matrix); }

}  // namespace ef::core::games
