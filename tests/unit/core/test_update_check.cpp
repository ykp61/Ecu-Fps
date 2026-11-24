// Zorunlu guncelleme karar cekirdegi (spec 2026-11-11-zorunlu-guncelleme-design.md §6). Imza burada sahte dogrulayiciyla
// sinanir; gercek CNG dogrulamasi test_envelope.cpp'deki capraz vektorle kanitlidir. Ag yok.
#include <string>

#include "core/update/release_key.h"
#include "core/update/update_check.h"
#include "ef_test.h"

using namespace ef::core;
using namespace ef::core::update;

namespace {

std::string b64url(std::string_view in) {
    static constexpr char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    std::uint32_t acc = 0;
    int bits = 0;
    for (unsigned char c : in) {
        acc = (acc << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(t[(acc >> bits) & 63]);
        }
    }
    if (bits > 0) out.push_back(t[(acc << (6 - bits)) & 63]);
    return out;
}

const std::string kGoodSig(64, '\x01');
const std::string kBadSig(64, '\x02');

// Sahte imza: anahtar "K" ve imza 64 x 0x01 ise gecerli; mesaj alan + anahtar kimligi icermeli.
const catalog::RawVerifier fake_verify = [](std::string_view blob, std::string_view msg, std::string_view sig) {
    return blob == "K" && sig == kGoodSig && msg.starts_with("EcuFps:v1:release:rel1");
};
const catalog::KeyLookup keys = [](std::string_view d, std::string_view k) { return d == "release" && k == "rel1" ? std::string("K") : std::string(); };

std::string envelope(const std::string& payload, const std::string& sig = kGoodSig, const char* domain = "release") {
    return std::string(R"({"domain":")") + domain + R"(","key_id":"rel1","payload":")" + b64url(payload) + R"(","sig":")" + b64url(sig) + R"("})";
}

std::string manifest(const char* ver, const char* url = "https://github.com/ykp61/Ecu-Fps-releases/releases/download/v0.2.0/EcuFps.exe",
                     const char* arch = "x64", const char* channel = "alpha", const char* pub = "Ecu Fps", unsigned min_os = 19041, int mv = 5) {
    return std::string(R"({"version":")") + ver + R"(","arch":")" + arch + R"(","url":")" + url +
           R"(","sha256":"ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789","channel":")" + channel + R"(","publisher":")" + pub +
           R"(","min_os_build":)" + std::to_string(min_os) + R"(,"size":1234,"manifest_version":)" + std::to_string(mv) + "}";
}

catalog::ReleaseContext ctx() {
    catalog::ReleaseContext c;
    c.current_version = "0.1.0";
    c.publisher = "Ecu Fps";
    c.os_build = 26100;
    c.last_manifest_version = 4;
    c.trusted_release_key = true;
    return c;
}

UpdateOutcome decide(const std::string& env, bool writable = true, const catalog::ReleaseContext& c = ctx()) {
    return decide_update(std::string_view(env), c, keys, fake_verify, writable).outcome;
}

}  // namespace

EF_TEST(update_versions) {
    EF_CHECK(compare_versions("0.1.10", "0.1.9") > 0);
    EF_CHECK(compare_versions("0.1.9", "0.1.10") < 0);
    EF_CHECK(compare_versions("1.2.3", "1.2.3") == 0);
    EF_CHECK(compare_versions("0.2.0", "0.1.99") > 0);
    EF_CHECK(compare_versions("x", "0.0.0") < 0);  // bicimsiz en dusuk
    EF_CHECK(compare_versions("0.0.0", "x") > 0);
    EF_CHECK(compare_versions("1.2", "0.0.1") < 0);
    EF_CHECK(compare_versions("1.2.3.4", "0.0.1") < 0);
    EF_CHECK(compare_versions("1..3", "0.0.1") < 0);
    EF_CHECK(compare_versions("-1.0.0", "0.0.1") < 0);
}

