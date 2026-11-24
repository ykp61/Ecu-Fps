// R-C4: oyun sunucusu ping adresi dogrulamasi (saf cekirdek). Ag/ICMP burada calismaz.
#include <string>

#include "core/net/server_ping.h"
#include "core/settings/preferences.h"
#include "ef_test.h"

using ef::core::net::normalize_server_ipv4;

EF_TEST(rc4_server_ipv4_accepts_plain_dotted_quads) {
    EF_CHECK(normalize_server_ipv4("203.0.113.10") == std::string("203.0.113.10"));
    EF_CHECK(normalize_server_ipv4("  192.168.1.1\t") == std::string("192.168.1.1"));  // kenar boslugu atilir
    EF_CHECK(normalize_server_ipv4("10.0.0.0") == std::string("10.0.0.0"));
    EF_CHECK(normalize_server_ipv4("1.0.0.255") == std::string("1.0.0.255"));
    EF_CHECK(normalize_server_ipv4("223.255.255.254") == std::string("223.255.255.254"));
}

EF_TEST(rc4_server_ipv4_rejects_everything_else) {
    for (const char* bad : {"", " ", "1.2.3", "1.2.3.4.5", "1.2.3.", ".1.2.3", "1..2.3", "256.1.1.1", "1.1.1.256", "01.2.3.4", "1.2.3.004",
                            "1.2.3.4a", "a.b.c.d", "example.com", "1.2.3.-4", "1 2 3 4", "1,2,3,4", "::1", "1.2.3.4:80", "0x7f.0.0.1",
                            "0.0.0.0", "0.1.2.3", "127.0.0.1", "127.255.255.255", "224.0.0.1", "239.255.255.250", "240.0.0.1", "255.255.255.255",
                            "1111.1.1.1", "1.2.3.4444", "1234567890123456"})
        EF_CHECK(!normalize_server_ipv4(bad).has_value());
}

EF_TEST(rc4_server_ipv4_pref_round_trips_and_drops_invalid) {
    using ef::core::settings::Preferences;
    Preferences p;
    EF_CHECK(p.game_server_ipv4.empty());
    EF_CHECK(p.serialize().find("game_server_ipv4") == std::string::npos);  // bos: alan yazilmaz
    p.game_server_ipv4 = "203.0.113.10";
    auto r = Preferences::parse(p.serialize());
    EF_CHECK(r.ok() && r.value().game_server_ipv4 == "203.0.113.10");
    p.game_server_ipv4 = "not-an-ip";  // gecersiz: yazilmaz
    EF_CHECK(p.serialize().find("game_server_ipv4") == std::string::npos);
    r = Preferences::parse(R"({"schema_version":1,"game_server_ipv4":"127.0.0.1"})");  // elle bozulmus dosya: atilir
    EF_CHECK(r.ok() && r.value().game_server_ipv4.empty());
    r = Preferences::parse(R"({"schema_version":1,"game_server_ipv4":42})");
    EF_CHECK(r.ok() && r.value().game_server_ipv4.empty());
    r = Preferences::parse(R"({"schema_version":1,"game_server_ipv4":" 010.0.0.1"})");
    EF_CHECK(r.ok() && r.value().game_server_ipv4.empty());
}
