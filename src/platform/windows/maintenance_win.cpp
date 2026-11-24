#include "platform/windows/maintenance_win.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <cmath>

#include "platform/windows/restore_point_win.h"

namespace ef::platform {

namespace cm = core::maintenance;
using contracts::ErrorCode;

namespace {

// DISM API (dismapi.h) tanimlari: resmi belgeye gore. Yalniz kullanilan girisler.
using DismSession = UINT;
using ProgressFn = void(CALLBACK*)(UINT current, UINT total, PVOID user);
using InitFn = HRESULT(WINAPI*)(int log_level, PCWSTR log_path, PCWSTR scratch);
using OpenFn = HRESULT(WINAPI*)(PCWSTR image_path, PCWSTR windows_dir, PCWSTR system_drive, DismSession* session);
using CheckFn = HRESULT(WINAPI*)(DismSession, BOOL scan_image, HANDLE cancel, ProgressFn, PVOID, int* health);
using RestoreFn = HRESULT(WINAPI*)(DismSession, PCWSTR* sources, UINT count, BOOL limit_access, HANDLE cancel, ProgressFn, PVOID);
using CloseFn = HRESULT(WINAPI*)(DismSession);
using ShutdownFn = HRESULT(WINAPI*)();
constexpr wchar_t kOnlineImage[] = L"DISM_{53BFAE52-B167-4E2F-A258-0A37B57FF845}";
constexpr int kLogErrors = 0;

template <class F>
F sym(void* m, const char* name) {
    return reinterpret_cast<F>(GetProcAddress(static_cast<HMODULE>(m), name));
}

}  // namespace

WinImageServicing::~WinImageServicing() {
    if (session_open_)
        if (auto close = sym<CloseFn>(mod_, "DismCloseSession")) close(session_);
    if (initialized_)
        if (auto shut = sym<ShutdownFn>(mod_, "DismShutdown")) shut();
}

constexpr long kNoApi = static_cast<long>(0x80004001L);  // E_NOTIMPL: kutuphane/ihracat yok

long WinImageServicing::open() {
    if (session_open_) return 0;
    if (!mod_) mod_ = LoadLibraryExW(L"dismapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);  // calisma klasorunden yuklenmez
    if (!mod_) return kNoApi;
    const auto init = sym<InitFn>(mod_, "DismInitialize");
    const auto open_session = sym<OpenFn>(mod_, "DismOpenSession");
    if (!init || !open_session) return kNoApi;
    if (!initialized_) {
        if (const HRESULT hr = init(kLogErrors, nullptr, nullptr); FAILED(hr)) return hr;  // gecici (servis mesgul vb.): desteklenmiyor DEGIL
        initialized_ = true;
    }
    const HRESULT hr = open_session(kOnlineImage, nullptr, nullptr, &session_);
    session_open_ = SUCCEEDED(hr);
    return session_open_ ? 0 : hr;
}

bool WinImageServicing::create_restore_point() {
    if (!rp_jobs_) return false;
    WinRestorePointApi api;
    const auto r = core::recovery::run_restore_point(api, *rp_jobs_, "Ecu Fps");
    return r.outcome == core::recovery::RestorePointOutcome::Created || r.outcome == core::recovery::RestorePointOutcome::SkippedByFrequency;
}

std::optional<bool> WinImageServicing::pending_reboot() {
    HKEY k = nullptr;
    const LSTATUS st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending", 0,
                                     KEY_READ, &k);
    if (st == ERROR_FILE_NOT_FOUND) return false;
    if (st != ERROR_SUCCESS) return std::nullopt;
    RegCloseKey(k);
    return true;
}

contracts::Result<cm::ImageHealth> WinImageServicing::check(bool scan) {
    using R = contracts::Result<cm::ImageHealth>;
    if (const long st = open(); st != 0)
        return R::fail(st == kNoApi ? ErrorCode::Unsupported : ErrorCode::IoError, "dism open " + std::to_string(static_cast<unsigned long>(st)));
    const auto fn = sym<CheckFn>(mod_, "DismCheckImageHealth");
    if (!fn) return R::fail(ErrorCode::Unsupported, "DismCheckImageHealth missing");
    int health = -1;
    const HRESULT hr = fn(session_, scan ? TRUE : FALSE, nullptr, nullptr, nullptr, &health);
    if (FAILED(hr)) return R::fail(ErrorCode::IoError, "DismCheckImageHealth " + std::to_string(static_cast<unsigned long>(hr)));
    switch (health) {
        case 0: return cm::ImageHealth::Healthy;
        case 1: return cm::ImageHealth::Repairable;
        case 2: return cm::ImageHealth::NonRepairable;
        default: return R::fail(ErrorCode::ParseError, "unknown health state");
    }
}

std::uint32_t WinImageServicing::restore_local_only() {
    if (const long st = open(); st != 0) return static_cast<std::uint32_t>(st);
    const auto fn = sym<RestoreFn>(mod_, "DismRestoreImageHealth");
    if (!fn) return 0x80004001u;
    // LimitAccess=TRUE: Windows Update'ten indirme yok (ayri onay kapsami).
    return static_cast<std::uint32_t>(fn(session_, nullptr, 0, TRUE, nullptr, nullptr, nullptr));
}

contracts::Result<std::uint64_t> read_standby_bytes() {
    using R = contracts::Result<std::uint64_t>;
    static const wchar_t* const kCounters[] = {L"\\Memory\\Standby Cache Normal Priority Bytes", L"\\Memory\\Standby Cache Reserve Bytes",
                                               L"\\Memory\\Standby Cache Core Bytes"};
    PDH_HQUERY q = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) return R::fail(ErrorCode::IoError, "pdh open");
    PDH_HCOUNTER h[3] = {};
    for (int i = 0; i < 3; ++i)
        if (PdhAddEnglishCounterW(q, kCounters[i], 0, &h[i]) != ERROR_SUCCESS || !h[i]) {
            PdhCloseQuery(q);
            return R::fail(ErrorCode::Unsupported, "pdh counter");
        }
    double sum = 0;
    bool ok = PdhCollectQueryData(q) == ERROR_SUCCESS;
    for (int i = 0; ok && i < 3; ++i) {
        PDH_FMT_COUNTERVALUE v{};
        ok = PdhGetFormattedCounterValue(h[i], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &v) == ERROR_SUCCESS &&
             (v.CStatus == PDH_CSTATUS_VALID_DATA || v.CStatus == PDH_CSTATUS_NEW_DATA) && std::isfinite(v.doubleValue) && v.doubleValue >= 0.0;
        if (ok) sum += v.doubleValue;
    }
    PdhCloseQuery(q);
    if (!ok) return R::fail(ErrorCode::IoError, "pdh read");
    return static_cast<std::uint64_t>(sum);
}

