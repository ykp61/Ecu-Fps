#include "platform/windows/service/client_facts.h"

#include <windows.h>
#include <sddl.h>

#include <optional>
#include <string>
#include <vector>

#include "core/setup/acl_rules.h"
#include "platform/windows/install/secure_dir.h"

namespace ef::platform::service {

namespace {

constexpr UINT kRevertFailedExit = 0xC0DE0001;

std::optional<core::service::FileId> file_id_of(const std::wstring& path) {
    if (path.empty()) return std::nullopt;
    // Yalniz ozellik okuma; paylasim tam (calisan EXE kilitli olsa da acilir). Hizmet kendi kimligiyle acar.
    HANDLE f = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION info{};
    const bool ok = GetFileInformationByHandle(f, &info) != 0;
    CloseHandle(f);
    if (!ok) return std::nullopt;
    core::service::FileId id;
    id.volume_serial = info.dwVolumeSerialNumber;
    id.file_index = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    return id;
}

// Taklit (impersonation) her cikis yolunda geri alinir. Geri alinamazsa hizmet istemci kimligiyle devam ETMEZ.
class ImpersonationScope {
public:
    explicit ImpersonationScope(HANDLE pipe) : active_(ImpersonateNamedPipeClient(pipe) != 0) {}
    ~ImpersonationScope() { revert(); }
    ImpersonationScope(const ImpersonationScope&) = delete;
    ImpersonationScope& operator=(const ImpersonationScope&) = delete;
    bool active() const noexcept { return active_; }
    void revert() {
        if (!active_) return;
        active_ = false;
        if (!RevertToSelf()) TerminateProcess(GetCurrentProcess(), kRevertFailedExit);
    }

private:
    bool active_;
};

std::vector<std::uint8_t> token_info(HANDLE token, TOKEN_INFORMATION_CLASS cls) {
    DWORD n = 0;
    (void)GetTokenInformation(token, cls, nullptr, 0, &n);
    if (n == 0 || n > 64 * 1024) return {};
    std::vector<std::uint8_t> buf(n);
    if (!GetTokenInformation(token, cls, buf.data(), n, &n)) return {};
    return buf;
}

std::string sid_text(PSID sid) {
    char* s = nullptr;
    if (!sid || !ConvertSidToStringSidA(sid, &s)) return {};
    std::string out(s);
    LocalFree(s);
    return out;
}

void read_token(HANDLE pipe, core::service::ClientFacts& f) {
    HANDLE token = nullptr;
    {
        ImpersonationScope imp(static_cast<HANDLE>(pipe));
        if (!imp.active()) return;
        // OpenAsSelf = TRUE: erisim denetimi hizmetin kendi belirteciyle yapilir.
        if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) token = nullptr;
    }  // RevertToSelf burada
    if (!token) return;
    DWORD session = 0, n = 0;
    const bool session_ok = GetTokenInformation(token, TokenSessionId, &session, sizeof(session), &n) != 0;
    const auto user = token_info(token, TokenUser);
    const auto groups = token_info(token, TokenGroups);
    CloseHandle(token);
    if (!session_ok || user.empty() || groups.empty()) return;
    f.session = session;
    f.user_sid = sid_text(reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid);
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID interactive = nullptr;
    if (!AllocateAndInitializeSid(&nt, 1, SECURITY_INTERACTIVE_RID, 0, 0, 0, 0, 0, 0, 0, &interactive)) return;
    // K27: BUILTIN\Administrators (S-1-5-32-544) uyeligi. UAC ile suzulmus orta duzey belirtecte yonetici grubu
    // SE_GROUP_USE_FOR_DENY_ONLY ile bulunur; standart kullanicida hic yoktur. Bu yuzden ozniteliklere bakilmaz
    // (CheckTokenMembership deny-only girisi yok sayar, kullanilmaz).
    PSID admins = nullptr;
    if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins)) {
        FreeSid(interactive);
        return;
    }
    const auto* g = reinterpret_cast<const TOKEN_GROUPS*>(groups.data());
    bool found = false, admin = false;
    for (DWORD i = 0; i < g->GroupCount; ++i) {
        const auto& sa = g->Groups[i];
        if ((sa.Attributes & SE_GROUP_ENABLED) && !(sa.Attributes & SE_GROUP_USE_FOR_DENY_ONLY) && EqualSid(sa.Sid, interactive)) found = true;
        if (EqualSid(sa.Sid, admins)) admin = true;
    }
    FreeSid(admins);
    FreeSid(interactive);
    f.interactive = found;
    f.admin_member = admin;
    f.token_ok = !f.user_sid.empty();
}

}  // namespace

ClientProcess::~ClientProcess() {
    if (h_) CloseHandle(static_cast<HANDLE>(h_));
}

bool ClientProcess::exited() const { return !h_ || WaitForSingleObject(static_cast<HANDLE>(h_), 0) != WAIT_TIMEOUT; }

GatheredClient gather_client(const PipeServer& pipe) {
    GatheredClient out;
    auto& f = out.facts;
    HANDLE h = static_cast<HANDLE>(pipe.handle());
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(h, &pid) || pid == 0) return out;
    f.pid = pid;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!p) return out;
    out.process = std::make_unique<ClientProcess>(p);
    std::wstring buf(32768, L'\0');
    DWORD n = static_cast<DWORD>(buf.size());
    if (QueryFullProcessImageNameW(p, 0, buf.data(), &n) && n > 0) {
        f.image.assign(buf.data(), n);
        f.image_ok = true;
        if (auto id = file_id_of(f.image)) {
            f.file = *id;
            f.file_ok = true;
        }
    }
    read_token(h, f);
    // PID tanitici acildiktan sonra istemci cikmis olabilir: belirtec yine okunur ama kapanmis surec reddedilir.
    if (out.process->exited()) f.image_ok = false;
    return out;
}

core::service::ServiceSelf gather_self(const core::setup::InstallLayout& layout) {
    core::service::ServiceSelf s;
    s.installed_exe = layout.exe.native();
    if (auto id = file_id_of(s.installed_exe)) {
        s.installed_file = *id;
        s.installed_file_ok = true;
    }
    if (!layout.dir.empty() && !install::is_reparse_or_missing_dir(layout.dir)) {
        auto facts = install::read_acl_facts(layout.dir);
        s.install_dir_acl_ok = facts.ok() && core::setup::install_dir_acl_ok(facts.value());
    }
    return s;
}

}  // namespace ef::platform::service
