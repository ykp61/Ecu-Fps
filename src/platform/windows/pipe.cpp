#include "platform/windows/pipe.h"

#include <windows.h>
#include <sddl.h>

#include <optional>

#include "platform/windows/win_env.h"

namespace ef::platform {

using contracts::ErrorCode;
using contracts::Status;

std::wstring pipe_name_for(const std::string& nonce) { return L"\\\\.\\pipe\\EcuFps." + to_wide(nonce); }

namespace {
std::string err(const char* what, DWORD e) { return std::string(what) + " (" + std::to_string(e) + ")"; }

bool valid_sid_text(const std::string& s) {
    if (s.size() < 5 || s.size() > 184 || s.rfind("S-1-", 0) != 0) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || c == '-' || c == 'S')) return false;
    return true;
}

// Yol karsilastirmasi: Windows dosya adlari harf duyarsizdir; yerel ayardan bagimsiz ASCII katlama.
bool same_image(std::wstring_view a, std::wstring_view b) {
    if (a.empty() || b.empty() || a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        wchar_t x = a[i], y = b[i];
        if (x >= L'A' && x <= L'Z') x = static_cast<wchar_t>(x + (L'a' - L'A'));
        if (y >= L'A' && y <= L'Z') y = static_cast<wchar_t>(y + (L'a' - L'A'));
        if (x != y) return false;
    }
    return true;
}

std::optional<std::uint64_t> process_create_time(HANDLE h) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(h, &creation, &exit, &kernel, &user)) return std::nullopt;
    ULARGE_INTEGER v;
    v.LowPart = creation.dwLowDateTime;
    v.HighPart = creation.dwHighDateTime;
    if (v.QuadPart == 0) return std::nullopt;
    return v.QuadPart;
}
}  // namespace

bool peer_matches(const PeerIdentity& peer, std::uint32_t expected_pid, std::uint64_t expected_create_time,
                  std::uint32_t expected_session, std::wstring_view expected_image) {
    if (expected_pid == 0 || expected_create_time == 0 || expected_image.empty()) return false;
    return peer.pid == expected_pid && peer.create_time == expected_create_time && peer.session == expected_session &&
           same_image(peer.image, expected_image);
}

PipeEnd::~PipeEnd() {
    if (h_ && h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
}

Status PipeEnd::io(bool write, char* buf, std::uint32_t len, std::uint32_t timeout_ms) {
    std::uint32_t done_total = 0;
    while (done_total < len) {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return Status::fail(ErrorCode::IoError, err("CreateEvent", GetLastError()));
        DWORD n = 0;
        BOOL ok = write ? WriteFile(h_, buf + done_total, len - done_total, nullptr, &ov)
                        : ReadFile(h_, buf + done_total, len - done_total, nullptr, &ov);
        DWORD e = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && e == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ov.hEvent, timeout_ms) != WAIT_OBJECT_0) {
                CancelIoEx(h_, &ov);
                GetOverlappedResult(h_, &ov, &n, TRUE);
                CloseHandle(ov.hEvent);
                return Status::fail(ErrorCode::IoError, "timeout");
            }
            ok = GetOverlappedResult(h_, &ov, &n, FALSE);
            e = ok ? ERROR_SUCCESS : GetLastError();
        } else if (ok) {
            GetOverlappedResult(h_, &ov, &n, FALSE);
        }
        CloseHandle(ov.hEvent);
        if (!ok) return Status::fail(ErrorCode::IoError, err(write ? "WriteFile" : "ReadFile", e));
        if (n == 0) return Status::fail(ErrorCode::IoError, "pipe closed");
        done_total += n;
    }
    return contracts::ok_status();
}

Status PipeEnd::write_raw(const std::string& bytes, std::uint32_t timeout_ms) {
    std::string copy = bytes;
    return io(true, copy.data(), static_cast<std::uint32_t>(copy.size()), timeout_ms);
}

Status PipeEnd::write_frame(const core::ipc::Frame& f, std::uint32_t timeout_ms) {
    if (f.payload.size() > core::ipc::kMaxPayload) return Status::fail(ErrorCode::ParseLimit, "payload too large");
    return write_raw(core::ipc::encode(f), timeout_ms);
}

