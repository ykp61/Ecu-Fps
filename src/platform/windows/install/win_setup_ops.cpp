#include "platform/windows/install/win_setup_ops.h"

#include <windows.h>

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#include "core/broker/admin.h"
#include "core/catalog/envelope.h"
#include "core/recovery/journal.h"
#include "core/recovery/sha256.h"
#include "core/security/trust.h"
#include "core/update/staged_update.h"
#include "platform/windows/install/secure_dir.h"
#include "platform/windows/journal_fs.h"
#include "platform/windows/service/failure_actions.h"
#include "platform/windows/signature.h"

namespace ef::platform::install {

using contracts::ErrorCode;
using contracts::Result;
using contracts::Status;
using core::setup::ArpValues;
using core::setup::PriorState;
using core::setup::ServiceConfig;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kMaxImageBytes = 256ull * 1024ull * 1024ull;
constexpr DWORD kServiceWaitMs = 30000;
constexpr REGSAM kWow64 = KEY_WOW64_64KEY;
constexpr wchar_t kServiceDescription[] =
    L"Applies the allow-listed, journaled Ecu Fps settings for the signed-in user without a UAC prompt.";
constexpr wchar_t kLocalSystem[] = L"LocalSystem";

Status fail(ErrorCode c, std::string d) { return Status::fail(c, std::move(d)); }
Status win_fail(const char* what, DWORD e) {
    return fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, std::string(what) + " (" + std::to_string(e) + ")");
}

bool file_exists(const fs::path& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool dir_exists(const fs::path& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool any_exists(const fs::path& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool delete_if_present(const fs::path& p) {
    if (DeleteFileW(p.c_str())) return true;
    const DWORD e = GetLastError();
    return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
}

bool iequal(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::towupper(static_cast<std::wint_t>(a[i])) != std::towupper(static_cast<std::wint_t>(b[i]))) return false;
    return true;
}

struct FileHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    FileHandle() = default;
    explicit FileHandle(HANDLE v) : h(v) {}
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    ~FileHandle() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    bool ok() const { return h != INVALID_HANDLE_VALUE; }
};

Result<std::string> read_whole(HANDLE h) {
    using R = Result<std::string>;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0) return R::fail(ErrorCode::IoError, "size");
    if (static_cast<std::uint64_t>(size.QuadPart) > kMaxImageBytes) return R::fail(ErrorCode::ParseLimit, "image too large");
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(h, zero, nullptr, FILE_BEGIN)) return R::fail(ErrorCode::IoError, "seek");
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t off = 0;
    while (off < bytes.size()) {
        const DWORD want = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - off, 1u << 20));
        DWORD got = 0;
        if (!ReadFile(h, bytes.data() + off, want, &got, nullptr) || got == 0) return R::fail(ErrorCode::IoError, "read");
        off += got;
    }
    return bytes;
}

Result<std::string> read_file(const fs::path& p) {
    FileHandle f(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!f.ok()) return Result<std::string>::fail(ErrorCode::IoError, "open " + std::to_string(GetLastError()));
    return read_whole(f.h);
}

core::catalog::SwapFs make_swap_fs() {
    core::catalog::SwapFs s;
    s.move = [](const std::string& from, const std::string& to) {
        return MoveFileExW(widen(from).c_str(), widen(to).c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;  // ayni birim, yerine yazma yok
    };
    s.exists = [](const std::string& p) { return any_exists(widen(p)); };
    s.remove = [](const std::string& p) { return delete_if_present(widen(p)); };
    return s;
}

// ---- SCM ----
struct Sc {
    SC_HANDLE h = nullptr;
    explicit Sc(SC_HANDLE v) : h(v) {}
    Sc(const Sc&) = delete;
    Sc& operator=(const Sc&) = delete;
    ~Sc() {
        if (h) CloseServiceHandle(h);
    }
};

struct ServiceSnapshot {
    bool exists = false, running = false;
    DWORD start_type = 0, type = 0;
    std::wstring account;
    ServiceConfig cfg;
};

Result<DWORD> current_state(SC_HANDLE svc) {
    SERVICE_STATUS_PROCESS sp{};
    DWORD need = 0;
    if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&sp), sizeof(sp), &need))
        return Result<DWORD>::fail(ErrorCode::IoError, "QueryServiceStatusEx " + std::to_string(GetLastError()));
    return sp.dwCurrentState;
}

bool wait_state(SC_HANDLE svc, DWORD want) {
    const ULONGLONG deadline = GetTickCount64() + kServiceWaitMs;
    for (;;) {
        auto s = current_state(svc);
        if (s && s.value() == want) return true;
        if (!s || GetTickCount64() >= deadline) return false;
        Sleep(250);
    }
}

