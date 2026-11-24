// EF-X26/X27/X30 istemci tarafi. Capraz vektor yerel servisin (Python cryptography) urettigi zarftir ve
// Windows CNG ile dogrulanir; anahtar test anahtaridir, urun anahtari degil.
#include <fstream>
#include <map>
#include <sstream>

#include "core/catalog/envelope.h"
#include "core/serialization/json.h"
#include "ef_test.h"
#include "platform/windows/catalog_signature.h"

using namespace ef::core::catalog;

namespace {
std::string read_fixture(const char* name) {
    for (const char* base : {"tests/fixtures/expansion/", "../../tests/fixtures/expansion/", "../../../tests/fixtures/expansion/", EF_SOURCE_ROOT "/tests/fixtures/expansion/"}) {
        std::ifstream f(std::string(base) + name, std::ios::binary);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }
    }
    return {};
}
const RawVerifier cng = [](std::string_view blob, std::string_view msg, std::string_view sig) { return ef::platform::verify_ecdsa_p256(blob, msg, sig); };
}  // namespace

EF_TEST(x27_cross_vector_python_signed_cng_verified) {
    const auto text = read_fixture("envelope_vector.json");
    EF_CHECK(!text.empty());
    if (text.empty()) return;
    const auto doc = ef::core::json::parse(text);
    EF_CHECK(doc.ok());
    const auto blob = base64url_decode(ef::core::json::get_string(doc.value(), "public_blob", 400).value());
    const auto* env = doc.value().find("envelope");
    EF_CHECK(blob && env);
    const std::string env_text = ef::core::json::write(*env);
    const KeyLookup keys = [&](std::string_view d, std::string_view k) { return d == "entitlement" && k == "vec1" ? *blob : std::string(); };
    const auto payload = open_envelope(env_text, "entitlement", keys, cng);
    EF_CHECK(payload.has_value());
    Entitlement e;
    EF_CHECK(payload && check_entitlement(*payload, "abcdefabcdefabcd", &e) == EntitlementState::Valid && e.license_id == 7);
    EF_CHECK(payload && check_entitlement(*payload, "baska-cihaz-0001") == EntitlementState::WrongDevice);  // T04
    EF_CHECK(!open_envelope(env_text, "update", keys, cng));  // alan degisirse dogrulama bozulur
    const KeyLookup none = [](std::string_view, std::string_view) { return std::string(); };
    EF_CHECK(!open_envelope(env_text, "entitlement", none, cng));  // anahtar izinli degil
    std::string tampered = env_text;
    const auto p = tampered.find("\"payload\":\"") + 12;
    tampered[p] = tampered[p] == 'A' ? 'B' : 'A';
    EF_CHECK(!open_envelope(tampered, "entitlement", keys, cng));  // T02
    EF_CHECK(!rollback_requires_license());  // T03: geri alma lisansa bagli degil
}

EF_TEST(x26_t01_release_manifest_rejections) {
    ReleaseContext ctx;
    ctx.current_version = "0.1.0";
    ctx.publisher = "Ecu Fps";
    ctx.os_build = 26100;
    ctx.last_manifest_version = 3;
    ctx.trusted_release_key = true;
    const auto m = [](const char* ver, const char* arch, unsigned min_os, const char* url, int mv) {
        return std::string(R"({"version":")") + ver + R"(","arch":")" + arch + R"(","url":")" + url +
               R"(","sha256":"0000000000000000000000000000000000000000000000000000000000000000","channel":"alpha","publisher":"Ecu Fps","min_os_build":)" +
               std::to_string(min_os) + R"(,"size":100,"manifest_version":)" + std::to_string(mv) + "}";
    };
    EF_CHECK(evaluate_release(m("0.2.0", "x64", 19041, "https://example.invalid/a.zip", 4), ctx) == ReleaseVerdict::Offer);
    EF_CHECK(evaluate_release(m("0.2.0", "arm64", 19041, "https://example.invalid/a.zip", 4), ctx) == ReleaseVerdict::WrongArch);
    EF_CHECK(evaluate_release(m("0.2.0", "x64", 30000, "https://example.invalid/a.zip", 4), ctx) == ReleaseVerdict::OsTooOld);
    EF_CHECK(evaluate_release(m("0.2.0", "x64", 19041, "http://example.invalid/a.zip", 4), ctx) == ReleaseVerdict::BadUrl);
    EF_CHECK(evaluate_release(m("0.2.0", "x64", 19041, "https://user@example.invalid/a.zip", 4), ctx) == ReleaseVerdict::BadUrl);
    EF_CHECK(evaluate_release(m("0.2.0", "x64", 19041, "https://example.invalid/a.zip", 3), ctx) == ReleaseVerdict::Replay);
    EF_CHECK(evaluate_release(m("0.1.0", "x64", 19041, "https://example.invalid/a.zip", 4), ctx) == ReleaseVerdict::NotNewer);
    EF_CHECK(evaluate_release(m("0.1.0.1", "x64", 19041, "https://example.invalid/a.zip", 4), ctx) == ReleaseVerdict::Malformed);
    {
        auto other = ctx;
        other.publisher = "Evil Corp";
        EF_CHECK(evaluate_release(m("0.2.0", "x64", 19041, "https://example.invalid/a.zip", 4), other) == ReleaseVerdict::WrongPublisher);
        other = ctx;
        other.channel = "stable";
        EF_CHECK(evaluate_release(m("0.2.0", "x64", 19041, "https://example.invalid/a.zip", 4), other) == ReleaseVerdict::WrongChannel);
    }
    ctx.trusted_release_key = false;  // B-003/ADR-028: gomulu yayin anahtari yoksa guncelleme calistirilmaz
    EF_CHECK(evaluate_release(m("0.2.0", "x64", 19041, "https://example.invalid/a.zip", 4), ctx) == ReleaseVerdict::UnsignedBuild);
}

