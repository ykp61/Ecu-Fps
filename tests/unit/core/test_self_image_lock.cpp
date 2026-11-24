// Denetim 3.3: setup dosyasi UAC sinirinda kilitli kalir. Kilit varken dosya calistirilabilir (UAC ile yukseltilmis isci
// ayni dosyadan baslar) ama yazilamaz, silinemez, yeniden adlandirilamaz. Kullanicinin dosyalarina dokunmaz: %TEMP%
// altinda whoami.exe kopyasi kullanilir.
#include <windows.h>

#include <filesystem>
#include <string>

#include "ef_test.h"
#include "platform/windows/elevation.h"

using namespace ef::platform;
namespace fs = std::filesystem;

namespace {
fs::path temp_copy() {
    wchar_t tmp[MAX_PATH + 1] = {};
    GetTempPathW(MAX_PATH, tmp);
    const fs::path dir = fs::path(tmp) / (L"ef_lock_test_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::create_directories(dir, ec);
    wchar_t sys[MAX_PATH + 1] = {};
    GetSystemDirectoryW(sys, MAX_PATH);
    const fs::path dst = dir / L"locked.exe";
    fs::copy_file(fs::path(sys) / L"whoami.exe", dst, fs::copy_options::overwrite_existing, ec);
    return ec ? fs::path{} : dst;
}
}  // namespace

EF_TEST(self_image_lock_blocks_replace_but_allows_run) {
    const auto exe = temp_copy();
    EF_CHECK(!exe.empty());
    if (exe.empty()) return;
    {
        auto lock = SelfImageLock::acquire(exe);
        EF_CHECK(lock.ok());
        // Yazma ve yeniden adlandirma / silme engelli.
        HANDLE w = CreateFileW(exe.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        EF_CHECK(w == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION);
        if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
        const auto moved = exe.parent_path() / L"moved.exe";
        EF_CHECK(!MoveFileExW(exe.c_str(), moved.c_str(), 0));
        EF_CHECK(!DeleteFileW(exe.c_str()));
        // Kilitli dosya yine calistirilabilir (yukseltilmis isci ayni yoldan baslar).
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + exe.wstring() + L"\" /?";
        const BOOL started = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        EF_CHECK(started != FALSE);
        if (started) {
            EF_CHECK(WaitForSingleObject(pi.hProcess, 10000) == WAIT_OBJECT_0);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    // Kilit kalkinca dosya yeniden silinebilir.
    EF_CHECK(DeleteFileW(exe.c_str()) != FALSE);
    std::error_code ec;
    fs::remove_all(exe.parent_path(), ec);
}

EF_TEST(self_image_lock_missing_file_fails) {
    auto lock = SelfImageLock::acquire(L"C:\\ef-definitely-missing\\nope.exe");
    EF_CHECK(!lock.ok());
    EF_CHECK(lock.error() != 0);
}

// Setup'taki gercek durum: calisan surec KENDI goruntusunu kilitler (yukleyici tutamaci ile uyumlu olmali).
EF_TEST(self_image_lock_running_exe_locks_itself) {
    auto lock = SelfImageLock::acquire({});
    EF_CHECK(lock.ok());
    const auto self = self_exe_path();
    HANDLE d = CreateFileW(self.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    EF_CHECK(d == INVALID_HANDLE_VALUE);  // yeniden adlandirma icin DELETE erisimi alinamaz
    if (d != INVALID_HANDLE_VALUE) CloseHandle(d);
}