contracts::Result<core::ipc::Frame> PipeEnd::read_frame(std::uint32_t timeout_ms) {
    using R = contracts::Result<core::ipc::Frame>;
    std::string header(core::ipc::kHeaderSize, '\0');
    if (auto s = io(false, header.data(), static_cast<std::uint32_t>(header.size()), timeout_ms); !s.ok()) return R(s.error());
    core::ipc::Frame f;
    auto len = core::ipc::parse_header(header, f.type, f.seq);
    if (!len.ok()) return R(len.error());  // asiri boyut: yuk hic okunmaz
    f.payload.resize(len.value());
    if (len.value() > 0)
        if (auto s = io(false, f.payload.data(), len.value(), timeout_ms); !s.ok()) return R(s.error());
    return f;
}

contracts::Result<std::optional<core::ipc::Frame>> PipeEnd::poll_frame() {
    using R = contracts::Result<std::optional<core::ipc::Frame>>;
    DWORD avail = 0;
    if (!PeekNamedPipe(h_, nullptr, 0, nullptr, &avail, nullptr))
        return R::fail(ErrorCode::IoError, err("PeekNamedPipe", GetLastError()));
    if (avail < core::ipc::kHeaderSize) return R(std::optional<core::ipc::Frame>{});
    std::string header(core::ipc::kHeaderSize, '\0');
    DWORD copied = 0;
    if (!PeekNamedPipe(h_, header.data(), static_cast<DWORD>(header.size()), &copied, &avail, nullptr))
        return R::fail(ErrorCode::IoError, err("PeekNamedPipe", GetLastError()));
    if (copied < core::ipc::kHeaderSize) return R(std::optional<core::ipc::Frame>{});
    core::ipc::MsgType type{};
    std::uint64_t seq = 0;
    auto len = core::ipc::parse_header(header, type, seq);
    if (!len.ok()) return R(len.error());
    if (avail < core::ipc::kHeaderSize + len.value()) return R(std::optional<core::ipc::Frame>{});
    auto f = read_frame(1000);
    if (!f.ok()) return R(f.error());
    return R(std::optional<core::ipc::Frame>(std::move(f).value()));
}

contracts::Result<std::unique_ptr<PipeServer>> PipeServer::create(const std::wstring& name, const std::string& user_sid) {
    using R = contracts::Result<std::unique_ptr<PipeServer>>;
    if (!valid_sid_text(user_sid)) return R::fail(ErrorCode::ParseError, "invalid user SID");
    // DACL: SYSTEM + Yoneticiler tam; oturum kullanicisi okuma/yazma. Diger herkes yok.
    // SACL: orta butunluk etiketi, NW (no-write-up) — yuksek IL broker'in pipe'ina orta IL UI yazabilsin.
    const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;" + to_wide(user_sid) + L")S:(ML;;NW;;;ME)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr))
        return R::fail(ErrorCode::ParseError, err("SDDL", GetLastError()));
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    HANDLE h = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                core::ipc::kMaxPayload + 64, core::ipc::kMaxPayload + 64, 0, &sa);
    const DWORD e = GetLastError();
    LocalFree(sd);
    if (h == INVALID_HANDLE_VALUE) {
        return R::fail(e == ERROR_ACCESS_DENIED || e == ERROR_PIPE_BUSY ? ErrorCode::Denied : ErrorCode::IoError,
                       err("CreateNamedPipe", e));
    }
    return std::unique_ptr<PipeServer>(new PipeServer(h));
}

contracts::Result<std::unique_ptr<PipeServer>> PipeServer::create_service(const std::wstring& name) {
    using R = contracts::Result<std::unique_ptr<PipeServer>>;
    // DACL: SYSTEM + Yoneticiler tam; ETKILESIMLI kullanicilar (IU) okuma/yazma; ag/hizmet oturumlari yok.
    // SACL: orta butunluk NW — orta IL UI SYSTEM hizmetin pipe'ina yazabilsin, daha dusuk IL yazamasin.
    constexpr wchar_t kSddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)S:(ML;;NW;;;ME)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kSddl, SDDL_REVISION_1, &sd, nullptr))
        return R::fail(ErrorCode::ParseError, err("SDDL", GetLastError()));
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    HANDLE h = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                core::ipc::kMaxPayload + 64, core::ipc::kMaxPayload + 64, 0, &sa);
    const DWORD e = GetLastError();
    LocalFree(sd);
    if (h == INVALID_HANDLE_VALUE) {
        return R::fail(e == ERROR_ACCESS_DENIED || e == ERROR_PIPE_BUSY ? ErrorCode::Denied : ErrorCode::IoError,
                       err("CreateNamedPipe", e));
    }
    return std::unique_ptr<PipeServer>(new PipeServer(h));
}