EF_TEST(update_host_allowlist) {
    EF_CHECK(update_host_allowed("https://github.com/ykp61/Ecu-Fps-releases/releases/latest/download/manifest.json"));
    EF_CHECK(update_host_allowed("https://objects.githubusercontent.com/x?sig=1"));
    EF_CHECK(update_host_allowed("https://release-assets.githubusercontent.com/a"));
    EF_CHECK(update_host_allowed("HTTPS://GitHub.com/a"));
    EF_CHECK(!update_host_allowed("http://github.com/a"));
    EF_CHECK(!update_host_allowed("https://evil.com/a"));
    EF_CHECK(!update_host_allowed("https://github.com.evil.com/a"));
    EF_CHECK(!update_host_allowed("https://evil.com/github.com"));
    EF_CHECK(!update_host_allowed("https://user@github.com/a"));
    EF_CHECK(!update_host_allowed("https://github.com:8443/a"));
    EF_CHECK(!update_host_allowed("https://github.com./a"));
    EF_CHECK(!update_host_allowed("https://speed.cloudflare.com/__down"));
    EF_CHECK(!update_host_allowed(""));
}

// Denetim 3.4 acil fren: imzali "halt": true manifest zorunlu karti acmaz, yazmayi kilitlemez ve manifest_version'i
// kaydeder (durdurulmamis eski manifest yeniden oynatilamaz). 0.3.0/0.4.0 istemcileri alani tanimaz; yayin betigi
// onlar icin surumu 0.0.0 yazar (NotNewer). Imzasiz / bozuk halt alani kabul edilmez.
EF_TEST(update_decide_halt_stops_mandatory_update) {
    std::string m = manifest("0.2.0");
    m.insert(m.size() - 1, R"(,"halt":true)");
    const auto d = decide_update(std::string_view(envelope(m)), ctx(), keys, fake_verify, true);
    EF_CHECK(d.outcome == UpdateOutcome::UpToDate);
    EF_CHECK(d.verified_manifest_version == 5);
    EF_CHECK(writes_allowed(d.outcome));
    std::string off = manifest("0.2.0");
    off.insert(off.size() - 1, R"(,"halt":false)");
    EF_CHECK(decide(envelope(off)) == UpdateOutcome::Mandatory);
    std::string bad = manifest("0.2.0");
    bad.insert(bad.size() - 1, R"(,"halt":"yes")");
    EF_CHECK(decide(envelope(bad)) == UpdateOutcome::VerifyFailed);
    EF_CHECK(decide(envelope(m, kBadSig)) == UpdateOutcome::VerifyFailed);
    // Eski istemci bicimi: surum 0.0.0, halt alani yok sayilsa bile sunulmaz.
    EF_CHECK(decide(envelope(manifest("0.0.0", "https://github.com/ykp61/Ecu-Fps-releases/releases/download/v0.0.0/halt.exe"))) ==
              UpdateOutcome::UpToDate);
}

EF_TEST(update_decide_check_failed_without_envelope) {
    EF_CHECK(decide_update(std::nullopt, ctx(), keys, fake_verify, true).outcome == UpdateOutcome::CheckFailed);
}

EF_TEST(update_decide_mandatory_and_not_writable) {
    const auto d = decide_update(std::string_view(envelope(manifest("0.2.0"))), ctx(), keys, fake_verify, true);
    EF_CHECK(d.outcome == UpdateOutcome::Mandatory && d.manifest.version == "0.2.0" && d.manifest.size == 1234);
    EF_CHECK(decide(envelope(manifest("0.2.0")), false) == UpdateOutcome::NotWritable);
}

EF_TEST(update_decide_verify_failed_signature_and_domain) {
    EF_CHECK(decide(envelope(manifest("0.2.0"), kBadSig)) == UpdateOutcome::VerifyFailed);
    EF_CHECK(decide(envelope(manifest("0.2.0"), kGoodSig, "entitlement")) == UpdateOutcome::VerifyFailed);
    EF_CHECK(decide("not json") == UpdateOutcome::VerifyFailed);
    EF_CHECK(decide(envelope("{\"version\":1}")) == UpdateOutcome::VerifyFailed);  // Malformed
}

