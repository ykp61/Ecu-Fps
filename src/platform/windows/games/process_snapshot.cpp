#include "platform/windows/games/process_snapshot.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwchar>
#include <string>

namespace ef::platform::games {

namespace {
std::string narrow_lower(const wchar_t* w, DWORD n) {
    if (n == 0) return {};
    const int m = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    if (m <= 0) return {};
    std::string s(static_cast<std::size_t>(m), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), s.data(), m, nullptr, nullptr);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return c < 0x80 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c); });
    return s;
}
}  // namespace

core::games::ProcessSnapshot snapshot_processes() {
    core::games::ProcessSnapshot out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;  // Failed: "oyun yok" denmez

    PROCESSENTRY32W pe{sizeof(pe)};
    if (!Process32FirstW(snap, &pe)) {
        // Bos surec listesi Windows'ta normal degildir; numaralandirma hatasi guvenli tarafta Failed.
        CloseHandle(snap);
        return out;
    }
    DWORD step = ERROR_SUCCESS;
    do {
        if (pe.th32ProcessID != 0 && pe.th32ProcessID != 4) {
            core::games::ProcessInfo pi;
            pi.pid = pe.th32ProcessID;
            pi.exe_name = narrow_lower(pe.szExeFile, static_cast<DWORD>(std::wcslen(pe.szExeFile)));
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (h) {
                wchar_t path[MAX_PATH * 2];
                DWORD n = static_cast<DWORD>(std::size(path));
                if (QueryFullProcessImageNameW(h, 0, path, &n)) {
                    pi.exe_path = narrow_lower(path, n);
                    pi.path_readable = !pi.exe_path.empty();
                }
                FILETIME c{}, e{}, k{}, u{};
                if (GetProcessTimes(h, &c, &e, &k, &u))
                    pi.create_time = (static_cast<std::uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
                CloseHandle(h);
            }
            if (!pi.path_readable) ++out.inaccessible_count;
            out.processes.push_back(std::move(pi));
        }
        if (!Process32NextW(snap, &pe)) {
            step = GetLastError();
            break;
        }
    } while (true);
    CloseHandle(snap);
    // ERROR_NO_MORE_FILES normal bitistir. Baska hata: liste yarim kalmis olabilir -> Failed.
    out.state = step == ERROR_NO_MORE_FILES ? core::games::DetectionState::Ok : core::games::DetectionState::Failed;
    return out;
}

}  // namespace ef::platform::games
