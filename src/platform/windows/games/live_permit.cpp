#include "platform/windows/games/live_permit.h"

#include <windows.h>

#include <optional>

#include "platform/windows/games/process_snapshot.h"

namespace ef::platform::games {

void LiveGamePermit::set_known_exes(std::vector<std::string> names) {
    std::lock_guard lk(mu_);
    known_exes_ = std::move(names);
}

void LiveGamePermit::set_manual_exes(std::vector<std::string> paths) {
    std::lock_guard lk(mu_);
    manual_exes_ = std::move(paths);
    library_at_ms_ = -1;
    library_source_ = core::games::PermitLibrarySource::None;
}

void LiveGamePermit::offer_library(const LibraryScanResult& library, std::int64_t scanned_at_tick_ms,
                                   const std::vector<std::string>& manual_exes) {
    std::lock_guard lk(mu_);
    // Baska kovan / eski elle eklenen liste / guvenilmez sonuc / elimizdeki daha yeni: kullanilmaz (verify kendisi tarar).
    if (!core::games::permit_offer_accepted(sid_.empty(), manual_exes == manual_exes_, library.trusted(), scanned_at_tick_ms,
                                            library_at_ms_))
        return;
    library_ = library;
    library_at_ms_ = scanned_at_tick_ms;
    library_source_ = core::games::PermitLibrarySource::Watcher;
}

core::games::GateDecision LiveGamePermit::verify(core::games::GuardedAction action) {
    try {
        return verify_locked(action);
    } catch (...) {
        return {false, "gate.detection_failed"};  // beklenmeyen hata izin degildir; surec de sonlanmaz
    }
}

core::games::GateDecision LiveGamePermit::verify_locked(core::games::GuardedAction action) {
    using namespace core::games;
    std::lock_guard lk(mu_);
    const auto now = static_cast<std::int64_t>(GetTickCount64());
    // Surec listesi her cagrida tazedir; kitaplik yalniz kisa sureli kendi taramasi ya da izleyicinin guvenilir taze
    // taramasi varsa yeniden kullanilir (Uyumluluk B8, bosluk 20). Aksi halde burada taranir (eski davranis).
    // B8 inceleme: izleyicinin bos sonucu yeniden kullanilmaz; dolu sonucu kullanilirken taze surec listesinde bilinen
    // kitaplik kokunun altinda bilinmeyen bir surec varsa (yeni kurulan oyun olabilir) kitaplik burada yeniden taranir.
    bool reuse = library_at_ms_ >= 0 && core::games::permit_library_reusable(library_source_, library_.trusted(),
                                                                             !library_.games.empty(), now - library_at_ms_);
    std::optional<ProcessSnapshot> snap;
    if (reuse && library_source_ == core::games::PermitLibrarySource::Watcher) {
        snap = snapshot_processes();
        if (core::games::permit_reuse_needs_rescan(library_.games, *snap)) reuse = false;
    }
    if (!reuse) {
        snap.reset();  // tarama suresince yeni surec baslamis olabilir: liste taramadan sonra yeniden alinir
        library_ = scan_installed_library(sid_, manual_exes_);
        library_at_ms_ = now;
        library_source_ = core::games::PermitLibrarySource::OwnScan;
    }
    GateContext c;
    c.detection_age_ms = 0;  // karar simdi alinan gozleme dayaniyor
    if (!library_.trusted()) {
        c.detection_reason_key = "gate.library_error";
        return check(action, c);
    }
    if (library_.games.empty()) {  // guvenilir "kurulu oyun yok": surec listesine gerek yok
        c.detection_ok = true;
        return check(action, c);
    }
    std::vector<std::string> dirs, exes = known_exes_;
    for (const auto& g : library_.games) {
        dirs.push_back(game_watch_key(g));  // EF-X11: elle eklenen oyun yalniz tam EXE ile eslesir
        if (!g.exe_hint.empty()) exes.push_back(exe_basename_lower(g.exe_hint));
    }
    GameWatcher w(std::move(dirs), std::move(exes));
    const auto r = w.update(snap ? *snap : snapshot_processes());
    c.detection_ok = r.detection == DetectionState::Ok;
    if (r.detection == DetectionState::Incomplete) c.detection_reason_key = "gate.detection_incomplete";
    else if (!c.detection_ok) c.detection_reason_key = "gate.detection_failed";
    for (const auto& g : r.running) c.running_game_ids.push_back(g.install_dir);
    return check(action, c);
}

}  // namespace ef::platform::games
