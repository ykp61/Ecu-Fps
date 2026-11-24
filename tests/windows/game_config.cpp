// GAME-04: config ayristirma hatasi oyunun dosyasini bozmaz. Gercek cs2_video.txt GECICI KOPYASI uzerinde
// (oyunun dosyasina dokunulmaz): bozuk kopyaya yazim reddedilir ve bayt degismez; saglam kopyada tek alan
// degisir, geri alma birebir doner.
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "core/games/cs2.h"

namespace {
std::string read_all(const std::wstring& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
void write_all(const std::wstring& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}
std::wstring find_real() {
    wchar_t pf[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"ProgramFiles(x86)", pf, MAX_PATH)) return {};
    const std::wstring base = std::wstring(pf) + L"\\Steam\\userdata\\";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((base + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        const auto f = base + fd.cFileName + L"\\730\\local\\cfg\\cs2_video.txt";
        if (GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES) found = f;
    } while (found.empty() && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}
}  // namespace

int main() {
    using namespace ef::core::games;
    std::string src = "\"video.cfg\"\n{\n\t\"Version\"\t\t\"16\"\n\t\"setting.mat_vsync\"\t\t\"0\"\n}\n";
    const auto real = find_real();
    if (!real.empty()) src = read_all(real);  // gercek icerik, yalniz okunur
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring copy = std::wstring(tmp) + L"ef_game_config_test.txt";

    // 1) Bozuk kopya: yazim reddedilir, bayt degismez
    const std::string corrupt = src.substr(0, src.size() / 2);
    write_all(copy, corrupt);
    if (cs2::set_field(read_all(copy), "setting.mat_vsync", "1")) {
        std::printf("game_config: corrupt file accepted\n");
        return 1;
    }
    if (read_all(copy) != corrupt) return 1;
    // 2) Saglam kopya: tek alan + birebir geri alma
    write_all(copy, src);
    const auto kv = cs2::parse_video_cfg(src);
    if (!kv || !kv->count("setting.mat_vsync")) {
        std::printf("game_config: source not parseable\n");
        return 1;
    }
    const auto old = kv->at("setting.mat_vsync");
    const std::string want = old == "0" ? "1" : "0";
    const auto w = cs2::set_field(read_all(copy), "setting.mat_vsync", want);
    if (!w) return 1;
    write_all(copy, *w);
    const auto r = cs2::restore_field(read_all(copy), "setting.mat_vsync", old, want);
    if (!r.text || r.conflict) return 1;
    write_all(copy, *r.text);
    const bool identical = read_all(copy) == src;
    DeleteFileW(copy.c_str());
    std::printf("game_config: source=%s corrupt_refused=1 roundtrip_identical=%d bytes=%zu\n", real.empty() ? "synthetic" : "real_copy", identical,
                src.size());
    return identical ? 0 : 1;
}