Result<ServiceSnapshot> snapshot_service(std::wstring_view name = core::setup::kServiceName) {
    using R = Result<ServiceSnapshot>;
    ServiceSnapshot out;
    Sc scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm.h) return R::fail(ErrorCode::IoError, "OpenSCManagerW " + std::to_string(GetLastError()));
    Sc svc(OpenServiceW(scm.h, std::wstring(name).c_str(), SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS));
    if (!svc.h) {
        const DWORD e = GetLastError();
        if (e == ERROR_SERVICE_DOES_NOT_EXIST) return out;
        return R::fail(ErrorCode::IoError, "OpenServiceW " + std::to_string(e));
    }
    DWORD need = 0;
    (void)QueryServiceConfigW(svc.h, nullptr, 0, &need);
    if (need == 0 || need > 64 * 1024) return R::fail(ErrorCode::IoError, "QueryServiceConfigW size");
    std::vector<BYTE> buf(need);
    auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
    if (!QueryServiceConfigW(svc.h, cfg, need, &need)) return R::fail(ErrorCode::IoError, "QueryServiceConfigW " + std::to_string(GetLastError()));
    SERVICE_DELAYED_AUTO_START_INFO delayed{};
    DWORD n2 = 0;
    if (!QueryServiceConfig2W(svc.h, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, reinterpret_cast<LPBYTE>(&delayed), sizeof(delayed), &n2))
        return R::fail(ErrorCode::IoError, "QueryServiceConfig2W " + std::to_string(GetLastError()));
    auto st = current_state(svc.h);
    if (!st) return st.error();
    out.exists = true;
    out.running = st.value() == SERVICE_RUNNING;
    out.start_type = cfg->dwStartType;
    out.type = cfg->dwServiceType;
    out.account = cfg->lpServiceStartName ? cfg->lpServiceStartName : L"";
    out.cfg.image_path = cfg->lpBinaryPathName ? cfg->lpBinaryPathName : L"";
    out.cfg.auto_start_delayed = cfg->dwStartType == SERVICE_AUTO_START && delayed.fDelayedAutostart;
    return out;
}