EF_TEST(update_decide_verify_failed_manifest_rejections) {
    EF_CHECK(decide(envelope(manifest("0.2.0", "https://github.com/ykp61/Ecu-Fps-releases/releases/download/v0.2.0/EcuFps.exe", "x64", "alpha",
                                      "Evil Corp"))) == UpdateOutcome::VerifyFailed);  // WrongPublisher
    EF_CHECK(decide(envelope(manifest("0.2.0", "https://github.com/ykp61/Ecu-Fps-releases/releases/download/v0.2.0/EcuFps.exe", "x64", "alpha",
                                      "Ecu Fps", 19041, 4))) == UpdateOutcome::VerifyFailed);  // Replay
    EF_CHECK(decide(envelope(manifest("0.2.0", "http://github.com/ykp61/Ecu-Fps-releases/releases/download/v0.2.0/EcuFps.exe"))) ==
              UpdateOutcome::VerifyFailed);  // BadUrl
    EF_CHECK(decide(envelope(manifest("0.2.0", "https://evil.com/EcuFps.exe"))) == UpdateOutcome::VerifyFailed);
    EF_CHECK(decide(envelope(manifest("0.2.0", "https://github.com/other/repo/releases/download/v0.2.0/EcuFps.exe"))) == UpdateOutcome::VerifyFailed);
    EF_CHECK(decide(envelope(manifest("0.2.0", "https://github.com/ykp61/Ecu-Fps-releases/releases/download/../../x/EcuFps.exe"))) ==
              UpdateOutcome::VerifyFailed);
    EF_CHECK(decide(envelope(manifest("0.2.0", "https://github.com/ykp61/Ecu-Fps-releases/releases/download/"))) == UpdateOutcome::VerifyFailed);
    auto unsigned_ctx = ctx();
    unsigned_ctx.trusted_release_key = false;  // gomulu anahtar yok: UnsignedBuild, indirme yok
    EF_CHECK(decide(envelope(manifest("0.2.0")), true, unsigned_ctx) == UpdateOutcome::VerifyFailed);
}

EF_TEST(update_decide_up_to_date_and_not_targeted) {
    EF_CHECK(decide(envelope(manifest("0.1.0"))) == UpdateOutcome::UpToDate);  // NotNewer (esit)
    EF_CHECK(decide(envelope(manifest("0.0.9"))) == UpdateOutcome::UpToDate);  // NotNewer (eski)
    const char* url = "https://github.com/ykp61/Ecu-Fps-releases/releases/download/v0.2.0/EcuFps.exe";
    EF_CHECK(decide(envelope(manifest("0.2.0", url, "arm64"))) == UpdateOutcome::UpToDate);                         // WrongArch
    EF_CHECK(decide(envelope(manifest("0.2.0", url, "x64", "stable"))) == UpdateOutcome::UpToDate);                // WrongChannel
    EF_CHECK(decide(envelope(manifest("0.2.0", url, "x64", "alpha", "Ecu Fps", 30000))) == UpdateOutcome::UpToDate);  // OsTooOld
    EF_CHECK(decide(envelope(manifest("0.1.0")), false) == UpdateOutcome::UpToDate);  // guncelse yazilabilirlik onemsiz
}

// Spec §2 tekrar oynatma: kalici en yuksek dogrulanmis manifest_version start_update_check'te replay_floor ile baglama
// yazilir. Ayni (en son) manifest her acilista yeniden kabul edilir; daha eski imzali manifest (dondurma / eski surume
// zorlama) Replay -> VerifyFailed. Taban yalniz dogrulanmis karardan sonra yukselir, asla geriye gitmez.
EF_TEST(update_replay_floor_from_persisted_highest_seen) {
    EF_CHECK(replay_floor(0) == 0);
    EF_CHECK(replay_floor(7) == 6);
    const char* url = "https://github.com/ykp61/Ecu-Fps-releases/releases/download/v0.2.0/EcuFps.exe";
    auto c = ctx();
    std::uint64_t seen = 0;  // ilk acilis: hic manifest gorulmedi
    c.last_manifest_version = replay_floor(seen);
    auto d = decide_update(std::string_view(envelope(manifest("0.3.0", url, "x64", "alpha", "Ecu Fps", 19041, 9))), c, keys, fake_verify, true);
    EF_CHECK(d.outcome == UpdateOutcome::Mandatory && d.verified_manifest_version == 9);
    seen = next_highest_seen(seen, d);
    EF_CHECK(seen == 9);
    c.last_manifest_version = replay_floor(seen);
    // Ayni manifest yeniden: kabul (her acilista ayni "latest" indirilir).
    EF_CHECK(decide(envelope(manifest("0.3.0", url, "x64", "alpha", "Ecu Fps", 19041, 9)), true, c) == UpdateOutcome::Mandatory);
    // Eski imzali manifest, surumu calisan surume esit: dondurma saldirisi (UpToDate olurdu) -> Replay.
    c.current_version = "0.2.0";
    EF_CHECK(decide(envelope(manifest("0.2.0", url, "x64", "alpha", "Ecu Fps", 19041, 8)), true, c) == UpdateOutcome::VerifyFailed);
    // Eski ama calisandan yeni (yerini almis) surume zorlama -> Replay.
    c.current_version = "0.1.0";
    EF_CHECK(decide(envelope(manifest("0.2.0", url, "x64", "alpha", "Ecu Fps", 19041, 8)), true, c) == UpdateOutcome::VerifyFailed);
    // Reddedilen karar tabani degistirmez; dogrulanmis daha dusuk karar da geriye yazmaz.
    const auto rejected = decide_update(std::string_view(envelope(manifest("0.2.0"), kBadSig)), c, keys, fake_verify, true);
    EF_CHECK(rejected.verified_manifest_version == 0 && next_highest_seen(seen, rejected) == 9);
    UpdateDecision lower;
    lower.verified_manifest_version = 3;
    EF_CHECK(next_highest_seen(seen, lower) == 9);
    // UpToDate de dogrulanmis karardir: taban yukselir (sonraki eski manifest yine reddedilir).
    c.current_version = "0.3.0";
    const auto up = decide_update(std::string_view(envelope(manifest("0.3.0", url, "x64", "alpha", "Ecu Fps", 19041, 10))), c, keys, fake_verify, true);
    EF_CHECK(up.outcome == UpdateOutcome::UpToDate && next_highest_seen(seen, up) == 10);
}

