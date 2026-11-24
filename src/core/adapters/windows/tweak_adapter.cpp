#include "core/adapters/windows/tweak_adapter.h"

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::OperationId;
using contracts::OperationScope;
using contracts::Status;
namespace rec = core::recovery;

namespace {
constexpr std::string_view kKeyNew = "key_new";

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
    auto n = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (std::size_t i = 0; i < s.size(); i += 2) {
        const int a = n(s[i]), c = n(s[i + 1]);
        if (a < 0 || c < 0) return std::nullopt;
        b.push_back(static_cast<std::uint8_t>(a * 16 + c));
    }
    return b;
}

// Okunan ham degerin metni; capture ve read_back ayni kurali kullanir (geri alma karsilastirmasi icin).
std::string render(const TweakEntry& e, const RegRaw& r) {
    if (r.type == e.reg_type) {
        if (r.type == kTweakRegDword) {
            if (const auto d = raw_dword(r)) return std::to_string(*d);
        } else if (r.type == kTweakRegSz) {
            if (const auto s = raw_sz(r)) {
                std::string o;
                bool ascii = true;
                for (wchar_t c : *s) {
                    if (c >= 0x80) ascii = false;
                    o.push_back(static_cast<char>(c));
                }
                if (ascii) return o;
            }
        }
    }
    return "raw:" + std::to_string(r.type) + ":" + hex(r.bytes);
}

// Islemi katalogdan cozer: yalniz TweakValueSet, kayitli id ve girdinin hive'una uyan kapsam.
const TweakEntry* resolve(const contracts::SettingOperation& op) {
    if (op.id != OperationId::TweakValueSet) return nullptr;
    const auto* e = find_tweak_target(op.target);
    if (!e) return nullptr;
    const auto want = e->hive == TweakHive::Machine ? OperationScope::Machine : OperationScope::CurrentUser;
    return op.scope == want ? e : nullptr;
}
}  // namespace

std::string tweak_target(const TweakEntry& e) { return std::string(kTweakTargetPrefix) + std::string(e.id); }

contracts::Result<std::optional<RegRaw>> TweakAdapter::read_raw(const TweakEntry& e) {
    using R = contracts::Result<std::optional<RegRaw>>;
    if (e.hive == TweakHive::User) return hkcu_.read(e.subkey, e.name);
    if (!hklm_) return R::fail(ErrorCode::Unsupported, "machine registry unavailable");
    return hklm_->read(e.subkey, e.name);
}

contracts::Result<rec::Snapshot> TweakAdapter::capture_entry(const TweakEntry& e) {
    using R = contracts::Result<rec::Snapshot>;
    auto r = read_raw(e);
    if (!r.ok()) return R(r.error());
    rec::Snapshot s;
    s.existed = r.value().has_value();
    if (s.existed) {
        s.raw_type = std::to_string(r.value()->type);
        s.raw = hex(r.value()->bytes);
        s.old_value = render(e, *r.value());
    } else if (e.hive == TweakHive::Machine && hklm_) {
        // Anahtar bizden once yoksa geri alma bos anahtari da kaldirir; okunamazsa kaldirmak yerine birakilir.
        const auto k = hklm_->key_exists(e.subkey);
        if (k.ok() && !k.value()) s.source_version = std::string(kKeyNew);
    }
    return s;
}

Status TweakAdapter::apply_entry(const TweakEntry& e) {
    const auto raw = tweak_on_raw(e, runtime());
    if (!raw) return Status::fail(ErrorCode::ParseError, "tweak catalog value");
    if (e.hive == TweakHive::User) return hkcu_.write(e.subkey, e.name, *raw);
    if (!hklm_) return Status::fail(ErrorCode::Unsupported, "machine registry unavailable");
    return hklm_->write(e.subkey, e.name, *raw);
}

contracts::Result<std::optional<std::string>> TweakAdapter::read_back_entry(const TweakEntry& e) {
    using R = contracts::Result<std::optional<std::string>>;
    auto r = read_raw(e);
    if (!r.ok()) return R(r.error());
    if (!r.value()) return std::optional<std::string>{};
    return std::optional<std::string>{render(e, *r.value())};
}

Status TweakAdapter::restore_entry(const TweakEntry& e, const rec::Snapshot& snap) {
    if (snap.existed) {
        const auto b = unhex(snap.raw);
        if (!b || snap.raw_type.empty() || snap.raw_type.size() > 3 ||
            snap.raw_type.find_first_not_of("0123456789") != std::string::npos)
            return Status::fail(ErrorCode::ParseError, "snapshot raw");
        const RegRaw v{static_cast<std::uint32_t>(std::stoul(snap.raw_type)), *b};
        if (e.hive == TweakHive::User) return hkcu_.write(e.subkey, e.name, v);
        if (!hklm_) return Status::fail(ErrorCode::Unsupported, "machine registry unavailable");
        return hklm_->write(e.subkey, e.name, v);
    }
    if (e.hive == TweakHive::User) return hkcu_.remove(e.subkey, e.name);
    if (!hklm_) return Status::fail(ErrorCode::Unsupported, "machine registry unavailable");
    if (auto s = hklm_->remove(e.subkey, e.name); !s.ok()) return s;
    if (snap.source_version == kKeyNew) return hklm_->remove_key_if_empty(e.subkey);  // yalniz bizim actigimiz anahtar
    return contracts::ok_status();
}

