#include "core/adapters/windows/gaming_toggles.h"

#include <array>

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Status;
namespace rec = core::recovery;

namespace {
constexpr std::wstring_view kDxKey = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
constexpr std::wstring_view kDxName = L"DirectXUserGlobalSettings";
constexpr std::array<GamingTarget, 7> kTargets{{
    {"gaming:game_mode", L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", false, L""},
    {"gaming:dvr_background", L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"HistoricalCaptureEnabled", false, L""},
    {"gaming:app_capture", L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", false, L""},
    {"gaming:dvr_enabled", L"System\\GameConfigStore", L"GameDVR_Enabled", false, L""},
    {"gaming:gamebar_startup", L"Software\\Microsoft\\GameBar", L"ShowStartupPanel", false, L""},
    {"gaming:windowed_opt", kDxKey, kDxName, true, L"SwapEffectUpgradeEnable"},
    {"gaming:windows_vrr", kDxKey, kDxName, true, L"VRROptimizeEnable"},
}};
constexpr std::string_view kAbsent = "<absent>";

std::string hex(const std::vector<std::uint8_t>& b) {
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (auto x : b) s.push_back(d[x >> 4]), s.push_back(d[x & 15]);
    return s;
}
std::optional<std::vector<std::uint8_t>> unhex(std::string_view s) {
    if (s.size() % 2) return std::nullopt;
    std::vector<std::uint8_t> b;
    for (std::size_t i = 0; i < s.size(); i += 2) {
        auto n = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
        const int a = n(s[i]), c = n(s[i + 1]);
        if (a < 0 || c < 0) return std::nullopt;
        b.push_back(static_cast<std::uint8_t>(a * 16 + c));
    }
    return b;
}
bool valid_flag(std::string_view v) { return v == "0" || v == "1"; }
std::wstring wflag(std::string_view v) { return v == "1" ? L"1" : L"0"; }

// Hedefin etkin "0"/"1" degeri (yoksa nullopt).
contracts::Result<std::optional<std::string>> current_flag(UserRegistryApi& api, const GamingTarget& t) {
    using R = contracts::Result<std::optional<std::string>>;
    auto r = api.read(t.subkey, t.name);
    if (!r.ok()) return r.error();
    if (!r.value()) return std::optional<std::string>{};
    if (t.is_dx_flag) {
        const auto s = raw_sz(*r.value());
        if (!s) return R::fail(ErrorCode::ParseError, "dx settings not a string");
        const auto f = dx_flag_get(*s, t.dx_key);
        if (!f) return std::optional<std::string>{};
        return std::optional<std::string>{*f == L"1" ? "1" : "0"};
    }
    const auto d = raw_dword(*r.value());
    if (!d) return R::fail(ErrorCode::ParseError, "not a dword");
    return std::optional<std::string>{*d != 0 ? "1" : "0"};
}
}  // namespace

const GamingTarget* find_gaming_target(std::string_view id) noexcept {
    for (const auto& t : kTargets)
        if (t.id == id) return &t;
    return nullptr;
}

std::optional<std::wstring> dx_flag_get(std::wstring_view s, std::wstring_view key) {
    std::size_t p = 0;
    while (p < s.size()) {
        const auto e = s.find(L';', p);
        const auto item = s.substr(p, e == std::wstring_view::npos ? std::wstring_view::npos : e - p);
        const auto eq = item.find(L'=');
        if (eq != std::wstring_view::npos && item.substr(0, eq) == key) return std::wstring(item.substr(eq + 1));
        if (e == std::wstring_view::npos) break;
        p = e + 1;
    }
    return std::nullopt;
}

std::wstring dx_flag_set(std::wstring_view s, std::wstring_view key, std::wstring_view value) {
    std::wstring out;
    bool found = false;
    std::size_t p = 0;
    while (p < s.size()) {
        const auto e = s.find(L';', p);
        const auto item = s.substr(p, e == std::wstring_view::npos ? std::wstring_view::npos : e - p);
        if (!item.empty()) {
            const auto eq = item.find(L'=');
            if (eq != std::wstring_view::npos && item.substr(0, eq) == key) {
                found = true;
                if (!value.empty()) out.append(key).append(L"=").append(value).append(L";");  // bos deger = ogeyi kaldir
            } else {
                out.append(item).append(L";");
            }
        }
        if (e == std::wstring_view::npos) break;
        p = e + 1;
    }
    if (!found && !value.empty()) out.append(key).append(L"=").append(value).append(L";");
    return out;
}

RegRaw dword_raw(std::uint32_t v) {
    return {4, {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 24)}};
}
std::optional<std::uint32_t> raw_dword(const RegRaw& r) {
    if (r.type != 4 || r.bytes.size() != 4) return std::nullopt;
    return static_cast<std::uint32_t>(r.bytes[0]) | (static_cast<std::uint32_t>(r.bytes[1]) << 8) | (static_cast<std::uint32_t>(r.bytes[2]) << 16) |
           (static_cast<std::uint32_t>(r.bytes[3]) << 24);
}
RegRaw sz_raw(std::wstring_view s) {
    RegRaw r{1, {}};
    for (wchar_t c : s) r.bytes.push_back(static_cast<std::uint8_t>(c & 0xFF)), r.bytes.push_back(static_cast<std::uint8_t>((c >> 8) & 0xFF));
    r.bytes.push_back(0), r.bytes.push_back(0);
    return r;
}
std::optional<std::wstring> raw_sz(const RegRaw& r) {
    if ((r.type != 1 && r.type != 2) || r.bytes.size() % 2) return std::nullopt;
    std::wstring s;
    for (std::size_t i = 0; i + 1 < r.bytes.size(); i += 2) {
        const wchar_t c = static_cast<wchar_t>(r.bytes[i] | (r.bytes[i + 1] << 8));
        if (c == 0) break;
        s.push_back(c);
    }
    return s;
}

contracts::Result<rec::Snapshot> GamingToggleAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<rec::Snapshot>;
    const auto* t = find_gaming_target(op.target);
    if (!t) return R::fail(ErrorCode::Denied, "target not allowlisted");
    auto r = api_.read(t->subkey, t->name);
    if (!r.ok()) return r.error();
    rec::Snapshot s;
    s.existed = r.value().has_value();
    if (s.existed) {
        s.raw_type = std::to_string(r.value()->type);
        s.raw = hex(r.value()->bytes);
    }
    if (t->is_dx_flag) {
        // Ortak dize: ogenin onceki degeri (yoksa <absent>); geri almada yalniz bu oge degisir.
        std::optional<std::wstring> f;
        if (s.existed) {
            const auto str = raw_sz(*r.value());
            if (!str) return R::fail(ErrorCode::ParseError, "dx settings not a string");
            f = dx_flag_get(*str, t->dx_key);
        }
        s.old_value = f ? (*f == L"1" ? "1" : "0") : std::string(kAbsent);
        return s;
    }
    if (s.existed) {
        const auto d = raw_dword(*r.value());
        if (!d) return R::fail(ErrorCode::ParseError, "not a dword");
        s.old_value = *d != 0 ? "1" : "0";
    }
    return s;
}

