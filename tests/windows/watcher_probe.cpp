// OPT-06 platform turu (pencere acmaz, ayar degistirmez): gecici "oyun klasorune" kopyalanan konsol ikilisi
// CREATE_NO_WINDOW ile baslatilir; GameWatcher gercek surec anlik goruntusuyle Started/Stopped uretmeli.
// PID + olusturma zamani kimligi ve yardimci ikili dislamasi gercek sistemde dogrulanir.
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "core/games/watcher.h"
#include "platform/windows/games/process_snapshot.h"

namespace fs = std::filesystem;

int main() {
    wchar_t tmp[MAX_PATH]{}, sys[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    GetSystemDirectoryW(sys, MAX_PATH);
    const fs::path dir = fs::path(tmp) / L"ef_watch_game";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / L"bin", ec);
    const fs::path game = dir / L"bin" / L"fakegame.exe";
    if (!CopyFileW((fs::path(sys) / L"PING.EXE").c_str(), game.c_str(), FALSE)) {
        std::printf("watcher_probe: copy failed\n");
        return 2;
    }
    std::string dir8 = dir.string();
    for (auto& c : dir8) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    ef::core::games::GameWatcher w({dir8});
    auto r = w.update(ef::platform::games::snapshot_processes());
    if (r.detection != ef::core::games::DetectionState::Ok) {
        std::printf("watcher_probe: scan_failed state=%s\n", std::string(ef::core::games::to_string(r.detection)).c_str());
        return 3;  // gercek sistemde tarama okunamadi: "oyun yok" denemez
    }
    if (!r.events.empty()) return 1;  // henuz oyun yok

    std::wstring cmd = L"\"" + game.wstring() + L"\" -n 8 127.0.0.1";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return 2;
    Sleep(500);
    r = w.update(ef::platform::games::snapshot_processes());
    const bool started = r.detection == ef::core::games::DetectionState::Ok && r.events.size() == 1 &&
                         r.events[0].kind == ef::core::games::WatchEventKind::Started && r.events[0].game.pid == pi.dwProcessId &&
                         r.events[0].game.create_time != 0;
    std::printf("watcher_probe: started=%d detection=%s pid=%lu id=%s\n", started, std::string(ef::core::games::to_string(r.detection)).c_str(),
                pi.dwProcessId, started ? r.events[0].game.game_id.c_str() : "-");
    r = w.update(ef::platform::games::snapshot_processes());
    const bool steady = r.detection == ef::core::games::DetectionState::Ok && r.events.empty();  // calismaya devam: olay yok
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    r = w.update(ef::platform::games::snapshot_processes());
    const bool stopped = r.detection == ef::core::games::DetectionState::Ok && r.events.size() == 1 &&
                         r.events[0].kind == ef::core::games::WatchEventKind::Stopped;
    std::printf("watcher_probe: steady=%d stopped=%d\n", steady, stopped);
    fs::remove_all(dir, ec);
    return started && steady && stopped ? 0 : 1;
}
