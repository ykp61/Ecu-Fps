#include "platform/windows/crash_handler.h"

#include <windows.h>
#include <dbghelp.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <system_error>
#include <vector>

namespace ef::platform {

namespace {

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

// Filtre icinde bellek ayirmamak icin yol onceden hazirlanir.
wchar_t g_dir[MAX_PATH] = {};
std::size_t g_max = 3;
MiniDumpWriteDumpFn g_write = nullptr;

LONG WINAPI on_unhandled(EXCEPTION_POINTERS* ep) {
    if (!g_write || g_dir[0] == L'\0') return EXCEPTION_CONTINUE_SEARCH;
    SYSTEMTIME st;
    GetSystemTime(&st);
    wchar_t path[MAX_PATH];
    const int n = swprintf_s(path, L"%s\\crash-%04u%02u%02uT%02u%02u%02u-%lu.dmp", g_dir, st.wYear, st.wMonth, st.wDay,
                             st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
    if (n <= 0) return EXCEPTION_CONTINUE_SEARCH;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
    MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
    g_write(GetCurrentProcess(), GetCurrentProcessId(), f,
            static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo), &mei, nullptr, nullptr);
    CloseHandle(f);
    return EXCEPTION_EXECUTE_HANDLER;  // sessizce sonlan (WER penceresi yok)
}

}  // namespace

[[noreturn]] void fatal_worker_exception(const char* what) noexcept {
    // Metin yigindadir (MSVC what() kopyasi) ve MiniDumpNormal yigini yazmaz: istisna kaydina kopyalanir.
    // args[0] = bayt sayisi, args[1..] = metnin ilk (EXCEPTION_MAXIMUM_PARAMETERS-1)*8 bayti.
    ULONG_PTR args[EXCEPTION_MAXIMUM_PARAMETERS] = {};
    const char* s = what ? what : "";
    std::size_t n = std::strlen(s);
    constexpr std::size_t kMax = (EXCEPTION_MAXIMUM_PARAMETERS - 1) * sizeof(ULONG_PTR);
    if (n > kMax) n = kMax;
    args[0] = static_cast<ULONG_PTR>(n);
    std::memcpy(&args[1], s, n);
    const DWORD count = static_cast<DWORD>(1 + (n + sizeof(ULONG_PTR) - 1) / sizeof(ULONG_PTR));
    RaiseException(kWorkerExceptionCode, EXCEPTION_NONCONTINUABLE, count, args);
    TerminateProcess(GetCurrentProcess(), kWorkerExceptionCode);  // filtre kurulu degilse bile devam edilmez
    for (;;) {}
}

void guard_current_thread() noexcept {
    std::set_terminate([] { fatal_worker_exception("std::terminate"); });
}

void prune_dumps(const std::filesystem::path& dir, std::size_t max_dumps) {
    std::error_code ec;
    std::vector<std::filesystem::directory_entry> dumps;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == L".dmp") dumps.push_back(e);
    }
    if (dumps.size() <= max_dumps) return;
    std::sort(dumps.begin(), dumps.end(), [](const auto& a, const auto& b) {
        std::error_code e1, e2;
        return a.last_write_time(e1) > b.last_write_time(e2);
    });
    for (std::size_t i = max_dumps; i < dumps.size(); ++i) std::filesystem::remove(dumps[i].path(), ec);
}

bool install_crash_handler(const CrashConfig& cfg) {
    std::error_code ec;
    std::filesystem::create_directories(cfg.dir, ec);
    const std::wstring d = cfg.dir.wstring();
    if (d.size() + 64 >= MAX_PATH) return false;
    wcscpy_s(g_dir, d.c_str());
    g_max = cfg.max_dumps;
    // Bir sonraki dump icin yer ac: max-1 tut.
    prune_dumps(cfg.dir, g_max > 0 ? g_max - 1 : 0);
    if (!g_write) {
        // DLL planting'e karsi yalniz System32'den yukle.
        HMODULE h = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!h) return false;
        g_write = reinterpret_cast<MiniDumpWriteDumpFn>(reinterpret_cast<void*>(GetProcAddress(h, "MiniDumpWriteDump")));
        if (!g_write) return false;
    }
    SetUnhandledExceptionFilter(on_unhandled);
    return true;
}

}  // namespace ef::platform