Status GamingToggleAdapter::apply(const contracts::SettingOperation& op) {
    const auto* t = find_gaming_target(op.target);
    if (!t) return Status::fail(ErrorCode::Denied, "target not allowlisted");
    if (!valid_flag(op.new_value)) return Status::fail(ErrorCode::OutOfRange, "value must be 0/1");
    if (!t->is_dx_flag) return api_.write(t->subkey, t->name, dword_raw(op.new_value == "1" ? 1u : 0u));
    auto r = api_.read(t->subkey, t->name);
    if (!r.ok()) return Status(r.error());
    std::wstring cur;
    if (r.value()) {
        const auto s = raw_sz(*r.value());
        if (!s) return Status::fail(ErrorCode::ParseError, "dx settings not a string");
        cur = *s;
    }
    return api_.write(t->subkey, t->name, sz_raw(dx_flag_set(cur, t->dx_key, wflag(op.new_value))));
}

contracts::Result<std::optional<std::string>> GamingToggleAdapter::read_back(const contracts::SettingOperation& op) {
    const auto* t = find_gaming_target(op.target);
    if (!t) return contracts::Result<std::optional<std::string>>::fail(ErrorCode::Denied, "target not allowlisted");
    return current_flag(api_, *t);
}

Status GamingToggleAdapter::restore(const contracts::SettingOperation& op, const rec::Snapshot& snap) {
    const auto* t = find_gaming_target(op.target);
    if (!t) return Status::fail(ErrorCode::Denied, "target not allowlisted");
    if (!t->is_dx_flag) {
        if (!snap.existed) return api_.remove(t->subkey, t->name);
        const auto b = unhex(snap.raw);
        if (!b || snap.raw_type.empty()) return Status::fail(ErrorCode::ParseError, "snapshot raw");
        return api_.write(t->subkey, t->name, RegRaw{static_cast<std::uint32_t>(std::stoul(snap.raw_type)), *b});
    }
    // Ortak dize: yalniz bu oge eski haline doner; baska ogeler (kullanicinin sonradan degistirdikleri) korunur.
    auto r = api_.read(t->subkey, t->name);
    if (!r.ok()) return Status(r.error());
    std::wstring cur;
    if (r.value()) {
        const auto s = raw_sz(*r.value());
        if (!s) return Status::fail(ErrorCode::ParseError, "dx settings not a string");
        cur = *s;
    }
    const std::wstring next = dx_flag_set(cur, t->dx_key, snap.old_value == kAbsent ? L"" : wflag(snap.old_value));
    if (next.empty() && !snap.existed) return api_.remove(t->subkey, t->name);  // onceden hic yoktu
    return api_.write(t->subkey, t->name, sz_raw(next));
}

bool GamingToggleAdapter::matches_snapshot(const contracts::SettingOperation& op, const rec::Snapshot& snap,
                                           const std::optional<std::string>& v) {
    const auto* t = find_gaming_target(op.target);
    if (!t || !t->is_dx_flag) return SettingAdapter::matches_snapshot(op, snap, v);
    // Ortak dize: ogenin onceki durumu old_value'dadir ("<absent>" = oge yoktu; dizenin kendisi olsa da olmasa da).
    if (snap.old_value == kAbsent) return !v;
    return v && *v == snap.old_value;
}

// ---- FakeUserRegistry ----
contracts::Result<std::optional<RegRaw>> FakeUserRegistry::read(std::wstring_view subkey, std::wstring_view name) {
    const auto it = values.find(key(subkey, name));
    if (it == values.end()) return std::optional<RegRaw>{};
    return std::optional<RegRaw>{it->second};
}
Status FakeUserRegistry::write(std::wstring_view subkey, std::wstring_view name, const RegRaw& v) {
    values[key(subkey, name)] = v;
    return contracts::ok_status();
}
Status FakeUserRegistry::remove(std::wstring_view subkey, std::wstring_view name) {
    values.erase(key(subkey, name));
    return contracts::ok_status();
}

}  // namespace ef::core::adapters::windows