contracts::Status WinMemoryList::purge_standby() {
    // SeProfileSingleProcessPrivilege (yonetici belirtecinde var, varsayilan kapali): acikca etkinlestirilir.
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
        return contracts::Status::fail(ErrorCode::Denied, "open token");
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const bool have = LookupPrivilegeValueW(nullptr, L"SeProfileSingleProcessPrivilege", &tp.Privileges[0].Luid) &&
                      AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(tok);
    if (!have) return contracts::Status::fail(ErrorCode::Denied, "privilege");
    using NtSetFn = LONG(NTAPI*)(ULONG info_class, PVOID info, ULONG len);
    const HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    const auto fn = nt ? reinterpret_cast<NtSetFn>(GetProcAddress(nt, "NtSetSystemInformation")) : nullptr;
    if (!fn) return contracts::Status::fail(ErrorCode::Unsupported, "NtSetSystemInformation missing");
    constexpr ULONG kSystemMemoryListInformation = 80;
    ULONG command = 4;  // MemoryPurgeStandbyList
    const LONG st = fn(kSystemMemoryListInformation, &command, sizeof(command));
    if (st < 0) return contracts::Status::fail(ErrorCode::IoError, "NtSetSystemInformation " + std::to_string(static_cast<unsigned long>(st)));
    return contracts::ok_status();
}

}  // namespace ef::platform
