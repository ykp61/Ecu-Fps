#include "core/games/game_icon.h"

#include <algorithm>
#include <cstring>

#include "core/games/exe_resolver.h"

namespace ef::core::games {

namespace {

bool lower_exe_suffix(std::string_view s) {
    if (s.size() < 5) return false;
    const std::string_view tail = s.substr(s.size() - 4);
    const auto low = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    return tail[0] == '.' && low(tail[1]) == 'e' && low(tail[2]) == 'x' && low(tail[3]) == 'e';
}

bool absolute_path(std::string_view s) {
    const bool letter = !s.empty() && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'));
    return s.size() >= 3 && letter && s[1] == ':' && (s[2] == '\\' || s[2] == '/');
}

// Steam appid: yalniz rakam (yol bileseni olarak kullanilir; baska karakter kabul edilmez).
bool steam_app_id(std::string_view id) {
    return !id.empty() && id.size() <= 10 && std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; });
}

void add_exe(std::vector<IconCandidate>& out, std::string_view exe) {
    IconCandidate c{IconSourceKind::ExeResource, std::string(exe)};
    if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(std::move(c));
}

}  // namespace

bool image_pixels_valid(const ImagePixels& p) noexcept { return image_pixels_valid(p, kImagePixelsMaxSide); }

bool image_pixels_valid(const ImagePixels& p, std::uint32_t max_side) noexcept {
    if (p.w == 0 || p.h == 0 || p.w > max_side || p.h > max_side) return false;
    return p.bgra.size() == static_cast<std::size_t>(p.w) * p.h * 4;
}

bool steam_icon_file_name(std::string_view name) noexcept {
    if (name.size() != 44 || name.substr(40) != ".jpg") return false;
    return std::all_of(name.begin(), name.begin() + 40, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool jpeg_stream_complete(const std::uint8_t* d, std::size_t n) noexcept {
    if (!d || n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    std::size_t i = 2;
    for (;;) {
        if (i + 1 >= n || d[i] != 0xFF) return false;
        while (i + 1 < n && d[i + 1] == 0xFF) ++i;  // dolgu baytlari
        if (i + 1 >= n) return false;
        const std::uint8_t m = d[i + 1];
        i += 2;
        if (m == 0xD9) return true;                                // EOI
        if (m == 0x00 || m == 0xD8) return false;                  // isaretci degil / ic ice SOI
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;       // uzunluksuz isaretciler (TEM, RSTn)
        if (i + 2 > n) return false;
        const std::size_t len = (static_cast<std::size_t>(d[i]) << 8) | d[i + 1];
        if (len < 2 || len > n - i) return false;
        i += len;
        if (m != 0xDA) continue;
        // SOS: entropi kodlu veri. 0xFF ardindan 0x00 (doldurma) ya da RSTn veridir; baska deger sonraki isaretcidir.
        for (;;) {
            const auto* ff = static_cast<const std::uint8_t*>(std::memchr(d + i, 0xFF, n - i));
            if (!ff) return false;
            const auto j = static_cast<std::size_t>(ff - d);
            if (j + 1 >= n) return false;
            const std::uint8_t nx = d[j + 1];
            if (nx == 0x00 || (nx >= 0xD0 && nx <= 0xD7)) {
                i = j + 2;
            } else if (nx == 0xFF) {
                i = j + 1;
            } else {
                i = j;
                break;
            }
        }
    }
}

std::vector<IconCandidate> icon_candidates(const inventory::GameInstall& game, std::string_view resolved_exe, std::string_view steam_root,
                                           LibraryFs& fs) {
    std::vector<IconCandidate> out;
    if (game.launcher == "manual") {
        // Elle eklenen oyun: klasor guvenilir kok degildir; yalniz kullanicinin sectigi EXE'nin kendi ikonu.
        if (!game.manual_missing && absolute_path(game.manual_exe) && lower_exe_suffix(game.manual_exe)) add_exe(out, game.manual_exe);
        return out;
    }
    if (game.launcher == "steam" && steam_app_id(game.app_id) && absolute_path(steam_root)) {
        std::string dir(steam_root);
        while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/')) dir.pop_back();
        dir += "\\appcache\\librarycache\\" + game.app_id;
        const DirList list = fs.list(dir);
        if (list.status == DirList::Status::Ok) {  // yarim liste "tek dosya" kaniti degildir
            const std::string* only = nullptr;
            std::size_t n = 0;
            for (const auto& name : list.names)
                if (steam_icon_file_name(name)) {
                    only = &name;
                    ++n;
                }
            if (n == 1) out.push_back({IconSourceKind::ImageFile, dir + "\\" + *only});
        }
    }
    const auto exe_ok = [&](std::string_view exe) {
        return absolute_path(exe) && lower_exe_suffix(exe) && path_under_dir(exe, game.install_dir);
    };
    if (exe_ok(resolved_exe)) add_exe(out, resolved_exe);
    else if (resolved_exe.empty() && exe_ok(game.manual_exe)) add_exe(out, game.manual_exe);
    return out;
}

}  // namespace ef::core::games
