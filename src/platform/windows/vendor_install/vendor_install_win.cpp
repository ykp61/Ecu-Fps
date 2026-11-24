#include "platform/windows/vendor_install/vendor_install_win.h"

#include <windows.h>
#include <bcrypt.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <cwchar>
#include <system_error>
#include <vector>

#include "platform/windows/inventory/gpu_storage.h"
#include "platform/windows/journal_fs.h"
#include "platform/windows/signature.h"
#include "platform/windows/win_env.h"

namespace ef::platform::vendor {

namespace cd = core::drivers;
using core::security::SignatureStatus;

namespace {

std::string to_utf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE x) : h(x) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& o) noexcept : h(o.h) { o.h = INVALID_HANDLE_VALUE; }
    ~Handle() {
        if (h != INVALID_HANDLE_VALUE && h != nullptr) CloseHandle(h);
    }
    bool ok() const { return h != INVALID_HANDLE_VALUE && h != nullptr; }
};

struct FileSource final : cd::ByteSource {
    HANDLE h;
    explicit FileSource(HANDLE x) : h(x) {}
    std::optional<std::size_t> read(std::uint8_t* buf, std::size_t n) override {
        DWORD got = 0;
        if (!ReadFile(h, buf, static_cast<DWORD>(n), &got, nullptr)) return std::nullopt;
        return static_cast<std::size_t>(got);
    }
};
struct FileSink final : cd::ByteSink {
    HANDLE h;
    explicit FileSink(HANDLE x) : h(x) {}
    bool write(const std::uint8_t* buf, std::size_t n) override {
        DWORD put = 0;
        return WriteFile(h, buf, static_cast<DWORD>(n), &put, nullptr) && put == n;
    }
};

// Yalniz duz dosya (dizin / baglanti noktasi / sembolik baglanti degil); yalniz-okuma paylasimi (acikken degistirilemez).
Handle open_plain_for_read(const std::filesystem::path& p, std::uint64_t* size) {
    Handle f(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!f.ok()) return f;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(f.h, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        GetFileType(f.h) != FILE_TYPE_DISK) {
        CloseHandle(f.h);
        f.h = INVALID_HANDLE_VALUE;
        return f;
    }
    if (size) *size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    return f;
}

std::string signer_of(const std::filesystem::path& file) {
    WINTRUST_FILE_INFO fi{};
    fi.cbStruct = sizeof(fi);
    fi.pcwszFilePath = file.c_str();
    WINTRUST_DATA wd{};
    wd.cbStruct = sizeof(wd);
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    wd.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;  // ag yok
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    std::string out;
    if (WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd) == ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA* pd = WTHelperProvDataFromStateData(wd.hWVTStateData);
        CRYPT_PROVIDER_SGNR* sg = pd ? WTHelperGetProvSignerFromChain(pd, 0, FALSE, 0) : nullptr;
        CRYPT_PROVIDER_CERT* pc = sg ? WTHelperGetProvCertFromChain(sg, 0) : nullptr;  // 0 = imzalayan (yaprak)
        if (pc && pc->pCert) {
            wchar_t name[256] = {};
            // Yalniz konunun CN alani: SIMPLE_DISPLAY CN yoksa OU / O / e-postaya duser (taklit yuzeyi).
            const DWORD n = CertGetNameStringW(pc->pCert, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(szOID_COMMON_NAME), name, 256);
            if (n > 1) out = to_utf8(std::wstring_view(name, n - 1));
        }
    }
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
    return out;
}

std::wstring widen(std::string_view s) { return std::wstring(s.begin(), s.end()); }  // yalniz ASCII kimlik / ad

// Yonetici olarak calisan paket kendini %TEMP%'e acar; kullanicinin TEMP'i yonetici olmayan surecler tarafindan
// yazilabilir (DLL yerlestirme). Cocuga yonetici ACL'li klasoru TEMP/TMP olarak verilir; diger degiskenler aynen kalir.
std::vector<wchar_t> child_environment(const std::wstring& temp) {
    std::vector<wchar_t> env;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
            if (_wcsnicmp(p, L"TEMP=", 5) == 0 || _wcsnicmp(p, L"TMP=", 4) == 0) continue;
            env.insert(env.end(), p, p + wcslen(p) + 1);
        }
        FreeEnvironmentStringsW(block);
    }
    for (const wchar_t* k : {L"TEMP=", L"TMP="}) {
        const std::wstring e = std::wstring(k) + temp;
        env.insert(env.end(), e.c_str(), e.c_str() + e.size() + 1);
    }
    env.push_back(L'\0');
    return env;
}