void PipeServer::disconnect() {
    if (h_ && h_ != INVALID_HANDLE_VALUE) (void)DisconnectNamedPipe(h_);
}

Status PipeServer::accept(std::uint32_t timeout_ms) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return Status::fail(ErrorCode::IoError, "CreateEvent");
    BOOL ok = ConnectNamedPipe(h_, &ov);
    DWORD e = ok ? ERROR_SUCCESS : GetLastError();
    Status result = contracts::ok_status();
    if (!ok && e == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, timeout_ms) != WAIT_OBJECT_0) {
            CancelIoEx(h_, &ov);
            DWORD n = 0;
            GetOverlappedResult(h_, &ov, &n, TRUE);
            result = Status::fail(ErrorCode::IoError, "accept timeout");
        } else {
            DWORD n = 0;
            if (!GetOverlappedResult(h_, &ov, &n, FALSE)) result = Status::fail(ErrorCode::IoError, err("accept", GetLastError()));
        }
    } else if (!ok && e != ERROR_PIPE_CONNECTED) {
        result = Status::fail(ErrorCode::IoError, err("ConnectNamedPipe", e));
    }
    CloseHandle(ov.hEvent);
    return result;
}

contracts::Result<PeerIdentity> PipeServer::client_identity() const {
    using R = contracts::Result<PeerIdentity>;
    PeerIdentity id;
    ULONG pid = 0, session = 0;
    if (!GetNamedPipeClientProcessId(h_, &pid)) return R::fail(ErrorCode::IoError, err("ClientProcessId", GetLastError()));
    if (!GetNamedPipeClientSessionId(h_, &session)) return R::fail(ErrorCode::IoError, err("ClientSessionId", GetLastError()));
    id.pid = pid;
    id.session = session;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return R::fail(ErrorCode::IoError, err("OpenProcess", GetLastError()));
    const auto created = process_create_time(p);
    std::wstring buf(32768, L'\0');
    DWORD n = static_cast<DWORD>(buf.size());
    const bool image_ok = QueryFullProcessImageNameW(p, 0, buf.data(), &n) != 0;
    CloseHandle(p);
    if (!created || !image_ok || n == 0) return R::fail(ErrorCode::IoError, "client process identity unavailable");
    id.create_time = *created;
    id.image.assign(buf.data(), n);
    return id;
}

contracts::Result<std::unique_ptr<PipeClient>> PipeClient::connect(const std::wstring& name, std::uint32_t expected_server_pid,
                                                                  std::uint32_t timeout_ms) {
    using R = contracts::Result<std::unique_ptr<PipeClient>>;
    const DWORD deadline = GetTickCount() + timeout_ms;
    for (;;) {
        // SECURITY_IDENTIFICATION: sunucu istemciyi kimlik icin goruntuleyebilir ama onun adina islem yapamaz.
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            ULONG spid = 0;
            if (!GetNamedPipeServerProcessId(h, &spid) || spid != expected_server_pid) {
                CloseHandle(h);
                return R::fail(ErrorCode::Denied, "pipe server is not the expected process");
            }
            return std::unique_ptr<PipeClient>(new PipeClient(h));
        }
        const DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PIPE_BUSY)
            return R::fail(e == ERROR_ACCESS_DENIED ? ErrorCode::Denied : ErrorCode::IoError, err("CreateFile pipe", e));
        if (static_cast<LONG>(GetTickCount() - deadline) >= 0) return R::fail(ErrorCode::IoError, "connect timeout");
        Sleep(20);
    }
}

contracts::Result<std::unique_ptr<PipeClient>> PipeClient::connect_service(const std::wstring& name, std::uint32_t expected_server_pid,
                                                                          std::uint32_t timeout_ms) {
    using R = contracts::Result<std::unique_ptr<PipeClient>>;
    if (expected_server_pid == 0) return R::fail(ErrorCode::Denied, "service pid unknown");
    // connect ile ayni kural: SECURITY_IDENTIFICATION (hizmet istemci adina islem yapamaz) + sunucu PID dogrulamasi.
    return connect(name, expected_server_pid, timeout_ms);
}

std::uint32_t PipeClient::server_pid() const {
    ULONG spid = 0;
    if (!h_ || h_ == INVALID_HANDLE_VALUE || !GetNamedPipeServerProcessId(h_, &spid)) return 0;
    return spid;
}

}  // namespace ef::platform
