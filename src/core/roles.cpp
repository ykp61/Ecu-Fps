#include "core/roles.h"

#include <vector>

namespace ef::core {

using contracts::ErrorCode;

std::string_view to_string(Role r) noexcept {
    switch (r) {
        case Role::Ui: return "ui";
        case Role::Smoke: return "smoke";
        case Role::Broker: return "broker";
        case Role::Recovery: return "recovery";
        case Role::BenchWorker: return "bench";
        case Role::CaptureWorker: return "capture";
        case Role::CleanupWorker: return "cleanup";
        case Role::Setup: return "setup";
        case Role::SetupWorker: return "setup-worker";
        case Role::Service: return "service";
        case Role::Uninstall: return "uninstall";
    }
    return "?";
}

Role effective_role(const LaunchArgs& a, bool running_from_install_dir) noexcept {
    if (a.role == Role::Ui && a.bare && !running_from_install_dir) return Role::Setup;
    return a.role;
}

std::string_view to_string(ElevationOutcome o) noexcept {
    switch (o) {
        case ElevationOutcome::Started: return "started";
        case ElevationOutcome::Denied: return "denied";
        case ElevationOutcome::Failed: return "failed";
    }
    return "?";
}

bool is_valid_nonce(std::string_view s) noexcept {
    if (s.size() != 32) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

namespace {
std::vector<std::wstring_view> split(std::wstring_view s) {
    std::vector<std::wstring_view> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) ++i;
        std::size_t j = i;
        while (j < s.size() && s[j] != L' ' && s[j] != L'\t') ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

std::optional<std::string> ascii(std::wstring_view w) {
    std::string s;
    for (wchar_t c : w) {
        if (c < 0x21 || c > 0x7E) return std::nullopt;
        s += static_cast<char>(c);
    }
    return s;
}
}  // namespace

contracts::Result<LaunchArgs> parse_launch_args(std::wstring_view cmd) {
    using R = contracts::Result<LaunchArgs>;
    if (cmd.size() > 256) return R::fail(ErrorCode::ParseLimit, "command line too long");
    const auto tokens = split(cmd);
    LaunchArgs a;
    if (tokens.empty()) { a.bare = true; return a; }
    if (tokens.size() == 1 && tokens[0] == L"--smoke") { a.role = Role::Smoke; return a; }
    if (tokens.size() == 1 && tokens[0] == L"--setup") { a.role = Role::Setup; return a; }
    if (tokens.size() == 1 && tokens[0] == L"--uninstall") { a.role = Role::Uninstall; return a; }
    if (tokens.size() == 1 && tokens[0] == L"--service") { a.role = Role::Service; return a; }
    if (tokens.size() == 1 && tokens[0] == L"--recovery") { a.role = Role::Recovery; return a; }
    if (tokens.size() == 1 && tokens[0] == L"--tray") { a.tray = true; return a; }
    if (constexpr std::wstring_view kUpdated = L"--updated-from="; tokens.size() == 1 && tokens[0].starts_with(kUpdated)) {
        const auto v = tokens[0].substr(kUpdated.size());
        if (v.empty() || v.size() > 32) return R::fail(ErrorCode::ParseError, "updated-from");
        for (wchar_t c : v)
            if (!((c >= L'0' && c <= L'9') || c == L'.')) return R::fail(ErrorCode::ParseError, "updated-from");
        for (wchar_t c : v) a.updated_from += static_cast<char>(c);  // yalniz ASCII rakam/nokta
        return a;
    }

    bool have_role = false, have_parent = false, have_nonce = false;
    for (const auto tw : tokens) {
        const auto t = ascii(tw);
        if (!t) return R::fail(ErrorCode::ParseError, "non-ascii argument");
        const auto eq = t->find('=');
        if (t->rfind("--", 0) != 0 || eq == std::string::npos) return R::fail(ErrorCode::ParseError, "bad argument");
        const std::string key = t->substr(2, eq - 2), val = t->substr(eq + 1);
        if (key == "role" && !have_role) {
            have_role = true;
            if (val == "broker") a.role = Role::Broker;
            else if (val == "bench") a.role = Role::BenchWorker;
            else if (val == "capture") a.role = Role::CaptureWorker;
            else if (val == "cleanup") a.role = Role::CleanupWorker;
            else if (val == "setup") a.role = Role::SetupWorker;
            else return R::fail(ErrorCode::InvalidEnum, "role");
        } else if (key == "parent" && !have_parent) {
            have_parent = true;
            if (val.empty() || val.size() > 10) return R::fail(ErrorCode::OutOfRange, "parent");
            std::uint64_t v = 0;
            for (char c : val) {
                if (c < '0' || c > '9') return R::fail(ErrorCode::ParseError, "parent");
                v = v * 10 + static_cast<std::uint64_t>(c - '0');
            }
            if (v == 0 || v > 0xFFFFFFFFull) return R::fail(ErrorCode::OutOfRange, "parent");
            a.parent_pid = static_cast<std::uint32_t>(v);
        } else if (key == "nonce" && !have_nonce) {
            have_nonce = true;
            if (!is_valid_nonce(val)) return R::fail(ErrorCode::ParseError, "nonce");
            a.nonce = val;
        } else {
            return R::fail(ErrorCode::ParseError, "unknown or repeated key " + key);
        }
    }
    if (!have_role || !have_parent || !have_nonce) return R::fail(ErrorCode::MissingValue, "role, parent and nonce required");
    return a;
}

std::wstring build_launch_args(const LaunchArgs& a) {
    switch (a.role) {
        case Role::Ui:
            if (!a.updated_from.empty()) return L"--updated-from=" + std::wstring(a.updated_from.begin(), a.updated_from.end());
            return a.tray ? L"--tray" : L"";
        case Role::Smoke: return L"--smoke";
        case Role::Recovery: return L"--recovery";
        case Role::Setup: return L"--setup";
        case Role::Uninstall: return L"--uninstall";
        case Role::Service: return L"--service";
        default: break;
    }
    std::wstring s = L"--role=";
    switch (a.role) {
        case Role::Broker: s += L"broker"; break;
        case Role::BenchWorker: s += L"bench"; break;
        case Role::CaptureWorker: s += L"capture"; break;
        case Role::CleanupWorker: s += L"cleanup"; break;
        case Role::SetupWorker: s += L"setup"; break;
        default: return s;  // Ui/Smoke/Recovery/Setup/Uninstall/Service yukarida ele alindi
    }
    s += L" --parent=" + std::to_wstring(a.parent_pid.value_or(0));
    s += L" --nonce=";
    for (char c : a.nonce) s += static_cast<wchar_t>(c);
    return s;
}

ElevatedRequestResult run_elevated_request(ElevationLauncher& launcher, std::uint32_t self_pid, const std::string& nonce,
                                           const std::function<bool()>& apply) {
    ElevatedRequestResult r;
    if (!is_valid_nonce(nonce)) {
        r.detail = "invalid nonce";
        return r;
    }
    LaunchArgs a{Role::Broker, self_pid, nonce};
    r.outcome = launcher.launch_elevated(a, r.detail);
    if (r.outcome != ElevationOutcome::Started) return r;  // UAC reddi/hata: hicbir sey yazilmaz
    r.applied = apply ? apply() : false;
    return r;
}

}  // namespace ef::core
