#include "core/adapters/windows/defender_exclusion.h"

#include <algorithm>
#include <cctype>

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Status;

namespace {
using OptStr = std::optional<std::string>;

std::string hex_encode(std::string_view b) {
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (const unsigned char x : b) s.push_back(d[x >> 4]), s.push_back(d[x & 15]);
    return s;
}

std::optional<std::string> hex_decode(std::string_view h) {
    if (h.empty() || h.size() % 2 != 0 || h.size() > 2048) return std::nullopt;
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;  // yalniz kucuk harf: ayni yol tek hedef gosterimine sahip olsun
    };
    std::string out;
    out.reserve(h.size() / 2);
    for (std::size_t i = 0; i < h.size(); i += 2) {
        const int hi = val(h[i]), lo = val(h[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

std::vector<std::string> components(std::string_view canon) {
    std::vector<std::string> v;
    std::size_t start = 3;  // "x:\" sonrasi
    while (start <= canon.size()) {
        const auto e = canon.find('\\', start);
        v.emplace_back(canon.substr(start, e == std::string_view::npos ? std::string_view::npos : e - start));
        if (e == std::string_view::npos) break;
        start = e + 1;
    }
    return v;
}

Status denied(std::string why) { return Status::fail(ErrorCode::Denied, std::move(why)); }

bool contains_path(const std::vector<std::string>& list, const std::string& canon) {
    for (const auto& p : list)
        if (defender_canonical_path(p) == canon) return true;
    return false;
}

Status require_usable(DefenderApi& api) {
    const auto u = api.usable();
    if (u != std::optional<bool>(true)) return denied("defender exclusions are not effective on this system");
    return contracts::ok_status();
}
}  // namespace

std::string defender_canonical_path(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (const char c : path) out.push_back(c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    while (out.size() > 3 && out.back() == '\\') out.pop_back();
    return out;
}

bool defender_path_shape_ok(std::string_view path) {
    const std::string c = defender_canonical_path(path);
    if (c.size() < 6 || c.size() > 1000) return false;
    if (!(c[0] >= 'a' && c[0] <= 'z') || c[1] != ':' || c[2] != '\\') return false;
    for (std::size_t i = 0; i < c.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(c[i]);
        if (ch < 0x20) return false;
        if (ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|' || ch == '%') return false;
        if (ch == ':' && i != 1) return false;
    }
    const auto comp = components(c);
    if (comp.size() < 2) return false;  // surucu kokunden en az 2 seviye asagida
    for (const auto& s : comp) {
        if (s.empty() || s == "." || s == ".." || s.back() == '.' || s.back() == ' ') return false;
    }
    const std::string& first = comp[0];
    if (first == "windows") return false;                    // %SystemRoot% ve altindaki her sey
    if (first == "users" && comp.size() <= 2) return false;  // kullanici profili koku (ve Users altindaki ortak klasorler)
    if (first == "program files" || first == "program files (x86)" || first == "programdata") {
        if (comp[1] == "windowsapps" || comp[1] == "modifiablewindowsapps") return false;
    }
    const std::string& last = comp.back();
    if (last == "steamapps" || last == "windowsapps") return false;
    if (comp.size() >= 2 && comp[comp.size() - 2] == "steamapps" && last == "common") return false;  // yalniz belirli oyun klasoru
    return true;
}

bool defender_path_allowed(std::string_view path, const std::vector<std::string>& trusted_game_roots) {
    if (!defender_path_shape_ok(path)) return false;
    const std::string c = defender_canonical_path(path);
    for (const auto& r : trusted_game_roots) {
        if (!defender_path_shape_ok(r)) continue;
        if (defender_canonical_path(r) == c) return true;
    }
    return false;
}

std::string defender_target_for_path(std::string_view path) {
    if (!defender_path_shape_ok(path)) return {};
    return std::string(kDefenderTargetPrefix) + hex_encode(defender_canonical_path(path));
}

std::optional<std::string> defender_path_from_target(std::string_view t) {
    if (t.size() <= kDefenderTargetPrefix.size() || t.substr(0, kDefenderTargetPrefix.size()) != kDefenderTargetPrefix) return std::nullopt;
    const auto bytes = hex_decode(t.substr(kDefenderTargetPrefix.size()));
    if (!bytes) return std::nullopt;
    if (!defender_path_shape_ok(*bytes)) return std::nullopt;
    if (defender_canonical_path(*bytes) != *bytes) return std::nullopt;  // yalniz kanonik bicim (tek gosterim)
    return *bytes;
}

contracts::Result<recovery::Snapshot> DefenderExclusionAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    if (op.scope != contracts::OperationScope::Machine) return R::fail(ErrorCode::Denied, "defender exclusion is machine scope");
    const auto path = defender_path_from_target(op.target);
    if (!path) return R::fail(ErrorCode::Denied, "invalid defender exclusion target");
    if (auto u = require_usable(api_); !u.ok()) return R(u.error());
    auto list = api_.exclusion_paths();
    if (!list.ok()) return R(list.error());  // liste okunamadi: "onceden dislanmis mi" bilinmez; yazilmaz
    recovery::Snapshot snap;
    snap.existed = contains_path(list.value(), *path);  // onceden dislanmissa Kapali'da kaldirilmaz
    snap.old_value = "1";
    snap.raw_type = "defender_exclusion";
    return snap;
}

Status DefenderExclusionAdapter::apply(const contracts::SettingOperation& op) {
    if (op.new_value != "1") return denied("only '1' may be applied");
    if (op.scope != contracts::OperationScope::Machine) return denied("defender exclusion is machine scope");
    const auto path = defender_path_from_target(op.target);
    if (!path) return denied("invalid defender exclusion target");
    if (auto u = require_usable(api_); !u.ok()) return u;
    return api_.add_path(*path);
}

contracts::Result<OptStr> DefenderExclusionAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<OptStr>;
    const auto path = defender_path_from_target(op.target);
    if (!path) return R::fail(ErrorCode::ParseError, "defender exclusion target");
    auto list = api_.exclusion_paths();
    if (!list.ok()) return R(list.error());
    if (contains_path(list.value(), *path)) return OptStr{std::string("1")};
    return OptStr{};
}

Status DefenderExclusionAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    const auto path = defender_path_from_target(op.target);
    if (!path) return Status::fail(ErrorCode::ParseError, "defender exclusion target");
    if (snap.existed) return contracts::ok_status();  // onceden vardi: bizim eklemedigimiz dislama kalir
    auto list = api_.exclusion_paths();
    if (!list.ok()) return Status(list.error());
    if (!contains_path(list.value(), *path)) return contracts::ok_status();  // zaten yok
    return api_.remove_path(*path);
}

bool defender_admin_allowed(const contracts::SettingOperation& op) noexcept {
    if (op.scope != contracts::OperationScope::Machine || op.new_value != "1") return false;
    try {
        return defender_path_from_target(op.target).has_value();
    } catch (...) {
        return false;
    }
}

contracts::Result<std::vector<std::string>> FakeDefenderApi::exclusion_paths() {
    if (deny_list) return contracts::Result<std::vector<std::string>>::fail(ErrorCode::Denied, "must be an administrator");
    return paths;
}

Status FakeDefenderApi::add_path(const std::string& path) {
    if (deny_write) return denied("fake deny");
    ++adds;
    if (!contains_path(paths, defender_canonical_path(path))) paths.push_back(path);
    return contracts::ok_status();
}

Status FakeDefenderApi::remove_path(const std::string& path) {
    if (deny_write) return denied("fake deny");
    ++removes;
    const auto c = defender_canonical_path(path);
    paths.erase(std::remove_if(paths.begin(), paths.end(), [&](const std::string& p) { return defender_canonical_path(p) == c; }), paths.end());
    return contracts::ok_status();
}

}  // namespace ef::core::adapters::windows
