// GAME-02: bu PC'deki gercek cs2_video.txt salt okuma ayristirma (dosya yoksa atlanir; asla yazilmaz).
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "core/games/cs2.h"

int main() {
    wchar_t pf[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"ProgramFiles(x86)", pf, MAX_PATH)) return 0;
    const std::wstring base = std::wstring(pf) + L"\\Steam\\userdata\\";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((base + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        std::printf("cs2_probe: steam userdata yok - atlandi\n");
        return 0;
    }
    int found = 0, parsed = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        const std::wstring f = base + fd.cFileName + L"\\730\\local\\cfg\\cs2_video.txt";
        std::ifstream in(f, std::ios::binary);
        if (!in) continue;
        ++found;
        std::stringstream ss;
        ss << in.rdbuf();
        const auto kv = ef::core::games::cs2::parse_video_cfg(ss.str());
        if (!kv) {
            std::printf("cs2_probe: parse failed\n");
            continue;
        }
        ++parsed;
        const auto v = ef::core::games::cs2::typed(*kv);
        std::printf("cs2_probe: keys=%zu version=%d res=%dx%d refresh=%.2f fullscreen=%d vsync=%d msaa=%d fsr=%d low_latency=%d\n", kv->size(),
                    v.version.value_or(-1), v.width.value_or(-1), v.height.value_or(-1), v.refresh_hz.value_or(-1), v.fullscreen.value_or(false),
                    v.vsync.value_or(false), v.msaa_samples.value_or(-1), v.fsr_detail.value_or(-1), v.low_latency.value_or(-1));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (found == 0) std::printf("cs2_probe: cs2_video.txt yok - atlandi\n");
    return found == parsed ? 0 : 1;
}
