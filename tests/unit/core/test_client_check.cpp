// ADR-033 / spec 5: yardimci hizmet istemci guven karari (saf). Gercek surec, dosya veya belirtec okunmaz.
#include "core/service/client_check.h"
#include "ef_test.h"

using namespace ef::core::service;

namespace {
const std::wstring kExe = L"C:\\Program Files\\Ecu Fps\\EcuFps.exe";
constexpr FileId kFile{0x1234ABCDull, 0x0001000000000042ull};

ClientFacts good_client() {
    ClientFacts c;
    c.pid = 4242;
    c.session = 1;
    c.image_ok = true;
    c.image = kExe;
    c.file_ok = true;
    c.file = kFile;
    c.token_ok = true;
    c.interactive = true;
    c.admin_member = true;
    c.user_sid = "S-1-5-21-1-2-3-1001";
    return c;
}

ServiceSelf good_self() {
    ServiceSelf s;
    s.installed_exe = kExe;
    s.installed_file_ok = true;
    s.installed_file = kFile;
    s.install_dir_acl_ok = true;
    return s;
}
}  // namespace

EF_TEST(client_check_all_good_allows) { EF_CHECK(check_client(good_client(), good_self()) == ClientVerdict::Allow); }

EF_TEST(client_check_insecure_acl_first) {
    auto s = good_self();
    s.install_dir_acl_ok = false;
    auto c = good_client();
    c.image = L"C:\\evil.exe";  // daha sonraki kurallar da bozuk: ilk once ACL
    c.token_ok = false;
    EF_CHECK(check_client(c, s) == ClientVerdict::InstallDirInsecure);
}

EF_TEST(client_check_query_failures) {
    const auto s = good_self();
    auto c = good_client();
    c.image_ok = false;
    EF_CHECK(check_client(c, s) == ClientVerdict::QueryFailed);
    c = good_client();
    c.file_ok = false;
    EF_CHECK(check_client(c, s) == ClientVerdict::QueryFailed);
    c = good_client();
    c.token_ok = false;
    EF_CHECK(check_client(c, s) == ClientVerdict::QueryFailed);
    c = good_client();
    c.user_sid.clear();
    EF_CHECK(check_client(c, s) == ClientVerdict::QueryFailed);
    auto s2 = good_self();
    s2.installed_file_ok = false;  // kurulum yolundaki dosya okunamadi
    EF_CHECK(check_client(good_client(), s2) == ClientVerdict::QueryFailed);
    // QueryFailed, WrongImage'dan once gelir.
    c = good_client();
    c.image = L"C:\\Users\\x\\Downloads\\EcuFps.exe";
    c.token_ok = false;
    EF_CHECK(check_client(c, s) == ClientVerdict::QueryFailed);
}

EF_TEST(client_check_wrong_image) {
    auto c = good_client();
    c.image = L"C:\\Users\\x\\Downloads\\EcuFps.exe";  // kurulum disi kopya
    EF_CHECK(check_client(c, good_self()) == ClientVerdict::WrongImage);
    c.image = L"C:\\Program Files\\Ecu Fps\\EcuFps.exe.bak";
    EF_CHECK(check_client(c, good_self()) == ClientVerdict::WrongImage);
}

EF_TEST(client_check_path_case_and_prefix_insensitive) {
    auto c = good_client();
    c.image = L"c:\\PROGRAM FILES\\ecu fps\\ECUFPS.EXE";
    EF_CHECK(check_client(c, good_self()) == ClientVerdict::Allow);
    c.image = L"\\\\?\\C:\\Program Files\\Ecu Fps\\EcuFps.exe";
    EF_CHECK(check_client(c, good_self()) == ClientVerdict::Allow);
}

EF_TEST(client_check_hard_link_same_path_wrong_file) {
    // Ayni yol ama farkli dosya kimligi: sert bag / yeniden adlandirma hilesi.
    auto c = good_client();
    c.file.file_index = kFile.file_index + 1;
    EF_CHECK(check_client(c, good_self()) == ClientVerdict::WrongFile);
    c = good_client();
    c.file.volume_serial = 0x99;
    EF_CHECK(check_client(c, good_self()) == ClientVerdict::WrongFile);
}

EF_TEST(client_check_session_zero) {
    auto c = good_client();
    c.session = 0;
    EF_CHECK(check_client(c, good_self()) == ClientVerdict::SessionZero);
}

EF_TEST(client_check_not_interactive) {
    auto c = good_client();
    c.interactive = false;
    EF_CHECK(check_client(c, good_self()) == ClientVerdict::NotInteractive);
}

EF_TEST(client_check_not_admin) {
    // K27: yonetici grubu uyesi olmayan (standart) hesap hizmeti kullanamaz; UAC aracisina duser.
    auto c = good_client();
    c.admin_member = false;
    EF_CHECK(check_client(c, good_self()) == ClientVerdict::NotAdmin);
    // NotInteractive, NotAdmin'den once gelir; SessionZero da once.
    c.interactive = false;
    EF_CHECK(check_client(c, good_self()) == ClientVerdict::NotInteractive);
    c.session = 0;
    EF_CHECK(check_client(c, good_self()) == ClientVerdict::SessionZero);
}

EF_TEST(client_check_verdict_names) {
    EF_CHECK(to_string(ClientVerdict::NotAdmin) == "not_admin");
    EF_CHECK(to_string(ClientVerdict::Allow) == "allow");
    EF_CHECK(to_string(ClientVerdict::WrongFile) == "wrong_file");
    EF_CHECK(to_string(ClientVerdict::InstallDirInsecure) == "install_dir_insecure");
}
