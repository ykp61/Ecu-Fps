#include "core/games/manual_games.h"

#include <cctype>

#include "core/games/exe_resolver.h"

namespace ef::core::games {

namespace {
std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}
std::string strip_slashes(std::string s) {
    while (s.size() > 3 && s.back() == '\\') s.pop_back();
    return s;
}
bool drive_absolute(std::string_view s) {
    return s.size() >= 3 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':' && s[2] == '\\';
}
bool has_dot_component(std::string_view s) {
    std::size_t p = 0;
    while (p <= s.size()) {
        const std::size_t e = s.find('\\', p);
        const auto comp = s.substr(p, e == std::string_view::npos ? std::string_view::npos : e - p);
        if (comp == "." || comp == "..") return true;
        if (e == std::string_view::npos) break;
        p = e + 1;
    }
    return false;
}
}  // namespace

std::string canonical_manual_exe(std::string_view path) {
    const auto t = trim(path);
    std::string out;
    out.reserve(t.size());
    for (const char c : t) out.push_back(c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::string manual_exe_dir(std::string_view path) {
    const auto t = trim(path);
    const std::size_t slash = t.find_last_of("\\/");
    if (slash == std::string_view::npos || slash < 2) return {};
    std::string dir(t.substr(0, slash));
    if (dir.size() == 2) dir.push_back('\\');  // "C:" -> "C:\"
    return dir;
}

std::string manual_exe_name(std::string_view path) {
    const auto t = trim(path);
    const std::size_t slash = t.find_last_of("\\/");
    std::string_view name = slash == std::string_view::npos ? t : t.substr(slash + 1);
    if (name.size() > 4 && canonical_manual_exe(name.substr(name.size() - 4)) == ".exe") name.remove_suffix(4);
    return std::string(name);
}

ManualExeCheck check_manual_exe(std::string_view path, const ManualExeContext& ctx, const std::vector<std::string>& existing) {
    const std::string c = canonical_manual_exe(path);
    if (c.empty()) return ManualExeCheck::Empty;
    if (c.size() > kMaxManualPathBytes) return ManualExeCheck::TooLong;
    if (c.rfind("\\\\", 0) == 0) return ManualExeCheck::Network;
    if (!drive_absolute(c)) return ManualExeCheck::NotAbsolute;
    if (has_dot_component(c.substr(3))) return ManualExeCheck::ParentComponent;
    const std::size_t slash = c.find_last_of('\\');
    if (c.size() - slash - 1 <= 4 || c.compare(c.size() - 4, 4, ".exe") != 0) return ManualExeCheck::NotExe;
    const std::string dir = strip_slashes(canonical_manual_exe(manual_exe_dir(c)));
    if (dir.size() <= 3) return ManualExeCheck::DriveRoot;
    if (!ctx.windows_dir.empty() && path_under_dir(c, strip_slashes(canonical_manual_exe(ctx.windows_dir)))) return ManualExeCheck::SystemDir;
    for (const auto& root : ctx.exact_roots)
        if (!root.empty() && dir == strip_slashes(canonical_manual_exe(root))) return ManualExeCheck::ProtectedRoot;
    for (const auto& e : existing)
        if (canonical_manual_exe(e) == c) return ManualExeCheck::Duplicate;
    if (existing.size() >= kMaxManualGames) return ManualExeCheck::Limit;
    return ManualExeCheck::Ok;
}

std::vector<std::string> filter_manual_exes(const std::vector<std::string>& stored, const ManualExeContext& ctx) {
    std::vector<std::string> out;
    for (const auto& e : stored)
        if (check_manual_exe(e, ctx, out) == ManualExeCheck::Ok) out.push_back(e);
    return out;
}

}  // namespace ef::core::games