EF_TEST(update_download_matches) {
    catalog::ReleaseManifest m;
    m.size = 1234;
    m.sha256 = "ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
    EF_CHECK(download_matches(1234, "abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789", m));
    EF_CHECK(!download_matches(1233, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789", m));
    EF_CHECK(!download_matches(1234, "bbcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789", m));
    EF_CHECK(!download_matches(1234, "abcdef", m));
    m.size = 0;
    EF_CHECK(!download_matches(0, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789", m));
}

EF_TEST(update_writes_gate) {
    EF_CHECK(!writes_allowed(UpdateOutcome::Checking));
    EF_CHECK(!writes_allowed(UpdateOutcome::Mandatory));
    EF_CHECK(!writes_allowed(UpdateOutcome::NotWritable));
    EF_CHECK(writes_allowed(UpdateOutcome::UpToDate));
    EF_CHECK(writes_allowed(UpdateOutcome::CheckFailed));
    EF_CHECK(writes_allowed(UpdateOutcome::VerifyFailed));
}

EF_TEST(update_swap_paths_same_folder) {
    const auto p = update_swap_paths("C:\\Apps\\EcuFps.exe");
    EF_CHECK(p.exe == "C:\\Apps\\EcuFps.exe");
    EF_CHECK(p.staged == "C:\\Apps\\EcuFps.exe.new");
    EF_CHECK(p.backup == "C:\\Apps\\EcuFps.exe.old");
    EF_CHECK(p.marker == "C:\\Apps\\EcuFps.exe.updated");
    EF_CHECK(update_host_allowed(kReleasePageUrl));
}

// Task 16: release_sign.py keygen ciktisi. Anahtar yokken ikisi de bos (UnsignedBuild); varken blob
// sizeof(BCRYPT_ECCKEY_BLOB) + 64 = 72 bayt, 'ECS1' (P-256 genel), cbKey 32; key_id zarf sinirina (64) uyar.
EF_TEST(release_key_header_is_well_formed) {
    if (!has_release_key()) {
        EF_CHECK(kReleaseKeyId.empty() && kReleasePublicBlob.empty());
        return;
    }
    constexpr std::size_t kEccKeyBlobHeader = 8;  // BCRYPT_ECCKEY_BLOB { ULONG dwMagic; ULONG cbKey; }
    EF_CHECK(kReleasePublicBlob.size() == kEccKeyBlobHeader + 64);
    EF_CHECK(kReleasePublicBlob.substr(0, 4) == "ECS1");
    EF_CHECK(kReleasePublicBlob[4] == 32 && kReleasePublicBlob[5] == 0 && kReleasePublicBlob[6] == 0 && kReleasePublicBlob[7] == 0);
    EF_CHECK(kReleaseKeyId.size() <= 64);
    for (char c : kReleaseKeyId)
        EF_CHECK((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_');
}
