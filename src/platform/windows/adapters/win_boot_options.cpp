#include "platform/windows/adapters/win_boot_options.h"

#include <windows.h>

#include <string>

namespace ef::platform::adapters {

using contracts::ErrorCode;
using contracts::Result;
using contracts::Status;
using core::adapters::windows::BootElement;

namespace {

// Derlenmis sabit argumanlar (Microsoft BCDEdit /set ve /deletevalue belgesi). Baska komut kurulamaz.
constexpr wchar_t kSetOff[] = L"/set {current} disabledynamictick yes";
constexpr wchar_t kSetOn[] = L"/set {current} disabledynamictick no";
constexpr wchar_t kDelete[] = L"/deletevalue {current} disabledynamictick";
constexpr DWORD kTimeoutMs = 30000;

Status run_bcdedit(const wchar_t* fixed_args) {
    wchar_t sys[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return Status::fail(ErrorCode::IoError, "system directory unavailable");
    const std::wstring exe = std::wstring(sys, n) + L"\\bcdedit.exe";
    std::wstring cmd = L"\"" + exe + L"\" " + fixed_args;  // CreateProcessW yazilabilir tampon ister
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, sys, &si, &pi))
        return Status::fail(ErrorCode::IoError, "bcdedit start failed (" + std::to_string(GetLastError()) + ")");
    CloseHandle(pi.hThread);
    const DWORD w = WaitForSingleObject(pi.hProcess, kTimeoutMs);
    DWORD code = 1;
    const bool got = w == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (w != WAIT_OBJECT_0) return Status::fail(ErrorCode::IoError, "bcdedit timed out");  // kesilmez; sonuc okumayla dogrulanir
    if (!got) return Status::fail(ErrorCode::IoError, "bcdedit exit code unavailable");
    if (code != 0) return Status::fail(ErrorCode::IoError, "bcdedit exit code " + std::to_string(code));
    return contracts::ok_status();
}

}  // namespace

Result<std::optional<std::string>> WinBootOptionApi::read(BootElement) {
    return Result<std::optional<std::string>>::fail(ErrorCode::Unsupported, "BCD read path not verified (R-B4 adim 0)");
}

Status WinBootOptionApi::write(BootElement e, std::string_view value) {
    if (e != BootElement::DisableDynamicTick) return Status::fail(ErrorCode::Denied, "boot element not in table");
    if (value == core::adapters::windows::kBootDynamicTickOff) return run_bcdedit(kSetOff);
    if (value == core::adapters::windows::kBootDynamicTickOn) return run_bcdedit(kSetOn);
    return Status::fail(ErrorCode::OutOfRange, "boot value must be on/off");
}

Status WinBootOptionApi::remove(BootElement e) {
    if (e != BootElement::DisableDynamicTick) return Status::fail(ErrorCode::Denied, "boot element not in table");
    return run_bcdedit(kDelete);
}

}  // namespace ef::platform::adapters
