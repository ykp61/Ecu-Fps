#include "core/adapters/windows/ifeo_priority.h"

#include <array>
#include <cctype>

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::OperationId;
using contracts::OperationScope;
using contracts::Status;
namespace rec = core::recovery;

namespace {
constexpr std::wstring_view kIfeoRoot = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\";
constexpr std::wstring_view kPerfOptions = L"PerfOptions";

// Cekirdek anti-cheat kullanan oyun EKLENMEZ. CS2: FACEIT kuruluysa plan katmani ayrica eler.
constexpr std::array<PriorityGame, 3> kGames{{
    {"cs2.exe", "730"},
    {"dota2.exe", "570"},
    {"Minecraft.Windows.exe", ""},
}};

constexpr std::string_view kExeNew = "exe_new";    // <exe> anahtari bizden once yoktu (PerfOptions da yoktu)
constexpr std::string_view kPerfNew = "perf_new";  // yalniz PerfOptions anahtari yoktu

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
std::string to_lower(std::string_view s) {
    std::string o;
    for (char c : s) o.push_back(lower(c));
    return o;
}
bool name_ok(std::string_view s) {
    if (s.size() < 5 || s.size() > 64) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ' ' || c == '-')) return false;
    return s.substr(s.size() - 4) == ".exe";
}

std::wstring widen(std::string_view s) {
    std::wstring w;
    for (char c : s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return w;
}

std::string hex(const std::vector<std::uint8_t>& b) {
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    for (auto x : b) s.push_back(d[x >> 4]), s.push_back(d[x & 15]);
    return s;
}
std::optional<std::vector<std::uint8_t>> unhex(std::string_view s) {
    if (s.size() % 2) return std::nullopt;
    std::vector<std::uint8_t> b;
    auto n = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (std::size_t i = 0; i < s.size(); i += 2) {
        const int a = n(s[i]), c = n(s[i + 1]);
        if (a < 0 || c < 0) return std::nullopt;
        b.push_back(static_cast<std::uint8_t>(a * 16 + c));
    }
    return b;
}

// Okunan ham degerin metni; capture ve read_back ayni kurali kullanir. 6 -> "above_normal", baska DWORD -> ondalik.
std::string render(const RegRaw& r) {
    if (r.type == kTweakRegDword) {
        if (const auto d = raw_dword(r)) return *d == kIfeoCpuPriorityAboveNormal ? std::string(kIfeoPriorityValue) : std::to_string(*d);
    }
    return "raw:" + std::to_string(r.type) + ":" + hex(r.bytes);
}

// Islemi cozer: yalniz GamePriorityHint, Machine kapsam, gecerli ve izin listesindeki hedef.
std::optional<std::string> resolve(const contracts::SettingOperation& op) {
    if (op.id != OperationId::GamePriorityHint || op.scope != OperationScope::Machine) return std::nullopt;
    return ifeo_exe_from_target(op.target);
}
}  // namespace

std::span<const PriorityGame> priority_games() noexcept { return {kGames.data(), kGames.size()}; }

const PriorityGame* find_priority_game(std::string_view exe) noexcept {
    const auto want = to_lower(exe);
    for (const auto& g : kGames)
        if (to_lower(g.exe) == want) return &g;
    return nullptr;
}

std::string ifeo_priority_target(std::string_view exe) {
    const auto low = to_lower(exe);
    if (!name_ok(low) || !find_priority_game(low)) return {};
    return std::string(kIfeoTargetPrefix) + low;
}

std::optional<std::string> ifeo_exe_from_target(std::string_view target) {
    if (target.substr(0, kIfeoTargetPrefix.size()) != kIfeoTargetPrefix) return std::nullopt;
    const auto name = target.substr(kIfeoTargetPrefix.size());
    if (!name_ok(name)) return std::nullopt;  // buyuk harf name_ok'tan gecmez
    const auto* g = find_priority_game(name);
    if (!g) return std::nullopt;
    return std::string(g->exe);
}

std::wstring ifeo_exe_subkey(std::string_view canonical_exe) { return std::wstring(kIfeoRoot) + widen(canonical_exe); }
std::wstring ifeo_perf_subkey(std::string_view canonical_exe) {
    return ifeo_exe_subkey(canonical_exe) + L"\\" + std::wstring(kPerfOptions);
}

