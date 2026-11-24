#include "platform/windows/single_instance.h"

#include <windows.h>

#include "platform/windows/elevation.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/run_key_store.h"
#include "platform/windows/win_env.h"

namespace ef::platform {

using contracts::ErrorCode;

namespace {
// Kullanici SID'i ile adlandir: ayni oturumda farkli kullanici (runas) karismasin.
std::wstring instance_mutex_name() {
    const auto u = inventory::read_user();
    return L"Local\\EcuFps.SingleInstance." + to_wide(u.process_user_sid.value.value_or("unknown"));
}
}  // namespace

bool ui_instance_running() {
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, instance_mutex_name().c_str());
    if (h) {
        CloseHandle(h);
        return true;
    }
    return GetLastError() != ERROR_FILE_NOT_FOUND;
}

std::unique_ptr<SingleInstance> SingleInstance::acquire(bool& already_running, const std::wstring& name_in) {
    const std::wstring name = name_in.empty() ? instance_mutex_name() : name_in;
    HANDLE h = CreateMutexW(nullptr, TRUE, name.c_str());
    already_running = h && GetLastError() == ERROR_ALREADY_EXISTS;
    if (!h) return nullptr;
    if (already_running) {
        CloseHandle(h);
        return nullptr;
    }
    return std::unique_ptr<SingleInstance>(new SingleInstance(h));
}

std::unique_ptr<SingleInstance> SingleInstance::wait_acquire(unsigned timeout_ms, const std::wstring& name_in) {
    const std::wstring name = name_in.empty() ? instance_mutex_name() : name_in;
    HANDLE h = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!h) return nullptr;
    // WAIT_ABANDONED: onceki surec kilidi birakmadan sonlandi (ornek: cikista sonlandirma); kilit yine bu ornegin olur.
    const DWORD w = WaitForSingleObject(h, timeout_ms);
    if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) return std::unique_ptr<SingleInstance>(new SingleInstance(h));
    CloseHandle(h);
    return nullptr;
}

SingleInstance::~SingleInstance() {
    if (h_) {
        ReleaseMutex(h_);
        CloseHandle(h_);
    }
}

unsigned int activate_message_id() {
    static const UINT id = RegisterWindowMessageW(kActivateMessageName);
    return id;
}

core::ActivateTry activate_window(HWND w, unsigned timeout_ms) {
    if (!w) return core::ActivateTry::NoWindow;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    AllowSetForegroundWindow(pid);  // on plan hakkini ilk ornege devret
    // B2 / bosluk 4: PostMessage olu (cikmakta olan, mesaj dongusu bitmis) pencereye de "basarili" donerdi ve hicbir sey
    // acilmazdi. SendMessageTimeout yalniz alici mesaji gercekten isleyince basarilidir; alici isleyiciyi kuyruga atip
    // hemen doner (App::host). Yalniz bilinen yanit (kActivateReply) aktivasyon sayilir: mesaji tanimayan pencere
    // (DefWindowProc 0) cikmakta olan ornegindir.
    DWORD_PTR r = 0;
    SetLastError(ERROR_SUCCESS);
    const bool sent = SendMessageTimeoutW(w, activate_message_id(), 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, timeout_ms, &r) != 0;
    const bool denied = !sent && GetLastError() == ERROR_ACCESS_DENIED;  // UIPI: yonetici ornek, filtre yok
    return core::classify_activate_reply(sent, static_cast<std::uintptr_t>(r), denied);
}

core::ActivateTry try_activate_existing_instance(unsigned timeout_ms) {
    // Aktivasyonu yalniz kalici gizli ana pencere (host) yanitlar; arayuz penceresi (MainWindow) mesaji tanimaz. Host cikis
    // basinda yok edilir; cikmakta olan surecin gizli arayuz penceresine dusmek yerine NoWindow donulur ve kilit beklenir.
    return activate_window(FindWindowW(kHostWindowClass, nullptr), timeout_ms);
}

core::RelaunchOutcome resolve_second_launch(std::unique_ptr<SingleInstance>& out) {
    core::RelaunchOps ops;
    ops.activate = [] { return try_activate_existing_instance(); };
    ops.wait_lock = [&out](std::uint32_t ms) {
        out = SingleInstance::wait_acquire(ms);
        return out != nullptr;
    };
    ops.now_ms = [] { return static_cast<std::uint64_t>(GetTickCount64()); };
    return core::resolve_second_launch(ops);
}

std::wstring autostart_command() { return core::settings::autostart_command(self_exe_path().wstring()); }

// Windows testleri (scope_probe / uninstall_probe) icin ince sarmalayicilar: ayni WinRunKeyStore + geri okuma.
contracts::Status set_autostart(bool enabled, const std::wstring& run_key) {
    WinRunKeyStore store(run_key);
    core::settings::Preferences p;
    const auto s = core::settings::autostart_set(store, p, enabled, autostart_command());
    return s.failure == core::settings::AutostartFailure::None ? contracts::ok_status()
                                                               : contracts::Status::fail(ErrorCode::IoError, "Run value write");
}

contracts::Result<bool> autostart_enabled(const std::wstring& run_key) {
    WinRunKeyStore store(run_key);
    const auto s = core::settings::autostart_read(store, autostart_command());
    if (!s.known) return contracts::Result<bool>::fail(ErrorCode::IoError, "Run value read");
    return s.on;  // baska konuma tasinmis eski EXE'yi gosteren kayit "acik" sayilmaz
}

}  // namespace ef::platform
