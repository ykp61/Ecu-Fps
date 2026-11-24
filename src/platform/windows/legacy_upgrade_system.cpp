// T3b (ADR-043 ek): eski adli kurulumdan yukseltmenin Win32 tarafi. GERCEK CIHAZDA DENENMEDI.
#include "platform/windows/legacy_upgrade_system.h"

#include <windows.h>

#include <cwchar>
#include <vector>

#include "core/catalog/envelope.h"
#include "core/legacy/legacy_entry.h"
#include "core/legacy/legacy_install.h"
#include "core/legacy/legacy_names.h"
#include "core/product_info.h"
#include "core/roles.h"
#include "core/security/trust.h"
#include "core/setup/arp.h"
#include "core/update/staged_update.h"
#include "platform/windows/elevation.h"
#include "platform/windows/install/secure_dir.h"
#include "platform/windows/install/win_setup_ops.h"
#include "platform/windows/journal_fs.h"
#include "platform/windows/known_roots.h"
#include "platform/windows/legacy_migration.h"
#include "platform/windows/pipe.h"
#include "platform/windows/service/update_store.h"
#include "platform/windows/signature.h"
#include "platform/windows/win_env.h"

namespace ef::platform {

namespace legacy = core::legacy;
using legacy::FsCode;
using legacy::FsStatus;
using legacy::SysOp;
namespace fs = std::filesystem;

namespace {

FsStatus from(const contracts::Status& s) {
    if (s) return {};
    const auto c = s.code();
    const FsCode code = c == contracts::ErrorCode::Denied ? FsCode::Denied : FsCode::Io;
    return FsStatus::fail(code, s.error().detail);
}

FsStatus not_allowed(const char* what) { return FsStatus::fail(FsCode::Denied, std::string("not on the upgrade allow-list: ") + what); }

std::wstring w(const std::string& u8) { return to_wide(u8); }

std::wstring version_text() {
    const auto v = core::product_version();
    return std::to_wstring(v.major) + L"." + std::to_wstring(v.minor) + L"." + std::to_wstring(v.patch);
}

std::wstring local_yyyymmdd() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t buf[16] = {};
    (void)swprintf(buf, 16, L"%04u%02u%02u", static_cast<unsigned>(t.wYear), static_cast<unsigned>(t.wMonth), static_cast<unsigned>(t.wDay));
    return buf;
}

bool file_present(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::int64_t now_unix_ms() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    const auto t = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<std::int64_t>((t - 116444736000000000ull) / 10000ull);
}

}  // namespace

WinUpgradeSystem::WinUpgradeSystem(core::setup::InstallLayout legacy_layout, core::setup::InstallLayout current, legacy::UpgradePaths paths,
                                   legacy::UpgradeNames names, std::wstring version)
    : legacy_(std::move(legacy_layout)), cur_(std::move(current)), p_(std::move(paths)), n_(std::move(names)), version_(std::move(version)) {}

// ---- ACL ----
FsStatus WinUpgradeSystem::apply_machine_acl(const std::string& dir) {
    if (!legacy::upgrade_path_allowed(SysOp::MachineAcl, dir, p_)) return not_allowed("machine acl");
    const fs::path d = w(dir);
    if (GetFileAttributesW(d.c_str()) != INVALID_FILE_ATTRIBUTES && install::is_reparse_or_missing_dir(d))
        return FsStatus::fail(FsCode::Denied, "not a plain directory");
    // SY + BA tam, Kullanicilar yalniz okuma (makine journal'i kurali; UI eski kaydi okuyabilsin). Geri okunur.
    if (auto s = from(install::ensure_protected_dir(d, install::kMachineRootSddl)); !s.ok()) return s;
    // Yukseltme kaydi (setup\legacy-upgrade.json) kokun setup\ altinda: SY + BA yalniz (kullanici okuyamaz da).
    if (legacy::same_canonical_path(d.native(), w(p_.new_machine_root)))
        if (auto s = from(install::ensure_protected_dir(d / L"setup", install::kAdminOnlySddl)); !s.ok()) return s;
    return verify_machine_acl(dir);
}

FsStatus WinUpgradeSystem::verify_machine_acl(const std::string& dir) { return from(verify_machine_journal_dir(w(dir))); }