contracts::Result<std::optional<RegRaw>> IfeoPriorityAdapter::read_value(std::string_view canonical_exe) {
    using R = contracts::Result<std::optional<RegRaw>>;
    if (!hklm_) return R::fail(ErrorCode::Unsupported, "machine registry unavailable");
    if (!find_priority_game(canonical_exe)) return R::fail(ErrorCode::Denied, "exe not allowlisted");
    return hklm_->read(ifeo_perf_subkey(canonical_exe), kIfeoValueName);
}

contracts::Result<rec::Snapshot> IfeoPriorityAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<rec::Snapshot>;
    const auto exe = resolve(op);
    if (!exe) return R::fail(ErrorCode::Denied, "ifeo priority target not allowlisted");
    auto r = read_value(*exe);
    if (!r.ok()) return R(r.error());
    rec::Snapshot s;
    s.existed = r.value().has_value();
    if (s.existed) {
        s.raw_type = std::to_string(r.value()->type);
        s.raw = hex(r.value()->bytes);
        s.old_value = render(*r.value());
    } else {
        // Anahtarlar bizden once yoksa geri alma yalniz onlari kaldirir; okunamazsa kaldirmak yerine birakilir.
        const auto perf = hklm_->key_exists(ifeo_perf_subkey(*exe));
        if (perf.ok() && !perf.value()) {
            const auto top = hklm_->key_exists(ifeo_exe_subkey(*exe));
            s.source_version = std::string(top.ok() && !top.value() ? kExeNew : kPerfNew);
        }
    }
    return s;
}

Status IfeoPriorityAdapter::apply(const contracts::SettingOperation& op) {
    const auto exe = resolve(op);
    if (!exe) return Status::fail(ErrorCode::Denied, "ifeo priority target not allowlisted");
    if (op.new_value != kIfeoPriorityValue) return Status::fail(ErrorCode::OutOfRange, "ifeo priority value not allowed");
    if (!hklm_) return Status::fail(ErrorCode::Unsupported, "machine registry unavailable");
    return hklm_->write(ifeo_perf_subkey(*exe), kIfeoValueName, dword_raw(kIfeoCpuPriorityAboveNormal));
}

contracts::Result<std::optional<std::string>> IfeoPriorityAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<std::optional<std::string>>;
    const auto exe = resolve(op);
    if (!exe) return R::fail(ErrorCode::Denied, "ifeo priority target not allowlisted");
    auto r = read_value(*exe);
    if (!r.ok()) return R(r.error());
    if (!r.value()) return std::optional<std::string>{};
    return std::optional<std::string>{render(*r.value())};
}

Status IfeoPriorityAdapter::restore(const contracts::SettingOperation& op, const rec::Snapshot& snap) {
    const auto exe = resolve(op);
    if (!exe) return Status::fail(ErrorCode::Denied, "ifeo priority target not allowlisted");
    if (!hklm_) return Status::fail(ErrorCode::Unsupported, "machine registry unavailable");
    const auto perf = ifeo_perf_subkey(*exe);
    if (snap.existed) {
        const auto b = unhex(snap.raw);
        if (!b || snap.raw_type.empty() || snap.raw_type.size() > 3 ||
            snap.raw_type.find_first_not_of("0123456789") != std::string::npos)
            return Status::fail(ErrorCode::ParseError, "snapshot raw");
        return hklm_->write(perf, kIfeoValueName, RegRaw{static_cast<std::uint32_t>(std::stoul(snap.raw_type)), *b});
    }
    if (auto s = hklm_->remove(perf, kIfeoValueName); !s.ok()) return s;
    // Yalniz BIZIM actigimiz bos anahtarlar: once PerfOptions, sonra (hic yoksa) <exe>. Baska deger/alt anahtar varsa dokunulmaz.
    if (snap.source_version == kExeNew || snap.source_version == kPerfNew) {
        if (auto s = hklm_->remove_key_if_empty(perf); !s.ok()) return s;
        if (snap.source_version == kExeNew) return hklm_->remove_key_if_empty(ifeo_exe_subkey(*exe));
    }
    return contracts::ok_status();
}

bool ifeo_priority_admin_allowed(const contracts::SettingOperation& op) noexcept {
    try {
        return resolve(op).has_value() && op.new_value == kIfeoPriorityValue;
    } catch (...) {
        return false;
    }
}

}  // namespace ef::core::adapters::windows
