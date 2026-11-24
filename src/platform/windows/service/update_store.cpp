#include "platform/windows/service/update_store.h"

#include <windows.h>

#include <algorithm>
#include <optional>

#include "core/recovery/sha256.h"
#include "core/update/update_check.h"
#include "platform/windows/win_env.h"

namespace ef::platform::service {

namespace {

constexpr DWORD kIoSlice = 1u << 20;

std::optional<std::string> read_all(const std::wstring& path, std::uint64_t max_bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    std::optional<std::string> out;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart >= 0 && static_cast<std::uint64_t>(size.QuadPart) <= max_bytes) {
        std::string data(static_cast<std::size_t>(size.QuadPart), '\0');
        std::size_t off = 0;
        bool ok = true;
        while (ok && off < data.size()) {
            const DWORD n = static_cast<DWORD>(std::min<std::size_t>(data.size() - off, kIoSlice));
            DWORD got = 0;
            ok = ReadFile(h, data.data() + off, n, &got, nullptr) && got == n;
            off += got;
        }
        if (ok) out = std::move(data);
    }
    CloseHandle(h);
    return out;
}

bool write_all(HANDLE h, std::string_view data) {
    std::size_t off = 0;
    while (off < data.size()) {
        const DWORD n = static_cast<DWORD>(std::min<std::size_t>(data.size() - off, kIoSlice));
        DWORD wrote = 0;
        if (!WriteFile(h, data.data() + off, n, &wrote, nullptr) || wrote != n) return false;
        off += wrote;
    }
    return true;
}

}  // namespace

// ---- FileStageSink ----

FileStageSink::~FileStageSink() { close(); }

void FileStageSink::close() {
    if (h_) CloseHandle(static_cast<HANDLE>(h_));
    h_ = nullptr;
}

bool FileStageSink::open_part(std::string_view version) {
    close();
    path_ = dir_ / (to_wide(version) + L".part");
    // Paylasimsiz: aktarim sirasinda baska hicbir surec dosyayi acamaz/degistiremez.
    HANDLE h = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    h_ = h;
    return true;
}

bool FileStageSink::write(std::string_view bytes) { return h_ && write_all(static_cast<HANDLE>(h_), bytes); }

bool FileStageSink::finish() {
    if (!h_) return false;
    const bool ok = FlushFileBuffers(static_cast<HANDLE>(h_)) != 0;
    close();
    return ok;
}

std::string FileStageSink::sha256_hex_readback() {
    if (path_.empty()) return {};
    const auto data = read_all(path_.wstring(), core::update::kExeMaxBytes);
    return data ? core::recovery::sha256_hex(*data) : std::string();
}

void FileStageSink::discard() {
    close();
    if (!path_.empty()) (void)DeleteFileW(path_.c_str());
}

// ---- FileUpdateHost ----

bool FileUpdateHost::load_state(core::update::StagedUpdateState& out) {
    const auto data = read_all(state_.wstring(), 4096);
    if (!data) return false;
    auto d = core::update::decode_state(*data);
    if (!d.ok()) return false;
    out = d.value();
    return true;
}

bool FileUpdateHost::save_state(const core::update::StagedUpdateState& s) {
    const std::wstring tmp = state_.wstring() + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = write_all(h, core::update::encode_state(s)) && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), state_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        (void)DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool FileUpdateHost::stage_part(const std::string& staged) {
    const auto& part = sink_.part_path();
    if (part.empty()) return false;
    const auto data = read_all(part.wstring(), core::update::kExeMaxBytes);
    if (!data) return false;
    const std::wstring dst = to_wide(staged);
    HANDLE h = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = write_all(h, *data) && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) (void)DeleteFileW(dst.c_str());
    return ok;
}

bool FileUpdateHost::file_digest(const std::string& path, std::uint64_t& size, std::string& sha256_hex) {
    const auto data = read_all(to_wide(path), core::update::kExeMaxBytes);
    if (!data) return false;
    size = data->size();
    sha256_hex = core::recovery::sha256_hex(*data);
    return true;
}

void FileUpdateHost::remove_part() {
    // Bu hizmetin depo klasorundeki tum *.part kalintilari (onceki yarim aktarimlar dahil).
    const auto dir = state_.parent_path();
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileW((dir / L"*.part").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) (void)DeleteFileW((dir / fd.cFileName).c_str());
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

std::int64_t FileUpdateHost::now_ms() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    const std::uint64_t t = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<std::int64_t>((t - 116444736000000000ull) / 10000ull);
}

core::catalog::SwapFs win_swap_fs() {
    core::catalog::SwapFs fs;
    fs.move = [](const std::string& from, const std::string& to) {
        return MoveFileExW(to_wide(from).c_str(), to_wide(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    };
    fs.exists = [](const std::string& p) { return GetFileAttributesW(to_wide(p).c_str()) != INVALID_FILE_ATTRIBUTES; };
    fs.remove = [](const std::string& p) { return DeleteFileW(to_wide(p).c_str()) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND; };
    return fs;
}

}  // namespace ef::platform::service