Status stop_and_wait(SC_HANDLE svc) {
    auto st = current_state(svc);
    if (!st) return st.error();
    if (st.value() == SERVICE_STOPPED) return contracts::ok_status();
    if (st.value() != SERVICE_STOP_PENDING) {
        SERVICE_STATUS ss{};
        if (!ControlService(svc, SERVICE_CONTROL_STOP, &ss) && GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
            return win_fail("ControlService stop", GetLastError());
    }
    return wait_state(svc, SERVICE_STOPPED) ? contracts::ok_status() : fail(ErrorCode::IoError, "service did not stop in 30 s");
}

// ---- Kisayol (COM) ----
struct ComScope {
    HRESULT hr;
    ComScope() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
    bool usable() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

Result<std::wstring> read_link_target(const fs::path& lnk) {
    using R = Result<std::wstring>;
    ComScope com;
    if (!com.usable()) return R::fail(ErrorCode::IoError, "COM");
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return R::fail(ErrorCode::IoError, "ShellLink");
    ComPtr<IPersistFile> pf;
    if (FAILED(link.As(&pf)) || FAILED(pf->Load(lnk.c_str(), STGM_READ))) return R::fail(ErrorCode::IoError, "shortcut load");
    std::wstring buf(MAX_PATH * 4, L'\0');
    if (FAILED(link->GetPath(buf.data(), static_cast<int>(buf.size()), nullptr, SLGP_RAWPATH))) return R::fail(ErrorCode::IoError, "shortcut path");
    buf.resize(wcsnlen(buf.c_str(), buf.size()));
    return buf;
}

Status write_link(const fs::path& lnk, const fs::path& target) {
    {
        ComScope com;
        if (!com.usable()) return fail(ErrorCode::IoError, "COM");
        ComPtr<IShellLinkW> link;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
            return fail(ErrorCode::IoError, "ShellLink");
        if (FAILED(link->SetPath(target.c_str())) || FAILED(link->SetArguments(L"")) ||
            FAILED(link->SetIconLocation(target.c_str(), 0)) || FAILED(link->SetWorkingDirectory(target.parent_path().c_str())))
            return fail(ErrorCode::IoError, "shortcut fields");
        ComPtr<IPersistFile> pf;
        if (FAILED(link.As(&pf)) || FAILED(pf->Save(lnk.c_str(), TRUE))) return fail(ErrorCode::IoError, "shortcut save");
    }
    auto back = read_link_target(lnk);  // geri okuma
    if (!back) return back.error();
    if (!core::setup::same_path(back.value(), target)) return fail(ErrorCode::IoError, "shortcut read-back mismatch");
    return contracts::ok_status();
}

// ---- ARP (HKLM 64-bit gorunum) ----
struct Key {
    HKEY h = nullptr;
    Key() = default;
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    ~Key() {
        if (h) RegCloseKey(h);
    }
};

Result<std::wstring> get_sz(HKEY k, const wchar_t* name) {
    DWORD size = 0;
    LSTATUS e = RegGetValueW(k, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size);
    if (e == ERROR_FILE_NOT_FOUND) return std::wstring{};
    if (e != ERROR_SUCCESS || size > 64 * 1024) return Result<std::wstring>::fail(ErrorCode::IoError, "RegGetValueW");
    std::wstring s(size / sizeof(wchar_t) + 1, L'\0');
    DWORD sz2 = static_cast<DWORD>(s.size() * sizeof(wchar_t));
    e = RegGetValueW(k, nullptr, name, RRF_RT_REG_SZ, nullptr, s.data(), &sz2);
    if (e != ERROR_SUCCESS) return Result<std::wstring>::fail(ErrorCode::IoError, "RegGetValueW");
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
}

Result<DWORD> get_dword(HKEY k, const wchar_t* name) {
    DWORD v = 0, size = sizeof(v);
    const LSTATUS e = RegGetValueW(k, nullptr, name, RRF_RT_REG_DWORD, nullptr, &v, &size);
    if (e == ERROR_FILE_NOT_FOUND) return DWORD{0};
    if (e != ERROR_SUCCESS) return Result<DWORD>::fail(ErrorCode::IoError, "RegGetValueW dword");
    return v;
}

Result<std::optional<ArpValues>> read_arp(std::wstring_view key = core::setup::kArpKey) {
    using R = Result<std::optional<ArpValues>>;
    Key k;
    const LSTATUS e = RegOpenKeyExW(HKEY_LOCAL_MACHINE, std::wstring(key).c_str(), 0, KEY_READ | kWow64, &k.h);
    if (e == ERROR_FILE_NOT_FOUND) return std::optional<ArpValues>{};
    if (e != ERROR_SUCCESS) return R::fail(ErrorCode::IoError, "open ARP " + std::to_string(e));
    ArpValues v;
    struct S {
        const wchar_t* name;
        std::wstring* out;
    };
    const S strs[] = {{L"DisplayName", &v.display_name},         {L"DisplayVersion", &v.display_version},
                      {L"Publisher", &v.publisher},             {L"DisplayIcon", &v.display_icon},
                      {L"InstallLocation", &v.install_location}, {L"UninstallString", &v.uninstall_string},
                      {L"InstallDate", &v.install_date}};
    for (const auto& s : strs) {
        auto r = get_sz(k.h, s.name);
        if (!r) return r.error();
        *s.out = std::move(r).value();
    }
    auto size = get_dword(k.h, L"EstimatedSize");
    auto nm = get_dword(k.h, L"NoModify");
    auto nr = get_dword(k.h, L"NoRepair");
    if (!size || !nm || !nr) return R::fail(ErrorCode::IoError, "ARP dword");
    v.estimated_size_kb = size.value();
    v.no_modify = nm.value() == 1;
    v.no_repair = nr.value() == 1;
    return std::optional<ArpValues>{std::move(v)};
}

LSTATUS set_sz(HKEY k, const wchar_t* name, const std::wstring& v) {
    return RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
}
LSTATUS set_dword(HKEY k, const wchar_t* name, DWORD v) {
    return RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
}

}  // namespace

void* open_own_image_deny_write(const fs::path& self_image) {
    HANDLE h = CreateFileW(self_image.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    return h == INVALID_HANDLE_VALUE ? nullptr : h;
}

Result<PriorState> WinSetupOps::probe() {
    PriorState p;
    p.dir_existed = dir_exists(l_.dir);
    p.exe_existed = file_exists(l_.exe);
    if (file_exists(l_.shortcut)) {
        p.shortcut_existed = true;
        auto t = read_link_target(l_.shortcut);
        if (t) p.shortcut_target = t.value();  // okunamayan kisayol: hedefsiz kayit (geri almada bizimkiyle yazilmaz)
    }
    if (file_exists(l_.desktop_shortcut)) {  // ayni adli onceki ortak masaustu kisayolu: hedefi geri alma icin saklanir
        p.desktop_existed = true;
        auto t = read_link_target(l_.desktop_shortcut);
        if (t) p.desktop_target = t.value();
    }
    auto svc = snapshot_service();
    if (!svc) return svc.error();
    p.service_existed = svc.value().exists;
    p.service_running = svc.value().running;
    if (svc.value().exists) p.service_config = svc.value().cfg;
    p.updates_existed = dir_exists(l_.updates_dir);
    auto arp = read_arp();
    if (!arp) return arp.error();
    p.arp = std::move(arp).value();
    return p;
}

Status WinSetupOps::create_install_dir() {
    if (l_.dir.empty()) return fail(ErrorCode::MissingValue, "install dir");
    // Var olan klasor de (ACL'i guvensiz olabilir) ayni korumali DACL ile yeniden uygulanir; olmazsa kurulum durur.
    return ensure_protected_dir(l_.dir, kInstallDirSddl);
}

Status WinSetupOps::remove_install_dir_if_empty() {
    if (!any_exists(l_.dir)) return contracts::ok_status();
    if (RemoveDirectoryW(l_.dir.c_str())) return contracts::ok_status();
    return win_fail("remove install dir (not empty?)", GetLastError());
}

Status WinSetupOps::stage_and_swap_exe() {
    if (!source_) return fail(ErrorCode::MissingValue, "own image handle");
    const auto& sp = l_.swap;
    if (sp.exe.empty() || sp.staged.empty() || sp.backup.empty()) return fail(ErrorCode::MissingValue, "swap paths");
    const fs::path exe = widen(sp.exe), staged = widen(sp.staged), backup = widen(sp.backup);
    if (any_exists(backup)) return fail(ErrorCode::IoError, "stale backup present");

    // Kaynak: yazma paylasimsiz acik tutulan kendi goruntusu (yol yeniden cozulmez).
    auto src = read_whole(static_cast<HANDLE>(source_));
    if (!src) return src.error();
    const auto want = core::recovery::sha256(src.value());
    // Kurulu EXE'nin kendisi --setup ile calisiyor: kaynak = hedef, yazma/silme paylasimsiz tutuldugu icin degistirilemez
    // ve run_swap onu yeniden adlandiramaz. Kopyalanacak bir sey yok; icerik zaten ayni (yedek yok, geri alma no-op).
    if (core::setup::same_path(self_, exe)) return contracts::ok_status();

    if (!delete_if_present(staged)) return win_fail("remove stale staged", GetLastError());
    {
        FileHandle out(CreateFileW(staged.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!out.ok()) return win_fail("create staged", GetLastError());
        std::size_t off = 0;
        const auto& b = src.value();
        while (off < b.size()) {
            const DWORD n = static_cast<DWORD>(std::min<std::size_t>(b.size() - off, 1u << 20));
            DWORD done = 0;
            if (!WriteFile(out.h, b.data() + off, n, &done, nullptr) || done != n) return win_fail("write staged", GetLastError());
            off += n;
        }
        if (!FlushFileBuffers(out.h)) return win_fail("flush staged", GetLastError());
    }
    // Geri okuma: yeniden acilip SHA-256 kaynakla esit.
    auto back = read_file(staged);
    if (!back) return back.error();
    if (back.value().size() != src.value().size() || core::recovery::sha256(back.value()) != want)
        return fail(ErrorCode::IoError, "staged copy hash mismatch");
    // Yukseltilmis isci ile ayni guven politikasi (release: gecerli imza; EF_UNSIGNED_ALPHA ile imzasiz da, ADR-034).
    const auto trust = core::security::broker_trust(core::security::kBuildPolicy, verify_file_signature(staged));
    if (!trust.allowed) return fail(ErrorCode::Denied, "staged copy signature not trusted");

    if (!file_exists(exe)) {
        if (!MoveFileExW(staged.c_str(), exe.c_str(), MOVEFILE_WRITE_THROUGH)) return win_fail("place exe", GetLastError());
    } else if (core::catalog::run_swap(make_swap_fs(), sp) != core::catalog::SwapStep::Replaced) {
        return fail(ErrorCode::IoError, "swap failed");
    }
    auto placed = read_file(exe);
    if (!placed) return placed.error();
    if (core::recovery::sha256(placed.value()) != want) return fail(ErrorCode::IoError, "installed exe hash mismatch");
    return contracts::ok_status();
}

Status WinSetupOps::restore_previous_exe(const PriorState& p) {
    const auto& sp = l_.swap;
    const fs::path exe = widen(sp.exe), staged = widen(sp.staged);
    if (!p.exe_existed) {  // onceki EXE yoktu: yalniz bizim kopyamiz ve hazirlik dosyasi silinir
        if (!delete_if_present(exe) || !delete_if_present(staged)) return win_fail("remove new exe", GetLastError());
        return any_exists(exe) ? fail(ErrorCode::IoError, "exe still present") : contracts::ok_status();
    }
    if (!core::catalog::recover_swap(make_swap_fs(), sp, false)) return fail(ErrorCode::IoError, "recover_swap(false)");
    (void)delete_if_present(staged);
    (void)delete_if_present(widen(sp.staged + ".failed"));
    return file_exists(exe) ? contracts::ok_status() : fail(ErrorCode::IoError, "previous exe missing after restore");
}

Status WinSetupOps::commit_exe() {
    if (!core::catalog::recover_swap(make_swap_fs(), l_.swap, true)) return fail(ErrorCode::IoError, "recover_swap(true)");
    (void)delete_if_present(widen(l_.swap.staged + ".failed"));
    return any_exists(widen(l_.swap.backup)) ? fail(ErrorCode::IoError, "backup still present") : contracts::ok_status();
}

Status WinSetupOps::write_shortcut() {
    if (l_.shortcut.empty() || l_.exe.empty()) return fail(ErrorCode::MissingValue, "shortcut path");
    return write_link(l_.shortcut, l_.exe);
}

Status WinSetupOps::restore_shortcut(const PriorState& p) {
    if (!p.shortcut_existed) return remove_shortcut();
    if (!p.shortcut_target) return fail(ErrorCode::MissingValue, "previous shortcut target unknown");
    return write_link(l_.shortcut, *p.shortcut_target);
}

Status WinSetupOps::remove_shortcut() {
    if (!delete_if_present(l_.shortcut)) return win_fail("delete shortcut", GetLastError());
    return any_exists(l_.shortcut) ? fail(ErrorCode::IoError, "shortcut still present") : contracts::ok_status();
}

// Ortak masaustu (FOLDERID_PublicDesktop; yol isci icinde SHGetKnownFolderPath ile): Baslat menusu kisayoluyla ayni yazma +
// geri okuma. Kaldirma/geri alma karari saf kuraldadir (desktop_remove_action / desktop_restore_action): ayni adli yabanci
// veya okunamayan kullanici dosyasi silinmez, onceki hedef zaten yerindeyse yeniden yazilmaz.
namespace {
std::optional<std::wstring> link_target_or_none(const fs::path& lnk) {
    auto t = read_link_target(lnk);
    if (!t) return std::nullopt;
    return t.value();
}
Status delete_desktop_link(const fs::path& lnk) {
    if (!delete_if_present(lnk)) return win_fail("delete desktop shortcut", GetLastError());
    return any_exists(lnk) ? fail(ErrorCode::IoError, "desktop shortcut still present") : contracts::ok_status();
}
}  // namespace

Status WinSetupOps::write_desktop_shortcut() {
    if (l_.desktop_shortcut.empty() || l_.exe.empty()) return fail(ErrorCode::MissingValue, "desktop shortcut path");
    return write_link(l_.desktop_shortcut, l_.exe);
}

Status WinSetupOps::restore_desktop_shortcut(const PriorState& p) {
    if (l_.desktop_shortcut.empty()) return fail(ErrorCode::MissingValue, "desktop shortcut path");
    const bool present = any_exists(l_.desktop_shortcut);
    const auto current = present ? link_target_or_none(l_.desktop_shortcut) : std::nullopt;
    switch (core::setup::desktop_restore_action(p, present, current, l_.exe)) {
        case core::setup::LinkAction::Nothing: return contracts::ok_status();
        case core::setup::LinkAction::Delete: return delete_desktop_link(l_.desktop_shortcut);
        case core::setup::LinkAction::Write: return write_link(l_.desktop_shortcut, *p.desktop_target);
        case core::setup::LinkAction::Fail: break;
    }
    return fail(ErrorCode::MissingValue, "previous desktop shortcut target unknown");
}

Status WinSetupOps::remove_desktop_shortcut() {
    if (l_.desktop_shortcut.empty()) return fail(ErrorCode::MissingValue, "desktop shortcut path");
    const bool present = any_exists(l_.desktop_shortcut);
    const auto current = present ? link_target_or_none(l_.desktop_shortcut) : std::nullopt;
    if (core::setup::desktop_remove_action(present, current, l_.exe) != core::setup::LinkAction::Delete)
        return contracts::ok_status();  // yok ya da kullanicinin ayni adli baska dosyasi: dokunulmaz
    return delete_desktop_link(l_.desktop_shortcut);
}

Status WinSetupOps::apply_service_config(const ServiceConfig& c, bool create_if_missing) {
    return ensure_named_service(core::setup::kServiceName, core::setup::kServiceDisplayName, c, create_if_missing);
}

Status ensure_named_service(std::wstring_view service_name, std::wstring_view display_name, const ServiceConfig& c, bool create_if_missing) {
    if (c.image_path.empty() || service_name.empty()) return fail(ErrorCode::MissingValue, "service image");
    {
        Sc scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
        if (!scm.h) return win_fail("OpenSCManagerW", GetLastError());
        const std::wstring name(service_name), display(display_name);
        Sc svc(OpenServiceW(scm.h, name.c_str(), SERVICE_ALL_ACCESS));
        if (!svc.h) {
            const DWORD e = GetLastError();
            if (e != ERROR_SERVICE_DOES_NOT_EXIST || !create_if_missing) return win_fail("OpenServiceW", e);
            svc.h = CreateServiceW(scm.h, name.c_str(), display.c_str(), SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                                   SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, c.image_path.c_str(), nullptr, nullptr, nullptr,
                                   nullptr /*LocalSystem*/, nullptr);
            if (!svc.h) return win_fail("CreateServiceW", GetLastError());
        } else if (!ChangeServiceConfigW(svc.h, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                                         c.image_path.c_str(), nullptr, nullptr, nullptr, kLocalSystem, L"", display.c_str())) {
            return win_fail("ChangeServiceConfigW", GetLastError());
        }
        SERVICE_DESCRIPTIONW desc{const_cast<LPWSTR>(kServiceDescription)};
        SERVICE_DELAYED_AUTO_START_INFO delayed{c.auto_start_delayed ? TRUE : FALSE};
        SERVICE_SID_INFO sid{SERVICE_SID_TYPE_UNRESTRICTED};
        if (!ChangeServiceConfig2W(svc.h, SERVICE_CONFIG_DESCRIPTION, &desc) ||
            !ChangeServiceConfig2W(svc.h, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayed) ||
            !ChangeServiceConfig2W(svc.h, SERVICE_CONFIG_SERVICE_SID_INFO, &sid) ||
            !write_required_failure_config(svc.h, nullptr))
            return win_fail("ChangeServiceConfig2W", GetLastError());
        // Garanti: hata eylemleri (3x RESTART, kisa sifirlama, cikis kodu da hata) geri okunup dogrulanmazsa kurulum basarisiz.
        {
            core::setup::FailureConfig fc;
            if (!read_failure_config(svc.h, fc, nullptr) || !core::setup::failure_config_ok(fc))
                return fail(ErrorCode::IoError, "service failure actions read-back mismatch");
        }
    }
    // Geri okuma (QueryServiceConfigW): goruntu yolu, tur, baslatma, hesap, gecikme.
    auto snap = snapshot_service(service_name);
    if (!snap) return snap.error();
    const auto& s = snap.value();
    if (!s.exists || !iequal(s.cfg.image_path, c.image_path) || s.type != SERVICE_WIN32_OWN_PROCESS ||
        s.start_type != SERVICE_AUTO_START || !iequal(s.account, kLocalSystem) || s.cfg.auto_start_delayed != c.auto_start_delayed)
        return fail(ErrorCode::IoError, "service config read-back mismatch");
    return contracts::ok_status();
}

Status WinSetupOps::write_service() { return apply_service_config(ServiceConfig{core::setup::service_image_path(l_), true}, true); }

Status WinSetupOps::restore_service(const PriorState& p) {
    if (!p.service_existed) return delete_service();
    if (!p.service_config) return fail(ErrorCode::MissingValue, "previous service config unknown");
    return apply_service_config(*p.service_config, true);
}

Status WinSetupOps::delete_service() { return delete_named_service(core::setup::kServiceName); }

Status delete_named_service(std::wstring_view service_name) {
    const std::wstring name(service_name);
    Sc scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm.h) return win_fail("OpenSCManagerW", GetLastError());
    {
        Sc svc(OpenServiceW(scm.h, name.c_str(), DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_CHANGE_CONFIG));
        if (!svc.h) {
            const DWORD e = GetLastError();
            return e == ERROR_SERVICE_DOES_NOT_EXIST ? contracts::ok_status() : win_fail("OpenServiceW", e);
        }
        if (auto s = stop_and_wait(svc.h); !s) return s;
        if (!DeleteService(svc.h) && GetLastError() != ERROR_SERVICE_MARKED_FOR_DELETE) return win_fail("DeleteService", GetLastError());
    }
    // Geri okuma: hizmet yok ya da silinmek uzere isaretli (son tanitici kapaninca SCM kaldirir).
    const ULONGLONG deadline = GetTickCount64() + 5000;
    for (;;) {
        Sc svc(OpenServiceW(scm.h, name.c_str(), SERVICE_CHANGE_CONFIG));
        if (!svc.h) {
            const DWORD e = GetLastError();
            if (e == ERROR_SERVICE_DOES_NOT_EXIST) return contracts::ok_status();
            return win_fail("OpenServiceW read-back", e);
        }
        if (!ChangeServiceConfigW(svc.h, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr) &&
            GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE)
            return contracts::ok_status();
        if (GetTickCount64() >= deadline) return fail(ErrorCode::IoError, "service still present after delete");
        Sleep(250);
    }
}

// Guncelleme (hizmet degisimi) sonrasi hizmet SCM tarafindan yeniden baslatilmadiysa yukseltilmis onarim hizmeti baslatir.
// Durum Swapped ve hedef surum bu (kurulum yapan, calisan) EXE'nin surumuyse UiHealthy'ye cekilir; yoksa hizmet acilisi
// "saglik kanitlanmadi" sayip yeni EXE'yi eskisiyle ezerdi. Yazma atomik (tmp + MOVEFILE_REPLACE_EXISTING|WRITE_THROUGH); hata sessiz degil
// ama kurulumu da bozmaz (durum yoksa/okunamazsa hicbir sey degismez).
static void adopt_pending_update_state(const core::setup::InstallLayout& l, const std::string& running_version) {
    std::ifstream in(l.update_state, std::ios::binary);
    if (!in) return;
    std::ostringstream ss;
    ss << in.rdbuf();
    in.close();
    auto st = core::update::decode_state(ss.str());
    if (!st || !core::update::adopt_swapped_as_healthy(st.value(), running_version)) return;
    const fs::path tmp = fs::path(l.update_state).concat(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << core::update::encode_state(st.value());
        out.flush();
        if (!out) return;
    }
    if (!MoveFileExW(tmp.c_str(), l.update_state.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        fs::remove(tmp, ec);
    }
}

Status WinSetupOps::start_service() {
    Sc scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm.h) return win_fail("OpenSCManagerW", GetLastError());
    Sc svc(OpenServiceW(scm.h, core::setup::kServiceName.data(), SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS));
    if (!svc.h) return win_fail("OpenServiceW", GetLastError());
    if (auto s = stop_and_wait(svc.h); !s) return s;  // calisiyorsa durdur-baslat (yeni EXE yuklensin)
    std::string ver;
    for (const wchar_t c : version_) ver.push_back(static_cast<char>(c));  // surum yalniz ASCII (rakam ve nokta)
    adopt_pending_update_state(l_, ver);
    if (!StartServiceW(svc.h, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) return win_fail("StartServiceW", GetLastError());
    if (!wait_state(svc.h, SERVICE_RUNNING)) return fail(ErrorCode::IoError, "service not running after 30 s");
    // Hemen durup (cikis kodu) kalan hizmet "basladi" sayilmaz: kisa sure sonra hala RUNNING olmali; aksi gorunur kurulum hatasi.
    Sleep(1500);
    auto again = current_state(svc.h);
    if (!again || again.value() != SERVICE_RUNNING) return fail(ErrorCode::IoError, "service stopped right after start");
    return contracts::ok_status();
}

Status WinSetupOps::stop_service() { return stop_named_service(core::setup::kServiceName); }

Status stop_named_service(std::wstring_view service_name) {
    Sc scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm.h) return win_fail("OpenSCManagerW", GetLastError());
    Sc svc(OpenServiceW(scm.h, std::wstring(service_name).c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS));
    if (!svc.h) {
        const DWORD e = GetLastError();
        return e == ERROR_SERVICE_DOES_NOT_EXIST ? contracts::ok_status() : win_fail("OpenServiceW", e);
    }
    return stop_and_wait(svc.h);
}

Status start_named_service(std::wstring_view service_name) {
    Sc scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm.h) return win_fail("OpenSCManagerW", GetLastError());
    Sc svc(OpenServiceW(scm.h, std::wstring(service_name).c_str(), SERVICE_START | SERVICE_QUERY_STATUS));
    if (!svc.h) return win_fail("OpenServiceW", GetLastError());
    if (!StartServiceW(svc.h, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) return win_fail("StartServiceW", GetLastError());
    if (!wait_state(svc.h, SERVICE_RUNNING)) return fail(ErrorCode::IoError, "service not running after 30 s");
    Sleep(1500);  // WinSetupOps::start_service ile ayni: hemen duran hizmet "basladi" sayilmaz
    auto again = current_state(svc.h);
    if (!again || again.value() != SERVICE_RUNNING) return fail(ErrorCode::IoError, "service stopped right after start");
    return contracts::ok_status();
}

Result<NamedServiceState> query_named_service(std::wstring_view service_name) {
    using R = Result<NamedServiceState>;
    NamedServiceState out;
    Sc scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm.h) return R::fail(ErrorCode::IoError, "OpenSCManagerW " + std::to_string(GetLastError()));
    Sc svc(OpenServiceW(scm.h, std::wstring(service_name).c_str(), SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS));
    if (!svc.h) {
        const DWORD e = GetLastError();
        if (e == ERROR_SERVICE_DOES_NOT_EXIST) return out;
        return R::fail(ErrorCode::IoError, "OpenServiceW " + std::to_string(e));
    }
    SERVICE_STATUS_PROCESS sp{};
    DWORD need = 0;
    if (!QueryServiceStatusEx(svc.h, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&sp), sizeof(sp), &need))
        return R::fail(ErrorCode::IoError, "QueryServiceStatusEx " + std::to_string(GetLastError()));
    (void)QueryServiceConfigW(svc.h, nullptr, 0, &need);
    if (need == 0 || need > 64 * 1024) return R::fail(ErrorCode::IoError, "QueryServiceConfigW size");
    std::vector<BYTE> buf(need);
    auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
    if (!QueryServiceConfigW(svc.h, cfg, need, &need)) return R::fail(ErrorCode::IoError, "QueryServiceConfigW " + std::to_string(GetLastError()));
    out.exists = true;
    out.state = sp.dwCurrentState;
    out.pid = sp.dwProcessId;
    out.image_path = cfg->lpBinaryPathName ? cfg->lpBinaryPathName : L"";
    return out;
}

