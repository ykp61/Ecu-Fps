#include "core/adapters/windows/appcompat_layers.h"

#include <algorithm>
#include <cctype>
#include <cwctype>

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Status;
namespace rec = core::recovery;

namespace {
constexpr std::string_view kAbsent = "<absent>";

bool utf8_to_wide(std::string_view s, std::wstring& out) {
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(static_cast<wchar_t>(c));
            ++i;
            continue;
        }
        int extra = 0;
        char32_t cp = 0;
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            cp = c & 0x07;
        } else {
            return false;
        }
        if (i + static_cast<std::size_t>(extra) >= s.size()) return false;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char x = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            if ((x & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (x & 0x3F);
        }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000)) return false;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
        i += static_cast<std::size_t>(extra) + 1;
    }
    return true;
}

bool ends_with_exe(std::wstring_view p) {
    if (p.size() < 4) return false;
    std::wstring tail(p.substr(p.size() - 4));
    for (auto& ch : tail) ch = static_cast<wchar_t>(std::towlower(ch));
    return tail == L".exe";
}

// Mutlak surucu yolu + guvenli bilesenler. Yol genis dizeye cozulmus olarak gelir.
bool valid_absolute_exe(std::wstring_view p) {
    if (p.size() < 5 || p.size() > 1024) return false;  // "C:\a.ex" en az
    if (!std::iswalpha(static_cast<wint_t>(p[0])) || p[1] != L':' || p[2] != L'\\') return false;
    if (p[3] == L'\\') return false;  // "C:\\..." degil
    if (!ends_with_exe(p)) return false;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const wchar_t c = p[i];
        if (c < 0x20) return false;
        if (c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|') return false;
        if (c == L':' && i != 1) return false;  // ikinci ':' (surucu disinda) yok
    }
    std::size_t start = 3;
    while (start <= p.size()) {
        const std::size_t e = p.find(L'\\', start);
        const std::wstring_view comp = p.substr(start, e == std::wstring_view::npos ? std::wstring_view::npos : e - start);
        if (comp.empty() || comp == L"." || comp == L"..") return false;
        if (comp.back() == L'.' || comp.back() == L' ') return false;
        if (e == std::wstring_view::npos) break;
        start = e + 1;
    }
    return true;
}

std::string ascii_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string canonical_path(std::string_view utf8_path) {
    std::string out;
    out.reserve(utf8_path.size());
    for (const char c : utf8_path) out.push_back(c == '/' ? '\\' : c);
    return ascii_lower(std::move(out));
}

std::string hex_bytes(const std::vector<std::uint8_t>& b) {
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (const auto x : b) s.push_back(d[x >> 4]), s.push_back(d[x & 15]);
    return s;
}

// Hedef kimligi guvenli karakter kumesinden olusmali (is_safe_target: yol/shell karakteri yok).
// Bu yuzden EXE yolu hedefte hex olarak tasinir; cozumleme uzunluk + karakter dogrular.
std::string hex_encode(std::string_view b) {
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (const unsigned char x : b) s.push_back(d[x >> 4]), s.push_back(d[x & 15]);
    return s;
}

std::optional<std::string> hex_decode(std::string_view h) {
    // valid_absolute_exe en fazla 1024 genis karaktere izin verir; UTF-8'de bu en fazla 4096 bayt,
    // yani 8192 hex karakterdir. Sinir uretici (appcompat_target_for_exe) ile tutarli olmali.
    if (h.empty() || h.size() % 2 != 0 || h.size() > 8192) return std::nullopt;
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
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

struct Layer {
    bool exists = false;
    std::wstring value;
};

contracts::Result<Layer> read_layer(UserRegistryApi& api, const std::wstring& exe) {
    using R = contracts::Result<Layer>;
    auto r = api.read(kAppCompatLayersSubkey, exe);
    if (!r.ok()) return r.error();
    Layer out;
    if (!r.value()) return out;
    const auto s = raw_sz(*r.value());
    if (!s) return R::fail(ErrorCode::ParseError, "layers value is not a string");
    out.exists = true;
    out.value = *s;
    return out;
}

std::vector<std::wstring> tokens(std::wstring_view s) {
    std::vector<std::wstring> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::iswspace(static_cast<wint_t>(s[i]))) ++i;
        const std::size_t start = i;
        while (i < s.size() && !std::iswspace(static_cast<wint_t>(s[i]))) ++i;
        if (i > start) out.emplace_back(s.substr(start, i - start));
    }
    return out;
}