Status TweakAdapter::apply_value(const TweakEntry& e, std::string_view new_value) {
    if (!tweak_item_enabled(e)) return Status::fail(ErrorCode::Denied, "tweak item not enabled");  // apply-only kapi
    const auto rt = runtime();
    const auto want = tweak_on_text(e, rt);
    if (!want) return Status::fail(ErrorCode::Unsupported, "tweak target value unavailable");
    if (new_value != *want) return Status::fail(ErrorCode::OutOfRange, "tweak value not allowed");
    const auto raw = tweak_on_raw(e, rt);  // ayni runtime: karsilastirilan ve yazilan deger ayni
    if (!raw) return Status::fail(ErrorCode::ParseError, "tweak catalog value");
    if (e.hive == TweakHive::User) return hkcu_.write(e.subkey, e.name, *raw);
    if (!hklm_) return Status::fail(ErrorCode::Unsupported, "machine registry unavailable");
    return hklm_->write(e.subkey, e.name, *raw);
}

contracts::Result<rec::Snapshot> TweakAdapter::capture(const contracts::SettingOperation& op) {
    const auto* e = resolve(op);
    if (!e) return contracts::Result<rec::Snapshot>::fail(ErrorCode::Denied, "tweak target not allowlisted");
    // capture yalniz yeni uygulamanin ilk adimidir (motor; geri alma capture cagirmaz): kapali bayrakli girdide burada durulur,
    // boylece journal ve kayit yazilmaz.
    if (!tweak_item_enabled(*e)) return contracts::Result<rec::Snapshot>::fail(ErrorCode::Denied, "tweak item not enabled");
    return capture_entry(*e);
}

Status TweakAdapter::apply(const contracts::SettingOperation& op) {
    const auto* e = resolve(op);
    if (!e) return Status::fail(ErrorCode::Denied, "tweak target not allowlisted");
    return apply_value(*e, op.new_value);
}

contracts::Result<std::optional<std::string>> TweakAdapter::read_back(const contracts::SettingOperation& op) {
    const auto* e = resolve(op);
    if (!e) return contracts::Result<std::optional<std::string>>::fail(ErrorCode::Denied, "tweak target not allowlisted");
    return read_back_entry(*e);
}

Status TweakAdapter::restore(const contracts::SettingOperation& op, const rec::Snapshot& snap) {
    const auto* e = resolve(op);
    if (!e) return Status::fail(ErrorCode::Denied, "tweak target not allowlisted");
    return restore_entry(*e, snap);
}

// ---- FakeMachineRegistry ----
contracts::Result<std::optional<RegRaw>> FakeMachineRegistry::read(std::wstring_view subkey, std::wstring_view name) {
    using R = contracts::Result<std::optional<RegRaw>>;
    if (fail_read) return R::fail(ErrorCode::IoError, "fake read failure");
    const auto it = values.find(key(subkey, name));
    if (it == values.end()) return std::optional<RegRaw>{};
    return std::optional<RegRaw>{it->second};
}
Status FakeMachineRegistry::write(std::wstring_view subkey, std::wstring_view name, const RegRaw& v) {
    if (fail_write) return Status::fail(ErrorCode::Denied, "fake write failure");
    ++writes;
    keys.insert(std::wstring(subkey));
    values[key(subkey, name)] = v;
    return contracts::ok_status();
}
Status FakeMachineRegistry::remove(std::wstring_view subkey, std::wstring_view name) {
    if (fail_write) return Status::fail(ErrorCode::Denied, "fake write failure");
    ++removes;
    values.erase(key(subkey, name));
    return contracts::ok_status();
}
contracts::Result<bool> FakeMachineRegistry::key_exists(std::wstring_view subkey) { return keys.count(std::wstring(subkey)) != 0; }
Status FakeMachineRegistry::remove_key_if_empty(std::wstring_view subkey) {
    const std::wstring prefix = std::wstring(subkey) + L"|";
    for (const auto& kv : values)
        if (kv.first.rfind(prefix, 0) == 0) return contracts::ok_status();  // dolu: dokunma
    keys.erase(std::wstring(subkey));
    return contracts::ok_status();
}

}  // namespace ef::core::adapters::windows
