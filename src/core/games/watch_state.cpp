#include "core/games/watch_state.h"

#include <algorithm>

namespace ef::core::games {

namespace {

bool same_session(const GameStart& a, const GameStart& b) noexcept {
    return a.pid == b.pid && a.create_time == b.create_time;
}

}  // namespace

void WatchState::reset() {
    running_.clear();
    active_.reset();
    detection_ = DetectionState::Unknown;
    first_safe_scan_done_ = false;
    last_observed_ms_ = -1;
}

WatchInstall WatchState::apply(const WatchResult& result) {
    WatchInstall out;
    // Sira disi (daha once alinmis) gozlem daha yeni sonucu ezmez.
    if (result.observed_at_ms >= 0 && result.observed_at_ms < last_observed_ms_) {
        out.running_dirs = running_;
        out.active = active_;
        out.detection = detection_;
        out.discarded = true;
        return out;
    }
    if (result.observed_at_ms >= 0) last_observed_ms_ = result.observed_at_ms;
    out.observed_at_ms = result.observed_at_ms;
    detection_ = result.detection;
    out.detection = result.detection;
    if (result.detection != DetectionState::Ok) {
        // Basarisiz/eksik tarama "oyun yok" degildir: onceki liste ve etkin oyun korunur.
        out.running_dirs = running_;
        out.active = active_;
        return out;
    }

    const bool had_games = !running_.empty();
    const bool first = !first_safe_scan_done_;

    // 1) Nihai calisan listeyi atomik yerles (olay sirasi kullanilmaz).
    running_.clear();
    for (const auto& g : result.running) {
        if (std::find(running_.begin(), running_.end(), g.install_dir) == running_.end())
            running_.push_back(g.install_dir);
    }

    // 2) Etkin oyun: onceki oturum hala listede ise korunur; degilse ilk calisan oyun.
    //    PID yeniden kullanimi (ayni PID, farkli olusturma zamani) yeni oturumdur.
    if (active_ && std::none_of(result.running.begin(), result.running.end(),
                                [&](const GameStart& g) { return same_session(*active_, g); })) {
        active_.reset();
    }
    if (!active_ && !result.running.empty()) active_ = result.running.front();

    first_safe_scan_done_ = true;
    out.running_dirs = running_;
    out.active = active_;
    out.detection_ok = true;
    out.first_safe_scan = first;
    out.last_game_closed = had_games && running_.empty();
    out.auto_restore = out.last_game_closed || (first && running_.empty());
    return out;
}

}  // namespace ef::core::games
