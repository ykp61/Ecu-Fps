#include "contracts/inventory.h"
#include "ef_test.h"

using namespace ef::contracts;

EF_TEST(target_user_is_session_user_when_same) {
    UserContext u;
    u.process_user_sid = Field<std::string>::known("S-1-5-21-1-2-3-1001", Provenance::Api);
    u.session_user_sid = Field<std::string>::known("S-1-5-21-1-2-3-1001", Provenance::Api);
    resolve_target_user(u);
    EF_CHECK(u.target_user_sid.value == "S-1-5-21-1-2-3-1001");
    EF_CHECK(u.different_admin.value == false);
}

EF_TEST(target_user_survives_different_admin_elevation) {
    // Oyuncu standart kullanici; UAC baska bir yonetici hesabiyla onaylandi.
    UserContext u;
    u.process_user_sid = Field<std::string>::known("S-1-5-21-1-2-3-500", Provenance::Api);  // admin
    u.session_user_sid = Field<std::string>::known("S-1-5-21-1-2-3-1001", Provenance::Api); // oyuncu
    u.elevated = Field<bool>::known(true, Provenance::Api);
    resolve_target_user(u);
    EF_CHECK(u.target_user_sid.value == "S-1-5-21-1-2-3-1001");
    EF_CHECK(u.different_admin.value == true);
}

EF_TEST(unknown_session_user_keeps_target_unknown) {
    UserContext u;
    u.process_user_sid = Field<std::string>::known("S-1-5-21-1-2-3-500", Provenance::Api);
    u.session_user_sid = Field<std::string>::unknown("WTSQuerySessionInformation failed (5)");
    resolve_target_user(u);
    EF_CHECK(!u.target_user_sid.has());  // islem kullanicisina geri dusulmez
    EF_CHECK(!u.target_user_sid.reason.empty());
    EF_CHECK(!u.different_admin.has());
}

EF_TEST(field_unknown_is_not_default_value) {
    const auto f = Field<int>::unknown("no system battery");
    EF_CHECK(!f.has());
    EF_CHECK(f.source == Provenance::None);
    EF_CHECK(f.reason == "no system battery");
    const auto k = Field<int>::known(0, Provenance::Api);
    EF_CHECK(k.has());
    EF_CHECK(*k.value == 0);
}