FsStatus WinUpgradeSystem::apply_install_acl(const std::string& dir) {
    if (!legacy::upgrade_path_allowed(SysOp::InstallAcl, dir, p_)) return not_allowed("install acl");
    return from(install::ensure_protected_dir(w(dir), install::kInstallDirSddl));
}

// ---- SCM ----
legacy::SvcState WinUpgradeSystem::service_state(std::wstring_view name) {
    const auto q = install::query_named_service(name);
    if (!q) return legacy::SvcState::Pending;  // okunamadi: "yok" sayilmaz (olusturma / silme denenmez)
    if (!q.value().exists) return legacy::SvcState::Missing;
    if (q.value().state == SERVICE_RUNNING) return legacy::SvcState::Running;
    if (q.value().state == SERVICE_STOPPED) return legacy::SvcState::Stopped;
    return legacy::SvcState::Pending;
}

std::optional<std::string> WinUpgradeSystem::service_exe(std::wstring_view name) {
    const auto q = install::query_named_service(name);
    if (!q || !q.value().exists) return std::nullopt;
    const auto exe = legacy::image_path_exe(q.value().image_path);
    if (!exe) return std::nullopt;
    return to_utf8(*exe);
}

FsStatus WinUpgradeSystem::create_service(std::wstring_view name, const std::string& exe) {
    if (!legacy::upgrade_name_allowed(SysOp::CreateService, name, n_) || !legacy::same_canonical_path(w(exe), cur_.exe.native()))
        return not_allowed("create service");
    // WinSetupOps::write_service ile ayni yapilandirma: LocalSystem, gecikmeli otomatik, SID turu, 3x RESTART; geri okunur.
    return from(install::ensure_named_service(name, core::setup::kServiceDisplayName,
                                              core::setup::ServiceConfig{core::setup::service_image_path(cur_), true}, true));
}

FsStatus WinUpgradeSystem::start_service(std::wstring_view name) {
    if (!legacy::upgrade_name_allowed(SysOp::StartService, name, n_)) return not_allowed("start service");
    return from(install::start_named_service(name));
}

FsStatus WinUpgradeSystem::stop_service(std::wstring_view name) {
    if (!legacy::upgrade_name_allowed(SysOp::StopService, name, n_)) return not_allowed("stop service");
    const auto s = install::stop_named_service(name);
    if (s) return {};
    return FsStatus::fail(service_state(name) == legacy::SvcState::Stopped ? FsCode::Io : FsCode::Busy, s.error().detail);
}

FsStatus WinUpgradeSystem::delete_service(std::wstring_view name) {
    if (!legacy::upgrade_name_allowed(SysOp::DeleteService, name, n_)) return not_allowed("delete service");
    return from(install::delete_named_service(name));
}

bool WinUpgradeSystem::service_healthy(std::wstring_view name, std::int64_t timeout_ms) {
    // Saglik: SCM RUNNING + PID ve yeni hizmetin kendi pipe'i bu PID'den sunuluyor (ad gaspi yok). Hello gonderilmez:
    // yeni hizmet yalniz dogrulanmis kurulu UI istemcisine hizmet verir (check_client); bu kural gevsetilmez.
    if (name != n_.new_service) return false;
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(timeout_ms < 0 ? 0 : timeout_ms);
    for (;;) {
        const auto q = install::query_named_service(name);
        if (q && q.value().exists && q.value().state == SERVICE_RUNNING && q.value().pid != 0) {
            auto c = PipeClient::connect_service(std::wstring(core::setup::kPipeName), q.value().pid, 2000);
            if (c.ok() && c.value()->server_pid() == q.value().pid) return true;
        }
        if (q && q.value().exists && q.value().state == SERVICE_STOPPED) return false;  // basladiktan sonra durdu
        if (GetTickCount64() >= deadline) return false;
        Sleep(500);
    }
}

// ---- ARP ----
std::optional<std::string> WinUpgradeSystem::arp_install_location(std::wstring_view key) {
    const auto r = install::read_arp_key(key);
    if (!r) return std::string("<unreadable>");  // okunamayan anahtar "yok" sayilmaz: uzerine yazilmaz, silinmez
    if (!r.value()) return std::nullopt;
    return to_utf8(r.value()->install_location);
}

