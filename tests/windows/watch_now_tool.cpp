// OPT-06 salt okuma araci (ctest disi): kurulu oyun klasorleriyle GameWatcher'i bir kez calistirir ve su an
// calisan oyunu raporlar. Pencere acmaz, ayar degistirmez, oyun surecine dokunmaz.
#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "core/games/library_scan.h"
#include "core/games/watcher.h"
#include "platform/windows/games/library_scan.h"
#include "platform/windows/games/process_snapshot.h"

int main(int argc, char** argv) {
    std::vector<std::string> dirs;
    ef::core::games::LibraryScanOptions options;
    options.xbox_packages = argc > 1 && std::string_view(argv[1]) == "--xbox";  // yalniz dogrulama; urunde kXboxPackagesValidated
    ef::platform::games::WinLibraryFs fs;
    const auto t0 = std::chrono::steady_clock::now();
    const auto library = ef::core::games::scan_library(fs, options);
    const long long scan_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    for (const auto& g : library.games) dirs.push_back(g.install_dir);
    static const char* const kStates[] = {"ok", "empty", "partial", "failed"};
    std::printf("library: state=%s games=%zu excluded_non_games=%zu malformed=%zu unreadable=%zu source_errors=%zu trusted=%d\n",
                kStates[static_cast<int>(library.state)], library.games.size(), library.excluded_non_games, library.malformed_manifests,
                library.unreadable_manifests, library.source_errors, library.trusted() ? 1 : 0);
    std::printf("scan_ms=%lld\n", scan_ms);
    for (const auto& p : library.providers)
        std::printf("  provider=%s status=%s games=%zu\n", p.provider.c_str(), std::string(ef::core::games::provider_status_key(p.status)).c_str(), p.games);
    for (const auto& i : library.issues)
        std::printf("  issue=%s launcher=%s\n", std::string(ef::core::games::issue_key(i.kind)).c_str(), i.launcher.c_str());
    for (const auto& g : library.games)
        if (g.launcher == "xbox") std::printf("  xbox id=%s dir=%s exe_hint=%s\n", g.app_id.c_str(), g.install_dir.c_str(), g.exe_hint.c_str());
    ef::core::games::GameWatcher w(dirs);
    const auto r = w.update(ef::platform::games::snapshot_processes());
    std::printf("watch_now: installed_dirs=%zu detection=%s events=%zu running=%zu inaccessible=%zu\n", dirs.size(),
                std::string(ef::core::games::to_string(r.detection)).c_str(), r.events.size(), r.running.size(), r.inaccessible_count);
    for (const auto& e : r.events)
        std::printf("  %s pid=%u create_time=%llu id=%s dir=%s\n", e.kind == ef::core::games::WatchEventKind::Started ? "started" : "stopped",
                    e.game.pid, static_cast<unsigned long long>(e.game.create_time), e.game.game_id.c_str(), e.game.install_dir.c_str());
    return r.detection == ef::core::games::DetectionState::Ok ? 0 : 1;
}
