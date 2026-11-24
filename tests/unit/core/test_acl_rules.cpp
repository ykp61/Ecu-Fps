// ADR-033 / spec 2: kurulum klasoru ACL kurali (saf). Gercek dosya sistemi okunmaz.
#include "core/setup/acl_rules.h"
#include "ef_test.h"

using namespace ef::core::setup;

namespace {
constexpr std::uint32_t kFullAccess = 0x001F01FFu;  // FILE_ALL_ACCESS ("FA")
constexpr std::uint32_t kReadExecute = 0x001200A9u;
constexpr char kUsers[] = "S-1-5-32-545";
constexpr char kEveryone[] = "S-1-1-0";
constexpr char kSomeUser[] = "S-1-5-21-1-2-3-1001";

AclFacts good() {
    AclFacts f;
    f.dacl_present = true;
    f.dacl_protected = true;
    f.owner_sid = kSidAdministrators;
    f.aces = {{kSidSystem, kFullAccess, true}, {kSidAdministrators, kFullAccess, true}, {kUsers, kReadExecute, true}};
    return f;
}
}  // namespace

EF_TEST(acl_rules_good_install_dacl) { EF_CHECK(install_dir_acl_ok(good())); }

EF_TEST(acl_rules_unprotected_or_null_dacl_rejected) {
    auto f = good();
    f.dacl_protected = false;
    EF_CHECK(!install_dir_acl_ok(f));
    f = good();
    f.dacl_present = false;  // NULL DACL
    EF_CHECK(!install_dir_acl_ok(f));
}

EF_TEST(acl_rules_user_write_ace_rejected) {
    for (const std::uint32_t bit : {0x2u, 0x4u, 0x10u, 0x40u, 0x100u, 0x10000u, 0x40000u, 0x80000u, 0x10000000u, 0x40000000u}) {
        auto f = good();
        f.aces.push_back({kSomeUser, kReadExecute | bit, true});
        EF_CHECK(!install_dir_acl_ok(f));
    }
    auto f = good();
    f.aces.push_back({kUsers, 0x2u, true});  // Users yazma
    EF_CHECK(!install_dir_acl_ok(f));
}

EF_TEST(acl_rules_everyone_full_rejected) {
    auto f = good();
    f.aces.push_back({kEveryone, kFullAccess, true});
    EF_CHECK(!install_dir_acl_ok(f));
}

EF_TEST(acl_rules_wrong_owner_rejected) {
    auto f = good();
    f.owner_sid = kSomeUser;
    EF_CHECK(!install_dir_acl_ok(f));
    f.owner_sid = "";
    EF_CHECK(!install_dir_acl_ok(f));
    f.owner_sid = kSidSystem;
    EF_CHECK(install_dir_acl_ok(f));
}

EF_TEST(acl_rules_trusted_installer_allowed) {
    auto f = good();
    f.owner_sid = kSidTrustedInstaller;
    f.aces.push_back({kSidTrustedInstaller, kFullAccess, true});
    EF_CHECK(install_dir_acl_ok(f));
}

EF_TEST(acl_rules_deny_ace_does_not_grant) {
    auto f = good();
    f.aces.push_back({kEveryone, kFullAccess, false});  // reddeden ACE erisimi daraltir
    EF_CHECK(install_dir_acl_ok(f));
    f.aces.push_back({"", 0xFFFFFFFFu, true});  // cozulemeyen izin ACE'si guvenli ret
    EF_CHECK(!install_dir_acl_ok(f));
}
