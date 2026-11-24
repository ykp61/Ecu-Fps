#include "platform/windows/readiness/system_reader.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <winioctl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "platform/windows/win_env.h"

namespace ef::platform::readiness {

namespace {

constexpr DWORD kFmt = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;
constexpr std::uint64_t kMib = 1024ull * 1024ull;
constexpr std::uint64_t kPagefileMarginBytes = 256ull * kMib;

const wchar_t* const kStandby = L"\\Memory\\Standby Cache Normal Priority Bytes";
const wchar_t* const kPagingFile = L"\\Paging File(_Total)\\% Usage";

std::optional<double> read_counter(PDH_HQUERY q, const wchar_t* path) {
    PDH_HCOUNTER c = nullptr;
    if (PdhAddEnglishCounterW(q, path, 0, &c) != ERROR_SUCCESS || !c) return std::nullopt;
    if (PdhCollectQueryData(q) != ERROR_SUCCESS) return std::nullopt;  // ilk okuma oncesi zorunlu
    PDH_FMT_COUNTERVALUE v{};
    if (PdhGetFormattedCounterValue(c, kFmt, nullptr, &v) != ERROR_SUCCESS) return std::nullopt;
    if (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA) return std::nullopt;
    if (!std::isfinite(v.doubleValue) || v.doubleValue < 0.0) return std::nullopt;
    return v.doubleValue;
}

std::optional<double> memory_compression_mib() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return std::nullopt;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (std::wcscmp(pe.szExeFile, L"Memory Compression") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (pid == 0) return std::nullopt;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return std::nullopt;
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    const bool ok = K32GetProcessMemoryInfo(p, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)) != FALSE;
    CloseHandle(p);
    if (!ok) return std::nullopt;
    const double mib = static_cast<double>(pmc.WorkingSetSize) / static_cast<double>(kMib);
    if (!std::isfinite(mib) || mib <= 0.0) return std::nullopt;
    return mib;
}

std::optional<double> system_drive_free_ratio(std::wstring& root) {
    wchar_t win_dir[MAX_PATH] = {};
    const UINT n = GetWindowsDirectoryW(win_dir, MAX_PATH);
    if (n < 3 || n >= MAX_PATH || win_dir[1] != L':') return std::nullopt;
    root.assign(win_dir, 3);  // "C:\"
    DWORD old_mode = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS, &old_mode);
    ULARGE_INTEGER freeb{}, total{};
    const bool ok = GetDiskFreeSpaceExW(root.c_str(), &freeb, &total, nullptr) != FALSE;
    SetThreadErrorMode(old_mode, nullptr);
    if (!ok || total.QuadPart == 0 || freeb.QuadPart > total.QuadPart) return std::nullopt;
    return static_cast<double>(freeb.QuadPart) / static_cast<double>(total.QuadPart);
}

std::optional<bool> trim_enabled(const std::wstring& root) {
    if (root.size() < 2) return std::nullopt;
    std::wstring volume = L"\\\\.\\" + root.substr(0, 2);  // "\\.\C:"
    HANDLE h = CreateFileW(volume.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceTrimProperty;
    q.QueryType = PropertyStandardQuery;
    DEVICE_TRIM_DESCRIPTOR d{};
    DWORD bytes = 0;
    const BOOL ok = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &d, sizeof(d), &bytes, nullptr);
    CloseHandle(h);
    if (!ok || bytes < sizeof(d)) return std::nullopt;
    return d.TrimEnabled != FALSE;
}

}  // namespace

core::readiness::SystemHealthInfo read_system_health() {
    core::readiness::SystemHealthInfo s;

    s.uptime_s = static_cast<std::uint64_t>(GetTickCount64() / 1000ull);

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        if (ms.ullTotalPageFile > 0 && ms.ullAvailPageFile <= ms.ullTotalPageFile) {
            const double used = static_cast<double>(ms.ullTotalPageFile - ms.ullAvailPageFile);
            s.commit_used_pct = 100.0 * used / static_cast<double>(ms.ullTotalPageFile);
        }
        if (ms.ullTotalPhys > 0)
            s.pagefile_configured = ms.ullTotalPageFile > ms.ullTotalPhys + kPagefileMarginBytes;
    }

    PERFORMANCE_INFORMATION pi{};
    if (GetPerformanceInfo(&pi, sizeof(pi)) && pi.PageSize > 0) {
        const double page = static_cast<double>(pi.PageSize);
        s.nonpaged_pool_mib = static_cast<double>(pi.KernelNonpaged) * page / static_cast<double>(kMib);
    }

    PDH_HQUERY q = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &q) == ERROR_SUCCESS) {
        const auto standby = read_counter(q, kStandby);
        const auto pagefile = read_counter(q, kPagingFile);
        if (standby) s.standby_mib = *standby / static_cast<double>(kMib);
        if (pagefile) s.pagefile_usage_pct = std::clamp(*pagefile, 0.0, 100.0);
        PdhCloseQuery(q);
    }

    s.compressed_mib = memory_compression_mib();

    std::wstring root;
    s.system_drive_free_ratio = system_drive_free_ratio(root);
    if (!root.empty()) s.trim_enabled = trim_enabled(root);

    return s;
}

}  // namespace ef::platform::readiness