class WinVendorInstall final : public cd::VendorInstallApi {
public:
    WinVendorInstall(std::filesystem::path machine_dir, std::function<cd::RestorePointState()> rp)
        : dir_(std::move(machine_dir) / L"vendor_pkg"), rp_(std::move(rp)) {}

    cd::StageResult stage(const cd::VendorRef& ref) override {
        cd::StageResult r;
        const auto user = user_package_dir(false);
        const auto name = cd::package_file_name(ref.id);
        if (!user || name.empty()) return r.status = cd::StageStatus::NotFound, r;
        std::uint64_t size = 0;
        Handle src = open_plain_for_read(*user / widen(name), &size);
        if (!src.ok()) return r.status = cd::StageStatus::NotFound, r;
        if (size > cd::kPackageMaxBytes) return r.status = cd::StageStatus::TooLarge, r;
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);  // ust klasorun yonetici ACL'ini miras alir
        if (!verify_machine_journal_dir(dir_).ok()) return r.status = cd::StageStatus::IoError, r;
        const auto dst_path = dir_ / widen(name);
        Handle dst(CreateFileW(dst_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!dst.ok()) return r.status = cd::StageStatus::IoError, r;
        FileSource in(src.h);
        FileSink out(dst.h);
        const auto run = cd::copy_package(in, out, size, {}, {});
        r.bytes = run.bytes;
        switch (run.outcome) {
            case cd::CopyOutcome::Ok: r.status = cd::StageStatus::Ok; break;
            case cd::CopyOutcome::TooSmall: r.status = cd::StageStatus::TooSmall; break;
            case cd::CopyOutcome::TooLarge: r.status = cd::StageStatus::TooLarge; break;
            default: r.status = cd::StageStatus::IoError; break;
        }
        return r;
    }

    cd::SignatureCheck verify(const cd::VendorRef& ref) override {
        cd::SignatureCheck c;
        const auto name = cd::package_file_name(ref.id);
        if (name.empty()) return c;
        return verify_signer(dir_ / widen(name));
    }

    cd::RestorePointState restore_point() override { return rp_ ? rp_() : cd::RestorePointState::Failed; }

    std::string installed_version(cd::GpuVendor v) override {
        for (const auto& g : inventory::read_gpus()) {
            if (!g.vendor_id.has() || *g.vendor_id.value != cd::pci_vendor_id(v)) continue;
            if (g.driver_version.has()) return *g.driver_version.value;
        }
        return {};
    }

    cd::LaunchResult run_installer(const cd::VendorRef& ref, const std::vector<std::wstring>& args, std::uint32_t timeout_ms) override {
        cd::LaunchResult r;
        const auto name = cd::package_file_name(ref.id);
        if (name.empty()) return r;
        const auto exe = dir_ / widen(name);
        // Paket calisirken degistirilemesin: yazma / silme paylasimi yok.
        Handle lock(CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!lock.ok()) return r;
        std::wstring cmd = L"\"" + exe.wstring() + L"\"";
        for (const auto& a : args) cmd += L" " + a;
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        const std::wstring cwd = dir_.wstring();
        const auto tmp = dir_ / L"tmp";
        std::error_code ec;
        std::filesystem::remove_all(tmp, ec);  // onceki calismadan kalan cikarilmis dosyalar
        std::filesystem::create_directories(tmp, ec);  // vendor_pkg'nin yonetici ACL'ini miras alir
        if (!verify_machine_journal_dir(tmp).ok()) return r;
        auto env = child_environment(tmp.wstring());
        if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, env.data(), cwd.c_str(), &si, &pi))
            return r;
        r.started = true;
        CloseHandle(pi.hThread);
        const DWORD w = WaitForSingleObject(pi.hProcess, timeout_ms);
        if (w != WAIT_OBJECT_0) {
            r.timed_out = true;  // baslamis kurulum oldurulmez
        } else {
            DWORD code = 0;
            if (GetExitCodeProcess(pi.hProcess, &code)) r.exit_code = code;
        }
        CloseHandle(pi.hProcess);
        return r;
    }

    void discard(const cd::VendorRef& ref) override {
        const auto name = cd::package_file_name(ref.id);
        if (name.empty()) return;
        std::error_code ec;
        std::filesystem::remove(dir_ / widen(name), ec);
        std::filesystem::remove_all(dir_ / L"tmp", ec);  // calisan paket varsa kilitli dosyalar kalir; sonraki calisma temizler
    }

