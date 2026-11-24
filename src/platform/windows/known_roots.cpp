#include "platform/windows/known_roots.h"

#include <windows.h>
#include <shlobj.h>

#include <string>
#include <vector>

#include "platform/windows/elevation.h"

namespace ef::platform {

namespace {
std::filesystem::path known(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::filesystem::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) out = raw;
    CoTaskMemFree(raw);  // basarisizlikta da cagrilmali (null guvenli)
    return out;
}
}  // namespace

core::setup::KnownRoots known_roots() {
    core::setup::KnownRoots r;
    r.program_files = known(FOLDERID_ProgramFiles);
    r.program_data = known(FOLDERID_ProgramData);
    r.common_programs = known(FOLDERID_CommonPrograms);
    r.common_desktop = known(FOLDERID_PublicDesktop);
    r.local_app_data = known(FOLDERID_LocalAppData);
    return r;
}

std::filesystem::path normalized_self_exe() {
    const auto self = self_exe_path();
    if (self.empty()) return self;
    HANDLE h = CreateFileW(self.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);  // erisim 0: yalniz yol sorgusu, okuma/yazma yok
    if (h == INVALID_HANDLE_VALUE) return self;
    std::vector<wchar_t> buf(MAX_PATH);
    DWORD n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED);
    if (n >= buf.size() && n < 32768) {
        buf.resize(static_cast<std::size_t>(n) + 1);
        n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED);
    }
    CloseHandle(h);
    if (n == 0 || n >= buf.size()) return self;
    std::wstring s(buf.data(), n);
    if (s.starts_with(L"\\\\?\\UNC\\")) s = L"\\\\" + s.substr(8);
    else if (s.starts_with(L"\\\\?\\")) s = s.substr(4);
    return std::filesystem::path(s);
}

}  // namespace ef::platform