Result<std::optional<ArpValues>> read_arp_key(std::wstring_view key) { return read_arp(key); }
Result<std::wstring> read_shortcut_target(const fs::path& lnk) { return read_link_target(lnk); }
Status write_shortcut_link(const fs::path& lnk, const fs::path& target) { return write_link(lnk, target); }

Status WinSetupOps::create_updates_dir() {
    if (auto s = ensure_machine_root(l_.machine_dir); !s) return s;
    return ensure_protected_dir(l_.updates_dir, kAdminOnlySddl);
}

Status WinSetupOps::remove_updates_dir() {
    if (!any_exists(l_.updates_dir)) return contracts::ok_status();
    std::error_code ec;
    fs::remove_all(l_.updates_dir, ec);  // baglanti noktasi izlenmez (baglantinin kendisi silinir)
    if (ec) return fail(ErrorCode::IoError, "remove updates dir: " + ec.message());
    return any_exists(l_.updates_dir) ? fail(ErrorCode::IoError, "updates dir still present") : contracts::ok_status();
}

Status WinSetupOps::write_arp(const ArpValues& v) { return write_arp_key(core::setup::kArpKey, v); }

Status write_arp_key(std::wstring_view arp_key, const ArpValues& v) {
    if (arp_key.empty()) return fail(ErrorCode::MissingValue, "ARP key");
    {
        Key k;
        const LSTATUS e = RegCreateKeyExW(HKEY_LOCAL_MACHINE, std::wstring(arp_key).c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                          KEY_READ | KEY_WRITE | kWow64, nullptr, &k.h, nullptr);
        if (e != ERROR_SUCCESS) return win_fail("RegCreateKeyExW ARP", static_cast<DWORD>(e));
        const LSTATUS rs[] = {set_sz(k.h, L"DisplayName", v.display_name),
                              set_sz(k.h, L"DisplayVersion", v.display_version),
                              set_sz(k.h, L"Publisher", v.publisher),
                              set_sz(k.h, L"DisplayIcon", v.display_icon),
                              set_sz(k.h, L"InstallLocation", v.install_location),
                              set_sz(k.h, L"UninstallString", v.uninstall_string),
                              set_sz(k.h, L"InstallDate", v.install_date),
                              set_dword(k.h, L"EstimatedSize", v.estimated_size_kb),
                              set_dword(k.h, L"NoModify", v.no_modify ? 1u : 0u),
                              set_dword(k.h, L"NoRepair", v.no_repair ? 1u : 0u)};
        for (const auto r : rs)
            if (r != ERROR_SUCCESS) return win_fail("RegSetValueExW ARP", static_cast<DWORD>(r));
        (void)RegDeleteValueW(k.h, L"QuietUninstallString");  // spec: sessiz kaldirma komutu yok
        (void)RegFlushKey(k.h);
    }
    auto back = read_arp(arp_key);
    if (!back) return back.error();
    if (!back.value() || !(*back.value() == v)) return fail(ErrorCode::IoError, "ARP read-back mismatch");
    return contracts::ok_status();
}

