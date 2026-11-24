// F-79: Toolhelp surec anlik goruntusu + tam yol; yalniz exe basename dondurulur.
// Sureclere yazma/enjeksiyon yok; erisilemeyen yol atlanir (yanlis eslesme uretilmez).
#include "platform/windows/readiness/background_reader.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <iterator>

namespace ef::platform::readiness {

namespace {

std::string lower_basename(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    std::string out;
    out.reserve(name.size());
    for (const wchar_t w : name) {
        if (w < 0x80) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(w))));
    }
    return out;
}

std::string exe_name_for(DWORD pid) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return {};
    wchar_t buf[32768] = {};
    DWORD n = static_cast<DWORD>(std::size(buf));
    std::string out;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) out = lower_basename(std::wstring(buf, n));
    CloseHandle(p);
    return out;
}

}  // namespace

std::vector<std::string> read_running_exe_names() {
    std::vector<std::string> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            auto name = exe_name_for(pe.th32ProcessID);
            if (!name.empty()) out.push_back(std::move(name));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace ef::platform::readiness
