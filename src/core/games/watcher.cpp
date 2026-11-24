#include "core/games/watcher.h"

#include <algorithm>
#include <iterator>
#include <array>

#include "core/games/consent.h"

namespace ef::core::games {

std::string_view to_string(DetectionState s) noexcept {
    switch (s) {
        case DetectionState::Unknown: return "unknown";
        case DetectionState::Ok: return "ok";
        case DetectionState::Failed: return "failed";
        case DetectionState::Incomplete: return "incomplete";
    }
    return "unknown";
}

std::string exe_basename_lower(std::string_view path) {
    const auto slash = path.find_last_of("\\/");
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    std::string out(name);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return c < 0x80 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c); });
    return out;
}

bool is_helper_exe(std::string_view n) noexcept {
    static constexpr std::array<std::string_view, 25> kHelpers{
        "crashreporter.exe", "unitycrashhandler64.exe", "unitycrashhandler32.exe", "crashreportclient.exe", "easyanticheat.exe",
        "easyanticheat_eos.exe", "beservice.exe", "battleye.exe", "vgc.exe", "vgtray.exe", "steamservice.exe", "steamwebhelper.exe",
        "epicwebhelper.exe", "launcher.exe", "rockstarlauncher.exe", "playgtav.exe", "eossdk-bootstrapper.exe", "uninstall.exe",
        "riot client.exe", "riotclientservices.exe", "riotclientux.exe", "riotclientcrashhandler.exe", "leagueclient.exe", "leagueclientux.exe", "battle.net.exe"};
    if (n.rfind("unins", 0) == 0) return true;
    if (n.find("setup") != std::string_view::npos || n.find("redist") != std::string_view::npos || n.find("vc_") == 0) return true;
    return std::find(kHelpers.begin(), kHelpers.end(), n) != kHelpers.end();
}

GameWatcher::GameWatcher(std::vector<std::string> install_dirs, std::vector<std::string> game_exe_names)
{
    set_library(std::move(install_dirs), std::move(game_exe_names));
}