FsStatus WinUpgradeSystem::write_arp(std::wstring_view key) {
    if (!legacy::upgrade_name_allowed(SysOp::WriteArp, key, n_)) return not_allowed("write arp");
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(cur_.exe.c_str(), GetFileExInfoStandard, &a)) return FsStatus::fail(FsCode::Io, "new exe size");
    const auto bytes = (static_cast<std::uint64_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    return from(install::write_arp_key(key, core::setup::make_arp_values(cur_, version_, bytes, local_yyyymmdd())));
}

FsStatus WinUpgradeSystem::delete_arp(std::wstring_view key) {
    if (!legacy::upgrade_name_allowed(SysOp::DeleteArp, key, n_)) return not_allowed("delete arp");
    return from(install::remove_arp_key(key));
}

// ---- Kisayollar ----
bool WinUpgradeSystem::shortcut_present(const std::string& lnk) { return file_present(w(lnk)); }

std::optional<std::string> WinUpgradeSystem::shortcut_target(const std::string& lnk) {
    const auto t = install::read_shortcut_target(w(lnk));
    if (!t) return std::nullopt;
    return to_utf8(t.value());
}

FsStatus WinUpgradeSystem::write_shortcut(const std::string& lnk, const std::string& target) {
    if (!legacy::upgrade_path_allowed(SysOp::WriteShortcut, lnk, p_) || !legacy::same_canonical_path(w(target), cur_.exe.native()))
        return not_allowed("write shortcut");
    return from(install::write_shortcut_link(w(lnk), w(target)));
}

FsStatus WinUpgradeSystem::delete_shortcut(const std::string& lnk) {
    if (!legacy::upgrade_path_allowed(SysOp::DeleteShortcut, lnk, p_)) return not_allowed("delete shortcut");
    const auto p = w(lnk);
    if (!DeleteFileW(p.c_str())) {
        const DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND)
            return FsStatus::fail(e == ERROR_ACCESS_DENIED ? FsCode::Denied : FsCode::Io, "delete shortcut (" + std::to_string(e) + ")");
    }
    return file_present(p) ? FsStatus::fail(FsCode::Io, "shortcut still present") : FsStatus{};
}

