#include "platform/windows/games/steam_config.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/games/launch_options.h"
#include "platform/windows/win_env.h"

namespace ef::platform::games {

namespace fs = std::filesystem;

namespace {

constexpr std::uintmax_t kMaxLocalConfig = 8u << 20;  // 8 MiB
constexpr std::uintmax_t kMaxVideoCfg = 1u << 20;     // 1 MiB

std::optional<std::string> read_bounded(const fs::path& p, std::uintmax_t max_bytes) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec || size == 0 || size > max_bytes) return std::nullopt;
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::string s(static_cast<std::size_t>(size), '\0');
    in.read(s.data(), static_cast<std::streamsize>(size));
    if (!in) return std::nullopt;
    return s;
}

std::optional<std::wstring> steam_path() {
    wchar_t buf[MAX_PATH] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buf, &size) !=
        ERROR_SUCCESS)
        return std::nullopt;
    return std::wstring(buf);
}

// En yeni değiştirilmiş cs2_video.txt (çok kullanıcılı Steam'de son oynayan kullanıcı).
std::optional<fs::path> newest_cs2_video(const fs::path& root) {
    std::error_code ec;
    const fs::path userdata = root / L"userdata";
    std::optional<fs::path> best;
    fs::file_time_type best_time{};
    for (fs::directory_iterator it(userdata, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        const fs::path p = it->path() / L"730" / L"local" / L"cfg" / L"cs2_video.txt";
        std::error_code fec;
        if (!fs::is_regular_file(p, fec)) continue;
        const auto t = fs::last_write_time(p, fec);
        if (fec) continue;
        if (!best || t > best_time) {
            best = p;
            best_time = t;
        }
    }
    return best;
}

}  // namespace

SteamLaunchOptions steam_launch_options() {
    SteamLaunchOptions out;
    const auto root = steam_path();
    if (!root) return out;
    std::error_code ec;
    const fs::path userdata = fs::path(*root) / L"userdata";
    for (fs::directory_iterator it(userdata, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (const auto txt = read_bounded(it->path() / L"config" / L"localconfig.vdf", kMaxLocalConfig)) {
            out.scanned = true;
            const auto m = core::games::parse_launch_options(*txt);
            out.options.insert(m.begin(), m.end());  // ilk kullanıcı kazanır
        }
    }
    return out;
}

std::optional<core::games::cs2::VideoSettings> steam_cs2_video() {
    const auto root = steam_path();
    if (!root) return std::nullopt;
    const auto p = newest_cs2_video(*root);
    if (!p) return std::nullopt;
    const auto txt = read_bounded(*p, kMaxVideoCfg);
    if (!txt) return std::nullopt;
    const auto kv = core::games::cs2::parse_video_cfg(*txt);
    if (!kv) return std::nullopt;
    return core::games::cs2::typed(*kv);
}

}  // namespace ef::platform::games
