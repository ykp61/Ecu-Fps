#include "app/update_support.h"

#include <windows.h>

#include <algorithm>

#include "core/update/release_key.h"
#include "platform/windows/catalog_signature.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/net/update_http.h"
#include "platform/windows/win_env.h"

namespace ef::app::upd {

bool update_check_disabled_by_env() {
    wchar_t v[8]{};
    const DWORD n = GetEnvironmentVariableW(L"EF_UPDATE_CHECK", v, 8);
    return n == 1 && v[0] == L'0';
}

std::uint32_t leading_build(const std::string& s) {
    std::uint32_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') break;
        if (v > 100000000u) return 0;
        v = v * 10 + static_cast<std::uint32_t>(c - '0');
    }
    return v;
}

std::string os_build_string() {
    const auto os = platform::inventory::read_os();
    return (os.build.has() ? std::to_string(*os.build.value) : std::string("?")) + "." +
           (os.ubr.has() ? std::to_string(*os.ubr.value) : std::string("?"));
}

std::optional<CheckResult> check_for_update(const std::string& current_version, std::uint64_t highest_seen, bool exe_dir_writable,
                                            const std::atomic<bool>& cancel) {
    const auto got = platform::net::update_get(core::update::kManifestUrl, core::update::kManifestMaxBytes, core::update::kCheckTimeoutMs, cancel);
    if (cancel.load()) return std::nullopt;
    core::catalog::ReleaseContext ctx;
    ctx.current_version = current_version;
    ctx.publisher = "Ecu Fps";
    ctx.os_build = leading_build(os_build_string());
    ctx.trusted_release_key = core::update::has_release_key();
    ctx.last_manifest_version = core::update::replay_floor(highest_seen);  // daha eski imzali manifest = Replay
    const core::catalog::KeyLookup keys = [](std::string_view domain, std::string_view key_id) {
        return domain == core::update::kReleaseDomain && key_id == core::update::kReleaseKeyId ? std::string(core::update::kReleasePublicBlob)
                                                                                               : std::string();
    };
    const core::catalog::RawVerifier verify = [](std::string_view blob, std::string_view msg, std::string_view sig) {
        return platform::verify_ecdsa_p256(blob, msg, sig);
    };
    CheckResult r;
    r.decision = core::update::decide_update(got.ok() ? std::optional<std::string_view>(got.body) : std::nullopt, ctx, keys, verify,
                                             exe_dir_writable);
    if (r.decision.outcome == core::update::UpdateOutcome::Mandatory || r.decision.outcome == core::update::UpdateOutcome::NotWritable)
        r.envelope = got.body;
    return r;
}

bool write_file(const std::string& path, const std::string& data) {
    const auto w = platform::to_wide(path);
    HANDLE h = CreateFileW(w.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::size_t off = 0;
    bool ok = true;
    while (ok && off < data.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - off, 1u << 20));
        DWORD wrote = 0;
        ok = WriteFile(h, data.data() + off, chunk, &wrote, nullptr) && wrote == chunk;
        off += wrote;
    }
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) DeleteFileW(w.c_str());
    return ok;
}

std::optional<std::string> read_file(const std::string& path, std::uint64_t max_bytes) {
    HANDLE h = CreateFileW(platform::to_wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    LARGE_INTEGER size{};
    std::optional<std::string> out;
    if (GetFileSizeEx(h, &size) && size.QuadPart >= 0 && static_cast<std::uint64_t>(size.QuadPart) <= max_bytes) {
        std::string data(static_cast<std::size_t>(size.QuadPart), '\0');
        std::size_t off = 0;
        bool ok = true;
        while (ok && off < data.size()) {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - off, 1u << 20));
            DWORD got = 0;
            ok = ReadFile(h, data.data() + off, chunk, &got, nullptr) && got == chunk;
            off += got;
        }
        if (ok) out = std::move(data);
    }
    CloseHandle(h);
    return out;
}

}  // namespace ef::app::upd
