#include "core/games/exe_resolver.h"

#include <algorithm>
#include <cctype>

namespace ef::core::games {

namespace {
std::string canon(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    for (std::size_t i = b; i < e; ++i) {
        char c = s[i] == '/' ? '\\' : s[i];
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

bool absolute_path(std::string_view s) {
    return s.size() >= 3 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':' && (s[2] == '\\' || s[2] == '/');
}

bool has_parent_component(std::string_view s) {
    std::size_t p = 0;
    while (p <= s.size()) {
        const std::size_t e = s.find_first_of("\\/", p);
        const std::string_view comp = s.substr(p, e == std::string_view::npos ? std::string_view::npos : e - p);
        if (comp == ".." || comp == ".") return true;
        if (e == std::string_view::npos) break;
        p = e + 1;
    }
    return false;
}

bool exe_name(std::string_view s) {
    return s.size() >= 4 && canon(s.substr(s.size() - 4)) == ".exe";
}

std::string join_dir(std::string_view dir, std::string_view rel) {
    std::string out = canon(dir);
    while (!out.empty() && out.back() == '\\') out.pop_back();
    out.push_back('\\');
    std::string tail = canon(rel);
    while (!tail.empty() && (tail.front() == '\\' || tail.front() == '/')) tail.erase(tail.begin());
    return out + tail;
}
}  // namespace

std::string_view to_string(ExeResolveStatus s) noexcept {
    switch (s) {
        case ExeResolveStatus::Resolved: return "resolved";
        case ExeResolveStatus::NotObserved: return "not_observed";
        case ExeResolveStatus::Ambiguous: return "ambiguous";
        case ExeResolveStatus::Invalid: return "invalid";
    }
    return "?";
}

bool path_under_dir(std::string_view path, std::string_view dir) {
    std::string p = canon(path), d = canon(dir);
    while (!d.empty() && d.back() == '\\') d.pop_back();
    if (p.size() <= d.size() + 1 || d.empty() || p.substr(0, d.size()) != d || p[d.size()] != '\\') return false;
    return !has_parent_component(p.substr(d.size() + 1));
}

GameExeResolution resolve_game_exe(std::string_view install_dir, std::string_view exe_hint,
                                   const std::vector<std::string>& observed_exe_paths) {
    GameExeResolution out;
    const std::string dir = canon(install_dir);
    if (!absolute_path(dir) || has_parent_component(dir)) {
        out.status = ExeResolveStatus::Invalid;
        return out;
    }

    // 1) Manifest ipucu (goreli veya mutlak; mutlak da kurulum klasoru altinda olmali).
    if (!exe_hint.empty()) {
        const std::string hint = canon(exe_hint);
        if (!has_parent_component(hint)) {
            const std::string cand = absolute_path(hint) ? hint : join_dir(dir, hint);
            if (exe_name(cand) && path_under_dir(cand, dir)) {
                out.status = ExeResolveStatus::Resolved;
                out.path = cand;
                return out;
            }
        }
    }

    // 2) Izleyicinin gordugu adaylar (tekille, sinir denetimi).
    std::vector<std::string> cands;
    for (const auto& raw : observed_exe_paths) {
        const std::string c = canon(raw);
        if (!absolute_path(c) || !exe_name(c) || !path_under_dir(c, dir)) continue;
        if (std::find(cands.begin(), cands.end(), c) == cands.end()) cands.push_back(c);
    }
    if (cands.empty()) {
        out.status = ExeResolveStatus::NotObserved;
        return out;
    }
    if (cands.size() > 1) {
        out.status = ExeResolveStatus::Ambiguous;
        return out;
    }
    out.status = ExeResolveStatus::Resolved;
    out.path = std::move(cands.front());
    return out;
}

}  // namespace ef::core::games