EF_TEST(x26_t02_t03_t04_power_loss_at_every_step_recovers) {
    for (int stop = 0; stop <= 3; ++stop) {
        for (bool healthy : {false, true}) {
            std::map<std::string, std::string> files{{"app.exe", "old"}, {"app.exe.new", "new"}, {"journal/tx.json", "j"}, {"license.dat", "l"}};
            SwapFs fs;
            fs.exists = [&](const std::string& p) { return files.count(p) > 0; };
            fs.move = [&](const std::string& a, const std::string& b) {
                if (!files.count(a)) return false;
                files[b] = files[a];
                files.erase(a);
                return true;
            };
            fs.remove = [&](const std::string& p) { return files.erase(p) > 0; };
            const SwapPaths p{"app.exe", "app.exe.new", "app.exe.bak", "app.exe.ok"};
            (void)run_swap(fs, p, static_cast<SwapStep>(stop + 1));  // guc kaybi bu adimdan sonra
            const bool new_ran = healthy && stop >= 2 && files.count("app.exe") && files["app.exe"] == "new";
            EF_CHECK(recover_swap(fs, p, new_ran));
            EF_CHECK(files.count("app.exe") == 1);                    // calisan bir surum her zaman var
            if (!new_ran) EF_CHECK(files["app.exe"] == "old");        // T03: yeni surum calismadiysa eski geri
            EF_CHECK(files["journal/tx.json"] == "j" && files["license.dat"] == "l");  // T04: kayitlara dokunulmaz
        }
    }
}

EF_TEST(x30_t01_t02_t03_notices) {
    const auto item = [](const char* id, const char* title, const char* text, const char* link) {
        return std::string(R"({"id":")") + id + R"(","title":")" + title + R"(","text":")" + text + R"(","link":")" + link +
               R"(","expires_at":2000,"min_os_build":19041,"min_product_version":"0.1.0"})";
    };
    const auto feed = [](const std::string& items) { return std::string(R"({"items":[)") + items + "]}"; };
    const auto ok = parse_notices(feed(item("n1", "0.2", "Yenilikler", "https://example.invalid/n1")));
    EF_CHECK(ok && ok->size() == 1);
    EF_CHECK(!parse_notices(feed(item("n2", "x", "<script>x</script>", "https://example.invalid"))));
    EF_CHECK(!parse_notices(feed(item("n2", "<b>x</b>", "x", "https://example.invalid"))));
    EF_CHECK(!parse_notices(feed(item("n3", "x", "x", "javascript:alert(1)"))));
    EF_CHECK(!parse_notices(feed(item("", "x", "x", "https://example.invalid"))));
    EF_CHECK(!parse_notices(feed(item("n1", "x", "x", "https://example.invalid") + "," + item("n1", "y", "y", "https://example.invalid"))));
    const auto& n = ok->front();
    NoticeContext c;
    c.opted_in = true;
    c.now_unix = 1000;
    c.os_build = 26100;
    c.product_version = "0.1.0";
    EF_CHECK(notice_visible(n, c));
    auto x = c;
    x.opted_in = false;
    EF_CHECK(!notice_visible(n, x));  // opt-in varsayilan kapali
    x = c;
    x.game_or_test_running = true;
    EF_CHECK(!notice_visible(n, x));  // T01
    x = c;
    x.dismissed = {"n1"};
    EF_CHECK(!notice_visible(n, x));  // T03
    x = c;
    x.now_unix = 2000;
    EF_CHECK(!notice_visible(n, x));  // suresi doldu
    x = c;
    x.os_build = 17763;
    EF_CHECK(!notice_visible(n, x));  // hedef disi OS
}

EF_TEST(x27_base64url_strict) {
    EF_CHECK(base64url_decode("aGk") == std::optional<std::string>("hi"));
    EF_CHECK(!base64url_decode("aGk=") && !base64url_decode("a+b/") && !base64url_decode("a"));
    EF_CHECK(!base64url_decode("aGl"));  // kanonik olmayan son bitler
}

// Saglikli onay yedegi silemezse (eski surec hala calisiyor) saglik isareti korunur: sonraki acilis isarete bakarak
// onaylar; isaretsiz yedek kanitlanmamis degisim sayilir.
EF_TEST(update_recover_healthy_keeps_marker_until_backup_removed) {
    std::map<std::string, std::string> files{{"app.exe", "new"}, {"app.exe.bak", "old"}, {"app.exe.ok", "1"}};
    bool backup_locked = true;
    SwapFs fs;
    fs.exists = [&](const std::string& p) { return files.count(p) > 0; };
    fs.move = [&](const std::string& a, const std::string& b) {
        if (!files.count(a)) return false;
        files[b] = files[a];
        files.erase(a);
        return true;
    };
    fs.remove = [&](const std::string& p) { return !(backup_locked && p == "app.exe.bak") && files.erase(p) > 0; };
    const SwapPaths p{"app.exe", "app.exe.new", "app.exe.bak", "app.exe.ok"};
    EF_CHECK(!recover_swap(fs, p, true));
    EF_CHECK(files.count("app.exe.bak") && files.count("app.exe.ok") && files["app.exe"] == "new");
    backup_locked = false;
    EF_CHECK(recover_swap(fs, p, true));
    EF_CHECK(!files.count("app.exe.bak") && !files.count("app.exe.ok") && files["app.exe"] == "new");
}