Status refresh_arp_display_version(const core::setup::InstallLayout& layout, const std::wstring& running_version, bool* changed,
                                   std::wstring* prior) {
    if (changed) *changed = false;
    auto cur = read_arp();
    if (!cur) return cur.error();
    const auto plan = core::setup::plan_display_version_refresh(cur.value(), layout, running_version);
    if (!plan) return contracts::ok_status();
    if (prior) *prior = cur.value()->display_version;
    {
        Key k;
        const LSTATUS e = RegOpenKeyExW(HKEY_LOCAL_MACHINE, core::setup::kArpKey.data(), 0, KEY_SET_VALUE | KEY_READ | kWow64, &k.h);
        if (e != ERROR_SUCCESS) return win_fail("open ARP for version refresh", static_cast<DWORD>(e));
        const LSTATUS r = set_sz(k.h, L"DisplayVersion", plan->display_version);
        if (r != ERROR_SUCCESS) return win_fail("RegSetValueExW DisplayVersion", static_cast<DWORD>(r));
        (void)RegFlushKey(k.h);
    }
    auto back = read_arp();
    if (!back) return back.error();
    if (!back.value() || !(*back.value() == *plan)) return fail(ErrorCode::IoError, "ARP DisplayVersion read-back mismatch");
    if (changed) *changed = true;
    return contracts::ok_status();
}

