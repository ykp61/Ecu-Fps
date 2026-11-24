#include "core/adapters/windows/gpu_preference.h"

#include "core/adapters/windows/appcompat_layers.h"

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Status;
namespace rec = core::recovery;

namespace {
constexpr std::string_view kAbsent = "<absent>";

std::string hex_of(const std::vector<std::uint8_t>& b) {
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (const auto x : b) s.push_back(d[x >> 4]), s.push_back(d[x & 15]);
    return s;
}

std::optional<std::vector<std::uint8_t>> from_hex(std::string_view h) {
    if (h.size() % 2 != 0) return std::nullopt;
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> out;
    out.reserve(h.size() / 2);
    for (std::size_t i = 0; i < h.size(); i += 2) {
        const int hi = val(h[i]), lo = val(h[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

std::string narrow_ascii(std::wstring_view w) {
    std::string s;
    for (const wchar_t c : w) s.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    return s;
}

// Deger yok -> nullopt; var ama dize degil -> hata.
contracts::Result<std::optional<std::wstring>> read_value(UserRegistryApi& api, const std::wstring& exe) {
    using R = contracts::Result<std::optional<std::wstring>>;
    auto r = api.read(kGpuPrefSubkey, exe);
    if (!r.ok()) return r.error();
    if (!r.value()) return std::optional<std::wstring>{};
    const auto s = raw_sz(*r.value());
    if (!s) return R::fail(ErrorCode::ParseError, "gpu preference value is not a string");
    return std::optional<std::wstring>{*s};
}
}  // namespace

std::string gpu_pref_target_for_exe(std::string_view exe_utf8) { return prefixed_target_for_exe(kGpuPrefPrefix, exe_utf8); }

std::optional<std::wstring> gpu_pref_exe_from_target(std::string_view target) { return exe_from_prefixed_target(kGpuPrefPrefix, target); }

void GpuPreferenceAdapter::set_allowed_roots(std::vector<std::string> roots) {
    auto c = canonicalize_write_roots(roots);
    std::lock_guard lk(roots_mu_);
    roots_ = std::move(c);
}

bool GpuPreferenceAdapter::under_allowed_root(std::string_view canonical_target) const {
    std::lock_guard lk(roots_mu_);
    return canonical_target_under_roots(kGpuPrefPrefix, canonical_target, roots_);
}

bool GpuPreferenceAdapter::exe_writable(std::string_view exe_utf8) const {
    const auto target = gpu_pref_target_for_exe(exe_utf8);
    if (target.empty()) return false;
    const auto canonical = canonical_prefixed_target(kGpuPrefPrefix, target);
    return canonical && under_allowed_root(*canonical);
}

contracts::Result<rec::Snapshot> GpuPreferenceAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<rec::Snapshot>;
    const auto exe = gpu_pref_exe_from_target(op.target);
    if (!exe) return R::fail(ErrorCode::Denied, "invalid gpu preference target");
    auto r = api_.read(kGpuPrefSubkey, *exe);
    if (!r.ok()) return r.error();
    rec::Snapshot s;
    s.existed = r.value().has_value();
    if (s.existed) {
        s.raw_type = std::to_string(r.value()->type);
        s.raw = hex_of(r.value()->bytes);
        const auto str = raw_sz(*r.value());
        const auto v = str ? dx_flag_get(*str, kGpuPrefKeyName) : std::nullopt;
        s.old_value = v ? narrow_ascii(*v) : std::string(kAbsent);
    } else {
        s.old_value = std::string(kAbsent);
    }
    return s;
}

Status GpuPreferenceAdapter::apply(const contracts::SettingOperation& op) {
    const auto canonical = canonical_prefixed_target(kGpuPrefPrefix, op.target);
    if (!canonical) return Status::fail(ErrorCode::Denied, "invalid gpu preference target");
    if (!under_allowed_root(*canonical)) return Status::fail(ErrorCode::Denied, "exe not under an installed game folder");
    if (op.new_value != "2") return Status::fail(ErrorCode::OutOfRange, "value must be \"2\"");
    const auto exe = gpu_pref_exe_from_target(op.target);
    if (!exe) return Status::fail(ErrorCode::Denied, "invalid gpu preference target");
    auto r = read_value(api_, *exe);
    if (!r.ok()) return Status(r.error());
    const std::wstring cur = r.value().value_or(L"");
    const std::wstring next = dx_flag_set(cur, kGpuPrefKeyName, L"2");
    if (r.value() && next == cur) return contracts::ok_status();  // zaten bizim degerimiz
    return api_.write(kGpuPrefSubkey, *exe, sz_raw(next));
}

contracts::Result<std::optional<std::string>> GpuPreferenceAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<std::optional<std::string>>;
    const auto exe = gpu_pref_exe_from_target(op.target);
    if (!exe) return R::fail(ErrorCode::Denied, "invalid gpu preference target");
    auto r = read_value(api_, *exe);
    if (!r.ok()) return r.error();
    if (!r.value()) return std::optional<std::string>{};
    const auto v = dx_flag_get(*r.value(), kGpuPrefKeyName);
    if (!v) return std::optional<std::string>{};
    return std::optional<std::string>{narrow_ascii(*v)};
}

Status GpuPreferenceAdapter::restore(const contracts::SettingOperation& op, const rec::Snapshot& snap) {
    const auto exe = gpu_pref_exe_from_target(op.target);
    if (!exe) return Status::fail(ErrorCode::Denied, "invalid gpu preference target");
    if (!snap.existed) return api_.remove(kGpuPrefSubkey, *exe);  // deger onceden yoktu
    const auto bytes = from_hex(snap.raw);
    if (!bytes || snap.raw_type.empty()) return Status::fail(ErrorCode::ParseError, "snapshot raw");
    std::uint32_t type = 0;
    try {
        type = static_cast<std::uint32_t>(std::stoul(snap.raw_type));
    } catch (...) {
        return Status::fail(ErrorCode::ParseError, "snapshot raw type");
    }
    return api_.write(kGpuPrefSubkey, *exe, RegRaw{type, *bytes});
}

}  // namespace ef::core::adapters::windows