private:
    std::filesystem::path dir_;
    std::function<cd::RestorePointState()> rp_;
};

bool is_package_name(const std::wstring& n) {
    const auto dot = n.find(L'.');
    if (dot != 16) return false;
    const auto ext = n.substr(dot);
    if (ext != L".exe" && ext != L".part") return false;
    for (std::size_t i = 0; i < 16; ++i) {
        const wchar_t c = n[i];
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
    }
    return true;
}

}  // namespace

std::optional<std::filesystem::path> user_package_dir(bool create) {
    const auto base = app_data_dir();
    if (!base) return std::nullopt;
    auto d = *base / L"drivers";
    std::error_code ec;
    if (create) std::filesystem::create_directories(d, ec);
    if (!std::filesystem::is_directory(d, ec)) return std::nullopt;
    return d;
}

std::string new_package_id() {
    unsigned char b[8] = {};
    if (BCryptGenRandom(nullptr, b, sizeof b, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return {};
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (const auto x : b) s += hex[x >> 4], s += hex[x & 15];
    return s;
}

void clear_package_dir() {
    const auto d = user_package_dir(false);
    if (!d) return;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(*d, ec)) {
        std::error_code e2;
        if (!e.is_regular_file(e2) || !is_package_name(e.path().filename().wstring())) continue;
        std::filesystem::remove(e.path(), e2);
    }
}

void remove_package(std::string_view id) {
    const auto d = user_package_dir(false);
    if (!d || !cd::valid_package_id(id)) return;
    std::error_code ec;
    std::filesystem::remove(*d / widen(cd::package_file_name(id)), ec);
    std::filesystem::remove(*d / widen(cd::package_part_name(id)), ec);
}

cd::SignatureCheck verify_signer(const std::filesystem::path& file) {
    cd::SignatureCheck c;
    c.status = verify_file_signature(file);
    if (c.status == SignatureStatus::Valid) c.signer = signer_of(file);
    return c;
}

ImportRun import_package(const std::filesystem::path& source, std::string_view id, const std::function<bool()>& stop,
                         const std::function<void(std::uint64_t, std::uint64_t)>& progress) {
    ImportRun r;
    const auto d = user_package_dir(true);
    if (!d || !cd::valid_package_id(id)) return r.outcome = cd::CopyOutcome::WriteFailed, r;
    std::uint64_t size = 0;
    Handle src = open_plain_for_read(source, &size);
    if (!src.ok()) return r;
    const auto part = *d / widen(cd::package_part_name(id));
    const auto done = *d / widen(cd::package_file_name(id));
    {
        Handle dst(CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!dst.ok()) return r.outcome = cd::CopyOutcome::WriteFailed, r;
        FileSource in(src.h);
        FileSink out(dst.h);
        const auto run = cd::copy_package(in, out, size, stop, progress);
        r.outcome = run.outcome;
        r.bytes = run.bytes;
    }
    std::error_code ec;
    if (r.outcome != cd::CopyOutcome::Ok) {
        std::filesystem::remove(part, ec);  // yarim kopya hicbir zaman tamam sayilmaz
        return r;
    }
    std::filesystem::rename(part, done, ec);
    if (ec) {
        std::filesystem::remove(part, ec);
        r.outcome = cd::CopyOutcome::WriteFailed;
    }
    return r;
}

std::unique_ptr<cd::VendorInstallApi> make_vendor_install(std::filesystem::path machine_dir, std::function<cd::RestorePointState()> restore_point) {
    return std::make_unique<WinVendorInstall>(std::move(machine_dir), std::move(restore_point));
}

}  // namespace ef::platform::vendor
