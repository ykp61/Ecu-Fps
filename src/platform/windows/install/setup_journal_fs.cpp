#include "platform/windows/install/setup_journal_fs.h"

#include <windows.h>

#include <string>

#include "platform/windows/install/secure_dir.h"

namespace ef::platform::install {

using contracts::ErrorCode;
using contracts::Status;

namespace {
constexpr DWORD kMaxJournalBytes = 4u * 1024u * 1024u;

struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};

bool write_all(HANDLE h, const std::string& s) {
    DWORD done = 0;
    return WriteFile(h, s.data(), static_cast<DWORD>(s.size()), &done, nullptr) && done == s.size();
}
}  // namespace

Status FsSetupJournal::ensure_dir() {
    if (file_.empty()) return Status::fail(ErrorCode::MissingValue, "setup journal path");
    if (auto s = ensure_machine_root(machine_dir_); !s) return s;
    return ensure_protected_dir(dir_, kAdminOnlySddl);
}

Status FsSetupJournal::append(const core::setup::JournalEntry& e) {
    if (auto s = ensure_dir(); !s) return s;
    Handle f;
    f.h = CreateFileW(file_.c_str(), GENERIC_READ | FILE_APPEND_DATA, 0, nullptr, OPEN_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (f.h == INVALID_HANDLE_VALUE) return Status::fail(ErrorCode::IoError, "open setup journal (" + std::to_string(GetLastError()) + ")");
    std::string line;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(f.h, &size)) return Status::fail(ErrorCode::IoError, "journal size");
    if (size.QuadPart > kMaxJournalBytes) return Status::fail(ErrorCode::ParseLimit, "setup journal too large");
    if (size.QuadPart > 0) {  // onceki yarim satir (guc kaybi) sonraki kaydi bozmasin
        LARGE_INTEGER pos{};
        pos.QuadPart = size.QuadPart - 1;
        char last = '\n';
        DWORD got = 0;
        if (!SetFilePointerEx(f.h, pos, nullptr, FILE_BEGIN) || !ReadFile(f.h, &last, 1, &got, nullptr) || got != 1)
            return Status::fail(ErrorCode::IoError, "journal tail");
        if (last != '\n') line.push_back('\n');
    }
    line += core::setup::encode_entry(e);
    line.push_back('\n');
    if (!write_all(f.h, line)) return Status::fail(ErrorCode::IoError, "journal write");
    if (!FlushFileBuffers(f.h)) return Status::fail(ErrorCode::IoError, "journal flush");
    return contracts::ok_status();
}

contracts::Result<std::vector<core::setup::JournalEntry>> FsSetupJournal::read() {
    using R = contracts::Result<std::vector<core::setup::JournalEntry>>;
    std::vector<core::setup::JournalEntry> out;
    Handle f;
    f.h = CreateFileW(file_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (f.h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return out;  // journal yok: yarim islem yok
        return R::fail(ErrorCode::IoError, "open setup journal (" + std::to_string(e) + ")");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(f.h, &size)) return R::fail(ErrorCode::IoError, "journal size");
    if (size.QuadPart > kMaxJournalBytes) return R::fail(ErrorCode::ParseLimit, "setup journal too large");
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD got = 0;
    if (!bytes.empty() && (!ReadFile(f.h, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr) || got != bytes.size()))
        return R::fail(ErrorCode::IoError, "journal read");
    std::size_t start = 0;
    while (start < bytes.size()) {
        const std::size_t nl = bytes.find('\n', start);
        const bool terminated = nl != std::string::npos;
        const std::string_view line(bytes.data() + start, (terminated ? nl : bytes.size()) - start);
        start = terminated ? nl + 1 : bytes.size();
        if (line.empty()) continue;
        auto e = core::setup::decode_entry(line);
        if (!e) {
            if (!terminated) break;  // son yarim satir: kayit kalici olmadan guc kesildi (append donmemisti)
            return R::fail(ErrorCode::ParseError, "setup journal line: " + e.error().detail);
        }
        out.push_back(std::move(e).value());
    }
    return out;
}

Status FsSetupJournal::clear() {
    if (!DeleteFileW(file_.c_str())) {
        const DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND)
            return Status::fail(ErrorCode::IoError, "delete setup journal (" + std::to_string(e) + ")");
    }
    (void)RemoveDirectoryW(dir_.c_str());          // bossa
    (void)RemoveDirectoryW(machine_dir_.c_str());  // bossa (kaldirmadan sonra geride klasor kalmaz)
    return contracts::ok_status();
}

}  // namespace ef::platform::install
