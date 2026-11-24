#include "platform/windows/elevation.h"

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <cstdint>
#include <cwctype>
#include <vector>

#include "platform/windows/run_key_store.h"
#include "platform/windows/win_env.h"

namespace ef::platform {

bool is_process_elevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION el{};
    DWORD len = 0;
    const bool ok = GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &len) && el.TokenIsElevated;
    CloseHandle(tok);
    return ok;
}

bool self_admin_member() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    DWORD n = 0;
    (void)GetTokenInformation(tok, TokenGroups, nullptr, 0, &n);
    std::vector<std::uint8_t> buf(n > 0 && n <= 64 * 1024 ? n : 0);
    const bool got = !buf.empty() && GetTokenInformation(tok, TokenGroups, buf.data(), n, &n) != 0;
    CloseHandle(tok);
    if (!got) return false;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID admins = nullptr;
    if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins)) return false;
    const auto* g = reinterpret_cast<const TOKEN_GROUPS*>(buf.data());
    bool found = false;
    for (DWORD i = 0; i < g->GroupCount && !found; ++i) found = EqualSid(g->Groups[i].Sid, admins) != 0;
    FreeSid(admins);
    return found;
}

std::filesystem::path self_exe_path() {
    std::wstring buf(MAX_PATH, L'\0');
    for (int i = 0; i < 6; ++i) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
    return {};
}

SelfImageLock::~SelfImageLock() {
    if (h_) CloseHandle(static_cast<HANDLE>(h_));
}

SelfImageLock SelfImageLock::acquire(const std::filesystem::path& exe) {
    SelfImageLock l;
    const auto p = exe.empty() ? self_exe_path() : exe;
    if (p.empty()) {
        l.err_ = ERROR_FILE_NOT_FOUND;
        return l;
    }
    // Paylasim yalniz FILE_SHARE_READ: yukleyicinin goruntu tutamaci (okuma/calistirma) ile uyumlu; yazma ve DELETE
    // (yeniden adlandirma / silme) isteyen her yeni acilis ERROR_SHARING_VIOLATION alir.
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        l.err_ = GetLastError();
        return l;
    }
    l.h_ = h;
    return l;
}

std::string random_nonce() {
    unsigned char b[16];
    if (BCryptGenRandom(nullptr, b, sizeof b, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return {};
    static constexpr char hex[] = "0123456789abcdef";
    std::string s;
    for (unsigned char c : b) {
        s += hex[c >> 4];
        s += hex[c & 0xF];
    }
    return s;
}

namespace {
std::wstring process_image(HANDLE h) {
    std::wstring buf(32768, L'\0');
    DWORD n = static_cast<DWORD>(buf.size());
    if (!QueryFullProcessImageNameW(h, 0, buf.data(), &n)) return {};
    buf.resize(n);
    return buf;
}
bool same_path(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
}  // namespace

namespace {
std::optional<std::uint64_t> filetime_create_time(HANDLE h) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(h, &creation, &exit, &kernel, &user)) return std::nullopt;
    ULARGE_INTEGER v;
    v.LowPart = creation.dwLowDateTime;
    v.HighPart = creation.dwHighDateTime;
    if (v.QuadPart == 0) return std::nullopt;
    return v.QuadPart;
}
}  // namespace

std::optional<ParentIdentity> verify_parent_same_exe(std::uint32_t pid) {
    if (pid == 0) return std::nullopt;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!h) return std::nullopt;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    const std::wstring img = process_image(h);
    const auto created = filetime_create_time(h);
    CloseHandle(h);
    const std::wstring self = self_exe_path().wstring();
    if (!alive || !created || img.empty() || self.empty() || !same_path(img, self)) return std::nullopt;

    DWORD session = 0;
    if (!ProcessIdToSessionId(pid, &session)) return std::nullopt;
    ParentIdentity id;
    id.pid = pid;
    id.create_time = *created;
    id.session = static_cast<std::uint32_t>(session);
    id.image = img;
    return id;
}

bool parent_is_same_exe(std::uint32_t pid) { return verify_parent_same_exe(pid).has_value(); }

bool wait_parent_exit(std::uint32_t pid, std::uint32_t timeout_ms) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return true;  // zaten yok
    const DWORD r = WaitForSingleObject(h, timeout_ms == 0 ? INFINITE : timeout_ms);
    CloseHandle(h);
    return r == WAIT_OBJECT_0;
}

core::ElevationOutcome ShellElevationLauncher::launch_elevated(const core::LaunchArgs& args, std::string& detail) {
    if (process_) {
        CloseHandle(process_);
        process_ = nullptr;
    }
    const std::wstring exe = self_exe_path().wstring();
    const std::wstring params = core::build_launch_args(args);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    const std::string role(core::to_string(args.role));
    if (ShellExecuteExW(&sei)) {
        process_ = sei.hProcess;
        detail.clear();
        // Izlenebilirlik: yukseltilmis yardimci rol baslatildi (rol + cocuk PID; yol/kullanici yok). Yazilamazsa atlanir.
        append_exit_log("elevated role launched role=" + role +
                        " child_pid=" + (process_ ? std::to_string(GetProcessId(process_)) : std::string("unknown")) + " result=ok");
        return core::ElevationOutcome::Started;
    }
    const DWORD e = GetLastError();
    append_exit_log("elevated role launch failed role=" + role + " result=" + std::to_string(e) +
                    (e == ERROR_CANCELLED ? " (denied)" : ""));
    detail = "ShellExecuteEx runas failed (" + std::to_string(e) + ")";
    return e == ERROR_CANCELLED ? core::ElevationOutcome::Denied : core::ElevationOutcome::Failed;
}

ShellElevationLauncher::~ShellElevationLauncher() {
    if (process_) CloseHandle(process_);
}

}  // namespace ef::platform