Status WinSetupOps::restore_arp(const PriorState& p) {
    if (p.arp) return write_arp(*p.arp);
    return remove_arp();
}

Status WinSetupOps::remove_arp() { return remove_arp_key(core::setup::kArpKey); }

Status remove_arp_key(std::wstring_view arp_key) {
    if (arp_key.empty()) return fail(ErrorCode::MissingValue, "ARP key");
    const std::wstring key(arp_key);
    {
        Key k;
        const LSTATUS e = RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_ALL_ACCESS | kWow64, &k.h);
        if (e == ERROR_FILE_NOT_FOUND) return contracts::ok_status();
        if (e != ERROR_SUCCESS) return win_fail("open ARP", static_cast<DWORD>(e));
        const LSTATUS t = RegDeleteTreeW(k.h, nullptr);
        if (t != ERROR_SUCCESS) return win_fail("RegDeleteTreeW ARP", static_cast<DWORD>(t));
    }
    const LSTATUS d = RegDeleteKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), kWow64, 0);
    if (d != ERROR_SUCCESS && d != ERROR_FILE_NOT_FOUND) return win_fail("RegDeleteKeyExW ARP", static_cast<DWORD>(d));
    auto back = read_arp(key);
    if (!back) return back.error();
    return back.value() ? fail(ErrorCode::IoError, "ARP key still present") : contracts::ok_status();
}

