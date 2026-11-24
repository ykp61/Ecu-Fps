#include "platform/windows/games/upscaler_scan.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/windows/win_env.h"

namespace ef::platform::games {

namespace fs = std::filesystem;

namespace {

constexpr int kMaxDepth = 3;
constexpr std::size_t kMaxAssets = 24;

std::string file_version(const fs::path& p) {
    const std::wstring w = p.wstring();
    DWORD dummy = 0;
    const DWORD size = GetFileVersionInfoSizeW(w.c_str(), &dummy);
    if (size == 0) return {};
    std::vector<unsigned char> buf(size);
    if (!GetFileVersionInfoW(w.c_str(), 0, size, buf.data())) return {};
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info || len < sizeof(*info))
        return {};
    const DWORD ms = info->dwFileVersionMS, ls = info->dwFileVersionLS;
    if (ms == 0 && ls == 0) return {};
    return std::to_string(HIWORD(ms)) + "." + std::to_string(LOWORD(ms)) + "." + std::to_string(HIWORD(ls)) + "." +
           std::to_string(LOWORD(ls));
}

}  // namespace

UpscalerScan scan_upscalers(const std::string& install_dir, std::size_t max_entries) {
    UpscalerScan out;
    if (install_dir.empty() || max_entries == 0) return out;
    std::error_code ec;
    const fs::path root = fs::path(to_wide(install_dir));
    if (!fs::is_directory(root, ec) || ec) return out;
    out.scanned = true;

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) {
        out.scanned = false;
        return out;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (++out.files_seen > max_entries) {
            out.truncated = true;
            break;
        }
        if (it.depth() >= kMaxDepth) it.disable_recursion_pending();
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        const std::string name = to_utf8(it->path().filename().wstring());
        const auto kind = core::games::kind_from_filename(name);
        if (!kind) continue;
        if (out.assets.size() >= kMaxAssets) {
            out.truncated = true;
            break;
        }
        std::error_code rel_ec;
        const fs::path rel = fs::relative(it->path(), root, rel_ec);
        out.assets.push_back({*kind, to_utf8((rel_ec ? it->path().filename() : rel).wstring()), file_version(it->path())});
    }
    std::sort(out.assets.begin(), out.assets.end(), [](const auto& a, const auto& b) {
        if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        return a.file < b.file;
    });
    return out;
}

}  // namespace ef::platform::games