bool token_eq(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::towlower(static_cast<wint_t>(a[i])) != std::towlower(static_cast<wint_t>(b[i]))) return false;
    return true;
}

}  // namespace

std::optional<std::wstring> exe_from_prefixed_target(std::string_view prefix, std::string_view target) {
    if (target.size() <= prefix.size() || target.substr(0, prefix.size()) != prefix) return std::nullopt;
    const auto bytes = hex_decode(target.substr(prefix.size()));
    if (!bytes) return std::nullopt;
    std::wstring w;
    if (!utf8_to_wide(*bytes, w)) return std::nullopt;
    for (auto& c : w)
        if (c == L'/') c = L'\\';
    if (!valid_absolute_exe(w)) return std::nullopt;
    return w;
}

std::optional<std::string> canonical_prefixed_target(std::string_view prefix, std::string_view target) {
    if (target.size() <= prefix.size() || target.substr(0, prefix.size()) != prefix) return std::nullopt;
    const auto bytes = hex_decode(target.substr(prefix.size()));
    if (!bytes || !exe_from_prefixed_target(prefix, target)) return std::nullopt;
    return std::string(prefix) + hex_encode(canonical_path(*bytes));
}

std::string prefixed_target_for_exe(std::string_view prefix, std::string_view exe_utf8) {
    std::wstring w;
    if (!utf8_to_wide(exe_utf8, w)) return {};
    for (auto& c : w)
        if (c == L'/') c = L'\\';
    if (!valid_absolute_exe(w)) return {};
    return std::string(prefix) + hex_encode(canonical_path(exe_utf8));
}

std::vector<std::string> canonicalize_write_roots(const std::vector<std::string>& roots) {
    std::vector<std::string> out;
    for (const auto& r : roots) {
        std::string c = canonical_path(r);
        while (!c.empty() && c.back() == '\\') c.pop_back();
        if (c.size() < 3 || c[1] != ':' || c[2] != '\\') continue;  // mutlak surucu yolu degil
        if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(std::move(c));
    }
    return out;
}

bool canonical_target_under_roots(std::string_view prefix, std::string_view canonical_target, const std::vector<std::string>& roots) {
    if (canonical_target.size() <= prefix.size()) return false;
    const auto exe = hex_decode(canonical_target.substr(prefix.size()));  // kanonik: kucuk harf, "\"
    if (!exe) return false;
    for (const auto& root : roots)
        if (exe->size() > root.size() + 1 && exe->compare(0, root.size(), root) == 0 && (*exe)[root.size()] == '\\') return true;
    return false;
}

std::optional<std::wstring> appcompat_exe_from_target(std::string_view target) { return exe_from_prefixed_target(kAppCompatPrefix, target); }

std::optional<std::string> appcompat_canonical_target(std::string_view target) { return canonical_prefixed_target(kAppCompatPrefix, target); }

std::string appcompat_target_for_exe(std::string_view exe_utf8) { return prefixed_target_for_exe(kAppCompatPrefix, exe_utf8); }

bool layer_has_token(std::wstring_view layers, std::wstring_view token) {
    for (const auto& t : tokens(layers))
        if (token_eq(t, token)) return true;
    return false;
}

std::wstring layer_add_token(std::wstring_view layers, std::wstring_view token) {
    std::wstring cur;
    std::size_t b = 0, e = layers.size();
    while (b < e && std::iswspace(static_cast<wint_t>(layers[b]))) ++b;
    while (e > b && std::iswspace(static_cast<wint_t>(layers[e - 1]))) --e;
    cur.assign(layers.substr(b, e - b));
    if (layer_has_token(cur, token)) return cur;
    if (cur.empty()) return L"~ " + std::wstring(token);
    if (cur.front() != L'~') return L"~ " + cur + L" " + std::wstring(token);
    return cur + L" " + std::wstring(token);
}

std::wstring layer_remove_token(std::wstring_view layers, std::wstring_view token) {
    std::wstring out;
    bool any = false;
    for (const auto& t : tokens(layers)) {
        if (token_eq(t, token)) continue;
        if (any) out.push_back(L' ');
        out.append(t);
        any = true;
    }
    if (out == L"~") out.clear();  // yalniz kullanici isaretcisi kaldi: deger bosa dondu
    return out;
}

void AppCompatLayerAdapter::set_allowed_roots(std::vector<std::string> roots) {
    auto c = canonicalize_write_roots(roots);
    std::lock_guard lk(roots_mu_);
    roots_ = std::move(c);
}