Result<bool> WinSetupOps::machine_journals_clean() {
    auto root = machine_journal_dir(false);  // salt okuma; var olan kok/dizin guvenlik dogrulamasindan gecer
    if (!root) {
        if (root.code() == ErrorCode::MissingValue) return true;  // makine journal'i hic olusmamis
        return root.error();
    }
    for (const wchar_t* sub : {static_cast<const wchar_t*>(nullptr), L"fixes"}) {
        fs::path dir = root.value();
        if (sub) {
            dir /= sub;
            if (!any_exists(dir)) continue;
            if (auto s = verify_machine_journal_dir(dir); !s) return s.error();
        }
        WinJournalFs jfs(dir);
        core::recovery::JournalStore store(jfs, machine_id());
        const auto listing = store.scan();
        if (!core::broker::journal_active_ops(listing, true).empty() || !core::broker::journal_unreconciled_ops(listing, true).empty())
            return false;
    }
    return true;
}

Status WinSetupOps::remove_machine_data() {
    if (!any_exists(l_.machine_dir)) return contracts::ok_status();
    if (is_reparse_or_missing_dir(l_.machine_dir)) return fail(ErrorCode::Denied, "machine dir is not a plain directory");
    std::error_code ec;
    std::vector<fs::path> victims;
    for (fs::directory_iterator it(l_.machine_dir, ec), end; !ec && it != end; it.increment(ec))
        if (!core::setup::same_path(it->path(), l_.setup_dir)) victims.push_back(it->path());  // canli kurulum journal'i kalir
    if (ec) return fail(ErrorCode::IoError, "list machine dir: " + ec.message());
    for (const auto& v : victims) {
        fs::remove_all(v, ec);
        if (ec) return fail(ErrorCode::IoError, "remove machine data: " + ec.message());
    }
    for (const auto& v : victims)
        if (any_exists(v)) return fail(ErrorCode::IoError, "machine data still present");
    return contracts::ok_status();
}

