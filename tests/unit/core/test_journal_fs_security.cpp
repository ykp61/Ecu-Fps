// CORE-01 + CORE-05 platform testleri: makine journal dizini sahiplik/DACL dogrulamasi ve
// yarim kalan *.json.tmp gecici dosyalarinin raporlanmasi/temizlenmesi.
#include <windows.h>

#include <aclapi.h>
#include <sddl.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "ef_test.h"
#include "platform/windows/journal_fs.h"
#include "platform/windows/win_env.h"

using namespace ef::platform;
using ef::contracts::ErrorCode;

namespace {

// Uretimle ayni korumali ACL: SYSTEM + Yoneticiler tam, Kullanicilar yalniz okuma.
constexpr wchar_t kProtectedSddl[] = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FR;;;BU)";

struct Scratch {
    std::filesystem::path dir;
    ~Scratch() {
        if (dir.empty()) return;
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

std::filesystem::path make_scratch_dir(const wchar_t* tag) {
    std::error_code ec;
    const auto base = std::filesystem::temp_directory_path(ec);
    if (ec) return {};
    for (int i = 0; i < 100; ++i) {
        const auto p = base / (std::wstring(L"ef_journal_fs_") + tag + L"_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                               std::to_wstring(i));
        if (std::filesystem::create_directory(p, ec)) return p;
        ec.clear();
    }
    return {};
}

bool set_dacl(const std::filesystem::path& dir, const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) return false;
    const BOOL ok = SetFileSecurityW(dir.c_str(), DACL_SECURITY_INFORMATION, sd);
    LocalFree(sd);
    return ok != FALSE;
}

// Test dizininin gercek sahibi (ortama gore kullanici/SYSTEM); sahiplik kurali bu SID ile dogrulanir.
std::string owner_sid(const std::filesystem::path& dir) {
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(dir.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &sd) !=
        ERROR_SUCCESS)
        return {};
    LPWSTR text = nullptr;
    std::string out;
    if (ConvertSidToStringSidW(owner, &text)) {
        out = to_utf8(text);
        LocalFree(text);
    }
    if (sd) LocalFree(sd);
    return out;
}

}  // namespace

EF_TEST(journal_dir_security_accepts_protected_dacl) {
    Scratch s{make_scratch_dir(L"ok")};
    EF_CHECK(!s.dir.empty());
    EF_CHECK(set_dacl(s.dir, kProtectedSddl));
    const auto owner = owner_sid(s.dir);
    EF_CHECK(!owner.empty());
    EF_CHECK(detail::check_journal_dir_security(s.dir, {owner}).ok());
}

EF_TEST(journal_dir_security_rejects_untrusted_write_ace) {
    Scratch s{make_scratch_dir(L"write")};
    EF_CHECK(!s.dir.empty());
    // Everyone (WD) tam denetim: yukseltilmemis kullanici journal'a yazabilir -> reddedilmeli.
    EF_CHECK(set_dacl(s.dir, L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FR;;;BU)(A;OICI;FA;;;WD)"));
    const auto owner = owner_sid(s.dir);
    EF_CHECK(!owner.empty());
    const auto st = detail::check_journal_dir_security(s.dir, {owner});
    EF_CHECK(!st.ok());
    EF_CHECK(st.error().code == ErrorCode::Denied);
    EF_CHECK(st.error().detail.find("write ACE") != std::string::npos);
}

EF_TEST(journal_dir_security_rejects_unexpected_owner) {
    Scratch s{make_scratch_dir(L"owner")};
    EF_CHECK(!s.dir.empty());
    EF_CHECK(set_dacl(s.dir, kProtectedSddl));
    // Sağlıklı DACL bile olsa beklenen sahiplik listesinde olmayan sahip reddedilir.
    const auto st = detail::check_journal_dir_security(s.dir, {"S-1-5-18", "S-1-5-32-544"});
    const auto owner = owner_sid(s.dir);
    if (owner == "S-1-5-18" || owner == "S-1-5-32-544") return;  // ortam SYSTEM/Administrators ise kural uygulanamaz
    EF_CHECK(!st.ok());
    EF_CHECK(st.error().code == ErrorCode::Denied);
    EF_CHECK(st.error().detail.find("owner") != std::string::npos);
}

EF_TEST(journal_dir_security_missing_and_non_directory_rejected) {
    Scratch s{make_scratch_dir(L"shape")};
    EF_CHECK(!s.dir.empty());
    const auto missing = detail::check_journal_dir_security(s.dir / L"missing", {"S-1-5-18"});
    EF_CHECK(missing.error().code == ErrorCode::MissingValue);
    const auto file = s.dir / L"plain-file";
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    EF_CHECK(h != INVALID_HANDLE_VALUE);
    CloseHandle(h);
    const auto not_dir = detail::check_journal_dir_security(file, {"S-1-5-18"});
    EF_CHECK(not_dir.error().code == ErrorCode::Denied);
    EF_CHECK(not_dir.error().detail.find("not a directory") != std::string::npos);
}

EF_TEST(journal_fs_reports_stale_temp_and_cleans_on_write) {
    Scratch s{make_scratch_dir(L"temp")};
    EF_CHECK(!s.dir.empty());
    WinJournalFs fs(s.dir);
    EF_CHECK(fs.write_atomic("tx-0123456789abcdef0123456789abcdef.json", "{}").ok());
    EF_CHECK(fs.temp_files().empty());  // basarili atomik yazim gecici dosya birakmaz
    {
        std::ofstream tmp(s.dir / L"tx-deadbeef.json.tmp", std::ios::binary);
        tmp << "partial";
    }
    const auto temps = fs.temp_files();
    EF_CHECK(temps.size() == 1);
    EF_CHECK(temps[0] == "tx-deadbeef.json.tmp");
    EF_CHECK(fs.list().value().size() == 1);  // gecici dosya journal kaydi sayilmaz
    EF_CHECK(fs.remove(temps[0]).ok());
    EF_CHECK(fs.temp_files().empty());
}