// ---- Dosyalar ----
FsStatus WinUpgradeSystem::schedule_delete_on_reboot(const std::string& path) {
    if (!legacy::upgrade_path_allowed(SysOp::RebootDelete, path, p_)) return not_allowed("reboot delete");
    if (!MoveFileExW(w(path).c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
        return FsStatus::fail(FsCode::Io, "MoveFileExW delay (" + std::to_string(GetLastError()) + ")");
    return {};
}

FsStatus WinUpgradeSystem::restore_legacy_exe() {
    // 0.9.5 UpdateCoordinator::roll_back ile ayni: recover_swap(false) (exe -> exe.new.failed, exe.old -> exe), hazirlik
    // dosyasi silinir, eski durum dosyasi Idle + tekrar oynatma tabani (eski hizmet yeniden basladiginda normal acilir).
    const auto& sp = legacy_.swap;
    if (sp.exe.empty() || sp.backup.empty()) return FsStatus::fail(FsCode::NotFound, "legacy swap paths");
    if (!file_present(w(sp.backup))) return FsStatus::fail(FsCode::NotFound, "old exe backup missing");
    if (!core::catalog::recover_swap(service::win_swap_fs(), sp, false)) return FsStatus::fail(FsCode::Busy, "recover_swap(false)");
    if (!file_present(w(sp.exe))) return FsStatus::fail(FsCode::Io, "old exe missing after restore");
    (void)DeleteFileW(w(sp.staged).c_str());
    WinMigrationFs mfs;
    std::string text;
    if (mfs.read_file(to_utf8(legacy_.update_state.native()), 64 * 1024, text).ok()) {
        if (const auto next = legacy::legacy_state_after_restore(text)) {
            const auto st = core::update::decode_state(*next);
            service::FileStageSink sink(legacy_.updates_dir);
            service::FileUpdateHost host(legacy_.update_state, sink);
            if (st.ok()) (void)host.save_state(st.value());  // atomik (tmp + MoveFileEx WRITE_THROUGH); yazilamazsa eski hizmet Commit eder
        }
    }
    return {};
}

// ---- Hizmet girisi (a) ----
namespace {

SERVICE_STATUS_HANDLE g_lstatus = nullptr;
DWORD g_lcheckpoint = 0;

void lset(DWORD state, DWORD specific = 0) {
    if (!g_lstatus) return;
    SERVICE_STATUS s{};
    s.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s.dwCurrentState = state;
    s.dwControlsAccepted = 0;  // yukseltme sirasinda durdurma kabul edilmez (yarim adim kalmasin; her adim kalici kayitli)
    s.dwWin32ExitCode = specific ? ERROR_SERVICE_SPECIFIC_ERROR : NO_ERROR;
    s.dwServiceSpecificExitCode = specific;
    s.dwWaitHint = state == SERVICE_START_PENDING ? 10000 : 0;
    s.dwCheckPoint = state == SERVICE_RUNNING || state == SERVICE_STOPPED ? 0 : ++g_lcheckpoint;
    (void)SetServiceStatus(g_lstatus, &s);
}

DWORD WINAPI legacy_control(DWORD control, DWORD, LPVOID, LPVOID) {
    return control == SERVICE_CONTROL_INTERROGATE ? NO_ERROR : ERROR_CALL_NOT_IMPLEMENTED;
}

void debug(const std::string& line) { OutputDebugStringA((line + "\n").c_str()); }

std::wstring final_path_of(const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::vector<wchar_t> buf(1024);
    DWORD n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED);
    if (n >= buf.size() && n < 32768) {
        buf.resize(static_cast<std::size_t>(n) + 1);
        n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED);
    }
    CloseHandle(h);
    if (n == 0 || n >= buf.size()) return {};
    return std::wstring(buf.data(), n);  // \\?\ oneki canonical_local_path'te soyulur; \\?\UNC\ reddedilir
}

// Surucu kokunden sonraki her bilesen (Program Files dahil) ve EXE: yeniden ayristirma noktasi yok.
void probe_reparse(const std::wstring& exe, bool& reparse, bool& failed) {
    const auto canon = legacy::canonical_local_path(exe);
    if (!canon) return void(failed = true);
    for (std::size_t i = 3; i <= exe.size(); ++i) {
        if (i != exe.size() && exe[i] != L'\\' && exe[i] != L'/') continue;
        const auto prefix = exe.substr(0, i);
        const DWORD a = GetFileAttributesW(prefix.c_str());
        if (a == INVALID_FILE_ATTRIBUTES) return void(failed = true);
        if (a & FILE_ATTRIBUTE_REPARSE_POINT) reparse = true;
    }
}

bool token_is_local_system() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    DWORD need = 0;
    (void)GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
    bool ok = false;
    if (need > 0 && need < 4096) {
        std::vector<BYTE> buf(need);
        if (GetTokenInformation(tok, TokenUser, buf.data(), need, &need))
            ok = IsWellKnownSid(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, WinLocalSystemSid) != FALSE;
    }
    CloseHandle(tok);
    return ok;
}

legacy::LegacyServiceFacts gather_legacy_facts() {
    legacy::LegacyServiceFacts f;
    f.self_raw = self_exe_path().native();
    f.self_final = final_path_of(f.self_raw);
    probe_reparse(f.self_raw, f.reparse_in_path, f.path_probe_failed);
    f.token_system = token_is_local_system();
    f.scm_connected = g_lstatus != nullptr;
    f.self_pid = GetCurrentProcessId();
    if (const auto q = install::query_named_service(legacy::kOldServiceName); q && q.value().exists) {
        f.scm_pid = q.value().pid;
        f.scm_image_path = q.value().image_path;
    }
    // Calisan goruntu: yukseltilmis rollerle ayni guven politikasi (release: gecerli imza; ADR-034 imzasiz alpha etiketli).
    const auto trust = core::security::broker_trust(core::security::kBuildPolicy, verify_file_signature(f.self_raw));
    f.image_trusted = trust.allowed;
    if (trust.dev_unsigned) OutputDebugStringA(core::security::dev_bypass_marker().data());
    if (trust.unsigned_alpha) OutputDebugStringA(core::security::unsigned_alpha_marker().data());
    return f;
}