Result<bool> WinSetupOps::remove_program_files() {
    if (!any_exists(l_.dir)) return false;
    if (is_reparse_or_missing_dir(l_.dir)) return Result<bool>::fail(ErrorCode::Denied, "install dir is not a plain directory");
    const bool self_inside = core::setup::same_path(self_.parent_path(), l_.dir);
    bool reboot = false;
    std::error_code ec;
    std::vector<fs::path> entries;
    for (fs::directory_iterator it(l_.dir, ec), end; !ec && it != end; it.increment(ec)) entries.push_back(it->path());
    if (ec) return Result<bool>::fail(ErrorCode::IoError, "list install dir: " + ec.message());
    for (const auto& e : entries) {
        if (self_inside && core::setup::same_path(e, self_)) continue;
        fs::remove_all(e, ec);
        if (!ec) continue;
        // Kullanimdaki dosya (or. eski surum .old): yeniden baslatmada silinmek uzere isaretlenir.
        if (!file_exists(e) || !MoveFileExW(e.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
            return Result<bool>::fail(ErrorCode::IoError, "remove install file: " + ec.message());
        reboot = true;
    }
    if (self_inside) {
        // Calisan EXE kendini silemez: once dosya, sonra (bos kalacak) klasor yeniden baslatmada silinir.
        if (!MoveFileExW(self_.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) ||
            !MoveFileExW(l_.dir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
            return Result<bool>::fail(ErrorCode::IoError, "schedule delete on reboot (" + std::to_string(GetLastError()) + ")");
        return true;
    }
    if (reboot) {
        if (!MoveFileExW(l_.dir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
            return Result<bool>::fail(ErrorCode::IoError, "schedule dir delete (" + std::to_string(GetLastError()) + ")");
        return true;
    }
    if (!RemoveDirectoryW(l_.dir.c_str())) return Result<bool>::fail(ErrorCode::IoError, "remove install dir (" + std::to_string(GetLastError()) + ")");
    return false;
}

}  // namespace ef::platform::install