void GameWatcher::set_library(std::vector<std::string> install_dirs, std::vector<std::string> game_exe_names) {
    dirs_ = std::move(install_dirs);
    exe_names_ = std::move(game_exe_names);
    for (auto& d : dirs_) {
        std::transform(d.begin(), d.end(), d.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        while (!d.empty() && (d.back() == '\\' || d.back() == '/')) d.pop_back();
    }
    for (auto& n : exe_names_) n = exe_basename_lower(n);
    std::erase_if(exe_names_, [](const std::string& n) { return n.empty(); });
    std::sort(exe_names_.begin(), exe_names_.end());
    exe_names_.erase(std::unique(exe_names_.begin(), exe_names_.end()), exe_names_.end());
}

std::optional<std::string> GameWatcher::match_dir(const std::string& exe) const {
    for (const auto& d : dirs_) {
        if (!d.empty() && exe == d) return d;  // EF-X11: elle eklenen oyun anahtari tam EXE yoludur (klasor degil)
        if (!d.empty() && exe.size() > d.size() + 1 && exe.compare(0, d.size(), d) == 0 && (exe[d.size()] == '\\' || exe[d.size()] == '/'))
            return d;
    }
    return std::nullopt;
}

bool GameWatcher::known_game_exe(std::string_view name) const {
    return !name.empty() && std::binary_search(exe_names_.begin(), exe_names_.end(), exe_basename_lower(name));
}

bool GameWatcher::observed_name(std::string_view name) const {
    if (name.empty()) return false;
    const std::string n = exe_basename_lower(name);
    return std::any_of(observed_.begin(), observed_.end(), [&](const ObservedProcess& o) { return o.exe_name == n; });
}

bool GameWatcher::observed_pid(std::uint32_t pid, std::string_view name) const {
    const std::string n = exe_basename_lower(name);
    return std::any_of(observed_.begin(), observed_.end(), [&](const ObservedProcess& o) {
        return o.pid == pid && (n.empty() || o.exe_name == n);
    });
}

void GameWatcher::remember_observed(std::uint32_t pid, std::string name) {
    name = exe_basename_lower(name);
    if (pid == 0 || name.empty()) return;
    // PID yeniden kullanildiysa eski kayit ayni PID ile degistirilir; ad gecmisi ayri tutulmaz.
    for (auto& o : observed_) {
        if (o.pid == pid) {
            o.exe_name = std::move(name);
            return;
        }
    }
    if (observed_.size() >= 256) observed_.erase(observed_.begin());  // sinirli bellek
    observed_.push_back({pid, std::move(name)});
}

bool poll_proves_no_game(const WatchResult& r) noexcept {
    return r.detection == DetectionState::Ok && r.running.empty();
}

LibraryRebuild decide_library_rebuild(const LibraryRebuildInput& in) noexcept {
    if (in.no_game) return {in.must || in.wanted, in.must || in.wanted};
    return {in.must, false};
}

std::vector<std::string> ObservedExeTracker::update(const WatchResult& r, std::int64_t now_ms) {
    std::vector<std::string> out;
    if (r.detection != DetectionState::Ok || now_ms < 0) return out;  // belirsiz tarama sureyi ne uzatir ne sifirlar
    std::erase_if(seen_, [&](const Seen& s) {
        return std::none_of(r.running.begin(), r.running.end(), [&](const GameStart& g) { return g.pid == s.pid && g.create_time == s.create_time; });
    });
    for (const auto& g : r.running) {
        if (g.game_id.empty()) continue;
        auto it = std::find_if(seen_.begin(), seen_.end(), [&](const Seen& s) { return s.pid == g.pid && s.create_time == g.create_time; });
        if (it == seen_.end()) {
            if (seen_.size() >= 64) continue;  // sinirli bellek
            seen_.push_back({g.game_id, g.pid, g.create_time, now_ms, false});
            it = std::prev(seen_.end());
        }
        if (!it->reported && now_ms - it->first_ms >= kMinRunMs) {
            it->reported = true;
            out.push_back(it->id);
        }
    }
    return out;
}

WatchResult GameWatcher::update(const ProcessSnapshot& snap) {
    WatchResult r;
    r.detection = snap.state;
    r.inaccessible_count = snap.inaccessible_count;
    // Liste okunamadi: "oyun kapandi" olayi URETILMEZ; etkin oyun ve onceki durum korunur.
    if (snap.state == DetectionState::Failed || snap.state == DetectionState::Unknown) return r;

    bool uncertain = snap.state == DetectionState::Incomplete;
    std::vector<GameStart> running;
    for (const auto& p : snap.processes) {
        const bool path_ok = p.path_readable && !p.exe_path.empty();
        const std::string name = p.exe_name.empty() && path_ok ? exe_basename_lower(p.exe_path) : p.exe_name;
        const bool known = known_game_exe(name) || observed_name(name);
        if (!path_ok) {
            // Sistem sureclerinin yolunu okumak normalde reddedilir; bunu oyun saymayiz. Yalniz
            // bilinen ya da daha once oyun olarak gozlenen bir surec belirsizliktir.
            if (known || observed_pid(p.pid, name)) uncertain = true;
            continue;
        }
        const auto dir = match_dir(p.exe_path);
        // Kullanicinin elle sectigi tam EXE (anahtar == yol) adi ne olursa olsun oyundur.
        const bool helper = !(dir && *dir == p.exe_path) && is_helper_exe(exe_basename_lower(p.exe_path));
        if (p.create_time == 0) {
            // Yol okunuyor ama olusturma zamani yok: oyun klasoru altindaki yardimci olmayan
            // surec oyun olabilir; "yok" sayilmaz, belirsizlik olarak isaretlenir.
            if (dir && !helper) {
                remember_observed(p.pid, name);
                uncertain = true;
            }
            continue;
        }
        if (!dir || helper) continue;
        remember_observed(p.pid, name);
        const auto same_pid = std::any_of(running.begin(), running.end(), [&](const GameStart& g) {
            return g.pid == p.pid && g.create_time == p.create_time;
        });
        if (!same_pid) running.push_back(GameStart{normalize_game_id(p.exe_path), *dir, p.pid, p.create_time});
    }

    if (uncertain) {
        // Eksik kimlik: hangi oyunun calistigi kesinlesmeden basla/dur olayi uretilmez.
        r.detection = DetectionState::Incomplete;
        return r;
    }

    r.detection = DetectionState::Ok;
    r.running = running;
    const auto contains = [&](const GameStart& g) {
        return std::any_of(running.begin(), running.end(), [&](const GameStart& x) {
            return x.pid == g.pid && x.create_time == g.create_time;
        });
    };
    if (active_ && !contains(*active_)) {
        r.events.push_back({WatchEventKind::Stopped, *active_});
        active_.reset();
    }
    if (!active_ && !running.empty()) {
        active_ = running.front();
        r.events.push_back({WatchEventKind::Started, *active_});
    }
    return r;
}

}  // namespace ef::core::games
