#include "platform/windows/win_env.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace ef::platform {

std::string to_utf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring to_wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

namespace {
std::optional<std::wstring> known_folder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p))) {
        if (p) CoTaskMemFree(p);
        return std::nullopt;
    }
    std::wstring s(p);
    CoTaskMemFree(p);
    return s;
}
std::wstring lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

std::wstring env_value(const wchar_t* name) {
    const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring v(n, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, v.data(), n);
    if (got == 0 || got >= n) return {};
    v.resize(got);
    return v;
}

// Tek karar (surec basina). GetTempPathW basarisizsa temp_root bos gecer -> test kipinde Blocked (kapali kalir).
const DataDirDecision& decision_once() {
    static const DataDirDecision dd = [] {
        const std::wstring v = env_value(L"EF_DATA_DIR");
        const bool bg = env_value(L"EF_TEST_BACKGROUND") == L"1";
        std::filesystem::path root;
        if (!v.empty() && bg) {
            wchar_t tmp[MAX_PATH + 2]{};
            const DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
            if (n != 0 && n <= MAX_PATH + 1) root = std::filesystem::path(std::wstring(tmp, n));
        }
        auto r = decide_data_dir(v, bg, root);
        if (r.source == DataDirSource::Blocked)
            OutputDebugStringW(L"EcuFps: EF_DATA_DIR rejected in test mode; persistence disabled (real data dir NOT used)\n");
        return r;
    }();
    return dd;
}
}  // namespace

DataDirDecision decide_data_dir(std::wstring_view value, bool test_background, const std::filesystem::path& temp_root) {
    if (value.empty() || !test_background) return {DataDirSource::Default, std::nullopt};
    if (auto r = data_dir_override(value, test_background, temp_root)) return {DataDirSource::Override, std::move(r)};
    return {DataDirSource::Blocked, std::nullopt};
}

std::optional<std::filesystem::path> data_dir_override(std::wstring_view value, bool test_background,
                                                       const std::filesystem::path& temp_root) {
    if (!test_background || value.empty() || temp_root.empty()) return std::nullopt;
    // Surucu harfli mutlak yol: "X:\..." (UNC "\\srv\...", "\\?\..." ve goreli yollar reddedilir).
    if (value.size() < 3 || value[1] != L':' || (value[2] != L'\\' && value[2] != L'/')) return std::nullopt;
    const wchar_t d = value[0];
    if (!((d >= L'A' && d <= L'Z') || (d >= L'a' && d <= L'z'))) return std::nullopt;
    const std::filesystem::path p(value);
    for (const auto& part : p)
        if (part == L"..") return std::nullopt;
    const std::filesystem::path norm = p.lexically_normal();
    const std::filesystem::path root = temp_root.lexically_normal();
    // Karsilastirma buyuk/kucuk harf duyarsiz (NTFS); sondaki ayirici yok sayilir.
    std::wstring a = lower(norm.wstring()), b = lower(root.wstring());
    while (!b.empty() && (b.back() == L'\\' || b.back() == L'/')) b.pop_back();
    while (!a.empty() && (a.back() == L'\\' || a.back() == L'/')) a.pop_back();
    for (auto* s : {&a, &b})
        for (auto& c : *s)
            if (c == L'/') c = L'\\';
    if (b.empty() || a.size() <= b.size() + 1 || a.compare(0, b.size(), b) != 0 || a[b.size()] != L'\\') return std::nullopt;
    return norm.has_filename() ? norm : norm.parent_path();
}

bool app_data_dir_overridden() { return decision_once().source == DataDirSource::Override; }

std::optional<std::filesystem::path> app_data_dir() {
    const auto& dd = decision_once();
    if (dd.source == DataDirSource::Override) return dd.path;
    if (dd.source == DataDirSource::Blocked) return std::nullopt;  // test kipi: gercek klasore ASLA dusulmez
    auto base = known_folder(FOLDERID_LocalAppData);
    if (!base) return std::nullopt;
    return std::filesystem::path(*base) / L"EcuFps";
}

core::logging::RedactionContext current_redaction_context() {
    core::logging::RedactionContext ctx;
    wchar_t buf[256];
    DWORD n = 256;
    if (GetUserNameW(buf, &n) && n > 1) ctx.user_name = to_utf8(std::wstring_view(buf, n - 1));
    n = 256;
    if (GetComputerNameExW(ComputerNameDnsHostname, buf, &n)) ctx.machine_name = to_utf8(std::wstring_view(buf, n));
    if (auto prof = known_folder(FOLDERID_Profile)) ctx.profile_dir = to_utf8(*prof);
    return ctx;
}

}  // namespace ef::platform