bool AppCompatLayerAdapter::under_allowed_root(std::string_view canonical_target) const {
    std::lock_guard lk(roots_mu_);
    return canonical_target_under_roots(kAppCompatPrefix, canonical_target, roots_);
}

contracts::Result<std::optional<bool>> AppCompatLayerAdapter::token_state(std::string_view target) {
    using R = contracts::Result<std::optional<bool>>;
    const auto exe = appcompat_exe_from_target(target);
    if (!exe) return R::fail(ErrorCode::Denied, "invalid appcompat target");
    auto r = read_layer(api_, *exe);
    if (!r.ok()) return r.error();
    if (!r.value().exists) return std::optional<bool>{};
    return std::optional<bool>{layer_has_token(r.value().value, kDisableFsOptToken)};
}

contracts::Result<rec::Snapshot> AppCompatLayerAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<rec::Snapshot>;
    const auto exe = appcompat_exe_from_target(op.target);
    if (!exe) return R::fail(ErrorCode::Denied, "invalid appcompat target");
    auto r = api_.read(kAppCompatLayersSubkey, *exe);
    if (!r.ok()) return r.error();
    rec::Snapshot s;
    s.existed = r.value().has_value();
    if (s.existed) {
        const auto str = raw_sz(*r.value());
        if (!str) return R::fail(ErrorCode::ParseError, "layers value is not a string");
        s.raw_type = std::to_string(r.value()->type);
        s.raw = hex_bytes(r.value()->bytes);
        s.old_value = layer_has_token(*str, kDisableFsOptToken) ? "disabled" : "enabled";
    } else {
        s.old_value = "enabled";  // deger yok = jeton yok
    }
    return s;
}

Status AppCompatLayerAdapter::apply(const contracts::SettingOperation& op) {
    const auto canonical = appcompat_canonical_target(op.target);
    if (!canonical) return Status::fail(ErrorCode::Denied, "invalid appcompat target");
    if (!under_allowed_root(*canonical)) return Status::fail(ErrorCode::Denied, "exe not under an installed game folder");
    if (op.new_value != "disabled") return Status::fail(ErrorCode::OutOfRange, "value must be \"disabled\"");
    const auto exe = appcompat_exe_from_target(op.target);
    if (!exe) return Status::fail(ErrorCode::Denied, "invalid appcompat target");
    auto r = read_layer(api_, *exe);
    if (!r.ok()) return Status(r.error());
    const std::wstring next = layer_add_token(r.value().value, kDisableFsOptToken);
    if (r.value().exists && next == r.value().value) return contracts::ok_status();  // zaten bizim degerimiz
    return api_.write(kAppCompatLayersSubkey, *exe, sz_raw(next));
}

contracts::Result<std::optional<std::string>> AppCompatLayerAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<std::optional<std::string>>;
    const auto exe = appcompat_exe_from_target(op.target);
    if (!exe) return R::fail(ErrorCode::Denied, "invalid appcompat target");
    auto r = read_layer(api_, *exe);
    if (!r.ok()) return r.error();
    if (!r.value().exists) return std::optional<std::string>{};
    return std::optional<std::string>{layer_has_token(r.value().value, kDisableFsOptToken) ? "disabled" : "enabled"};
}

Status AppCompatLayerAdapter::restore(const contracts::SettingOperation& op, const rec::Snapshot& snap) {
    const auto exe = appcompat_exe_from_target(op.target);
    if (!exe) return Status::fail(ErrorCode::Denied, "invalid appcompat target");
    auto r = read_layer(api_, *exe);
    if (!r.ok()) return Status(r.error());
    // Jeton onceden VARSA geri ekle (kullanicinin sonradan ekledigi katmanlar korunur); YOKSA yalniz
    // kendi jetonumuzu kaldir. Deger onceden yoksa ve baska katman da kalmadiysa degeri sil.
    const bool want_token = snap.existed && snap.old_value == "disabled";
    const std::wstring next =
        want_token ? layer_add_token(r.value().value, kDisableFsOptToken) : layer_remove_token(r.value().value, kDisableFsOptToken);
    if (!snap.existed) {
        if (next.empty()) return api_.remove(kAppCompatLayersSubkey, *exe);  // deger onceden yoktu
        if (r.value().exists && r.value().value == next) return contracts::ok_status();
        return api_.write(kAppCompatLayersSubkey, *exe, sz_raw(next));
    }
    if (r.value().exists && r.value().value == next) return contracts::ok_status();
    return api_.write(kAppCompatLayersSubkey, *exe, sz_raw(next));
}

}  // namespace ef::core::adapters::windows