void WINAPI legacy_service_main(DWORD, LPWSTR*) {
    const std::wstring name(legacy::kOldServiceName);
    g_lstatus = RegisterServiceCtrlHandlerExW(name.c_str(), legacy_control, nullptr);
    if (!g_lstatus) return;
    lset(SERVICE_START_PENDING);
    const auto roots = known_roots();
    const auto old_layout = legacy::legacy_layout(roots);
    const auto cur = core::setup::make_layout(roots);
    const auto facts = gather_legacy_facts();
    const auto d = legacy::decide_legacy_service_entry(facts, old_layout);
    debug("legacy_service entry=" + std::string(legacy::to_string(d.action)) + " reason=\"" + d.reason + "\"");
    lset(SERVICE_RUNNING);  // SCM'yi bekletmez; durdurma kabul edilmez
    DWORD exit_code = legacy::kLegacyHostExitWrongLocation;
    if (d.action == legacy::LegacyEntry::RunUpgrade || d.action == legacy::LegacyEntry::RestoreOldExe) {
        const auto paths = legacy::make_upgrade_paths(roots, fs::path(facts.self_raw));
        WinUpgradeSystem sys(old_layout, cur, paths, legacy::default_upgrade_names(), version_text());
        if (d.action == legacy::LegacyEntry::RestoreOldExe) {
            exit_code = sys.restore_legacy_exe().ok() ? legacy::kLegacyHostExitRolledBack : legacy::kLegacyHostExitRetry;
        } else {
            WinMigrationFs mfs;
            legacy::UpgradeEnv env;
            env.role = legacy::UpgradeRole::LegacyHost;
            env.version = to_utf8(version_text());
            env.now_unix_ms = now_unix_ms();
            env.machine_id = machine_id();
            env.sleep = [](int attempt) { Sleep(static_cast<DWORD>(250 * (attempt + 1))); };
            const auto r = legacy::run_legacy_upgrade(mfs, sys, paths, legacy::default_upgrade_names(), env);
            debug(legacy::upgrade_log_line(r));
            exit_code = r.exit_code;
        }
    }
    // 0: yeni hizmet devraldi (NO_ERROR, SCM yeniden baslatmaz). 5/6/1: hizmete ozgu hata -> eski kurulumun hata eylemi.
    lset(SERVICE_STOPPED, exit_code);
}

}  // namespace

bool legacy_service_dispatch_candidate() {
    return legacy::legacy_service_candidate(self_exe_path().native(), legacy::legacy_layout(known_roots()));
}

int run_legacy_service_host() {
    std::wstring name(legacy::kOldServiceName);
    SERVICE_TABLE_ENTRYW table[] = {{name.data(), legacy_service_main}, {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(table)) return core::kExitBadArgs;  // SCM disindan: hicbir sey yapilmaz
    return core::kExitOk;
}

// ---- Yeni hizmet temizligi (c) ----
void run_legacy_cleanup(void* stop_event, const std::function<void(const std::string&)>& log) {
    const auto roots = known_roots();
    const auto paths = legacy::make_upgrade_paths(roots, self_exe_path());
    if (paths.new_machine_root.empty() || !file_present(w(paths.record_file()))) return;  // bekleyen yukseltme yok (normal yol)
    // Kayit yalniz SY + BA yazilabilir klasorden okunursa guvenilir (kullanici sahte "healthy" yazamaz).
    if (const auto s = verify_machine_journal_dir(fs::path(w(paths.new_machine_root)) / L"setup"); !s) {
        log("legacy cleanup skipped: record dir ACL not trusted");
        return;
    }
    const auto old_layout = legacy::legacy_layout(roots);
    const auto cur = core::setup::make_layout(roots);
    WinMigrationFs mfs;
    WinUpgradeSystem sys(old_layout, cur, paths, legacy::default_upgrade_names(), version_text());
    legacy::UpgradeEnv env;
    env.role = legacy::UpgradeRole::NewService;
    env.version = to_utf8(version_text());
    env.machine_id = machine_id();
    env.sleep = [](int attempt) { Sleep(static_cast<DWORD>(250 * (attempt + 1))); };
    for (int attempt = 0;; ++attempt) {
        env.now_unix_ms = now_unix_ms();
        const auto r = legacy::run_legacy_upgrade(mfs, sys, paths, legacy::default_upgrade_names(), env);
        log(legacy::upgrade_log_line(r));
        const auto delay = legacy::legacy_cleanup_retry_ms(r.outcome, attempt);
        if (!delay) return;
        if (!stop_event || WaitForSingleObject(static_cast<HANDLE>(stop_event), *delay) != WAIT_TIMEOUT) return;
    }
}

}  // namespace ef::platform
