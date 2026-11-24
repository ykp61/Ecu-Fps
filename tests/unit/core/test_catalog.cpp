// EF-X04/X05/X06: katalog dogrulama, surum siralama, guncelleme, bulten ve rehber eslesme. Fikstur verisi
// gercek uretici kaydi DEGILDIR.
#include <string>

#include "core/catalog/catalog.h"
#include "ef_test.h"

using namespace ef::core::catalog;

namespace {
const SignatureVerifier ok_sig = [](std::string_view, std::string_view s) { return s == "good"; };
std::string doc(const char* kind, int version, long long expires) {
    return std::string(R"({"schema":1,"kind":")") + kind + R"(","version":)" + std::to_string(version) + R"(,"issued_unix":10,"expires_unix":)" +
           std::to_string(expires) + "}";
}
}  // namespace

EF_TEST(x05_t03_unsigned_expired_and_replayed_catalog_rejected) {
    EF_CHECK(verify_catalog(doc("bios", 5, 1000), "good", ok_sig, "bios", 100, std::uint64_t{4}) == CatalogStatus::Ok);
    EF_CHECK(verify_catalog(doc("bios", 5, 1000), "", ok_sig, "bios", 100, std::nullopt) == CatalogStatus::BadSignature);
    EF_CHECK(verify_catalog(doc("bios", 5, 1000), "forged", ok_sig, "bios", 100, std::nullopt) == CatalogStatus::BadSignature);
    EF_CHECK(verify_catalog(doc("bios", 5, 1000), "good", {}, "bios", 100, std::nullopt) == CatalogStatus::BadSignature);  // anahtar yok
    EF_CHECK(verify_catalog(doc("bios", 5, 1000), "good", ok_sig, "bios", 1000, std::nullopt) == CatalogStatus::Expired);
    EF_CHECK(verify_catalog(doc("bios", 5, 1000), "good", ok_sig, "bios", 100, std::uint64_t{6}) == CatalogStatus::Rollback);  // eski kopya
    EF_CHECK(verify_catalog(doc("bios", 5, 1000), "good", ok_sig, "bios", 100, std::uint64_t{5}) == CatalogStatus::Rollback);  // ayni surum, baska bayt
    EF_CHECK(verify_catalog(doc("bios", 5, 1000), "good", ok_sig, "bios", 100, std::uint64_t{5}, true) == CatalogStatus::Ok);  // cevrimdisi onbellek
    EF_CHECK(verify_catalog(doc("bios", 0, 1000), "good", ok_sig, "bios", 100, std::nullopt) == CatalogStatus::Ok);  // ilk kurulum
    EF_CHECK(verify_catalog(doc("advisory", 5, 1000), "good", ok_sig, "bios", 100, std::nullopt) == CatalogStatus::WrongKind);
    EF_CHECK(verify_catalog("", "good", ok_sig, "bios", 100, std::nullopt) == CatalogStatus::Missing);
    EF_CHECK(verify_catalog(R"({"schema":2,"kind":"bios"})", "good", ok_sig, "bios", 100, std::nullopt) == CatalogStatus::UnknownSchema);
}

EF_TEST(x05_t02_vendor_version_order_branch_and_oem) {
    EF_CHECK(compare_vendor_versions("F9", "F10") == std::optional<int>(-1));
    EF_CHECK(compare_vendor_versions("1.10", "1.9") == std::optional<int>(1));
    EF_CHECK(compare_vendor_versions("1801", "1801") == std::optional<int>(0));
    EF_CHECK(!compare_vendor_versions("F10 beta", "F10"));  // beta kolu final ile siralanmaz
    EF_CHECK(compare_vendor_versions("F11 beta", "F10 BETA") == std::optional<int>(1));
    EF_CHECK(!compare_vendor_versions("P2.10", "F10"));     // farkli OEM bicimi
    EF_CHECK(!compare_vendor_versions("", "F1") && !compare_vendor_versions("abc", "abc"));
    EF_CHECK(compare_vendor_versions("2.0beta12", "2.1beta1") == std::optional<int>(-1));
    EF_CHECK(compare_vendor_versions("0x12B", "0x129") == std::optional<int>(1));  // onaltilik mikrokod  // beta ayiraci gruplari birlestirmez
}

EF_TEST(x05_t01_old_but_latest_bios_is_not_an_update) {
    const BoardIdentity b{"ACME", "X1", "1.0", ""};
    std::vector<BiosRelease> rel{{"ACME", "X1", "", "", "F9", false, "https://acme.example/x1", 1},
                                 {"ACME", "X1", "", "", "F10", false, "https://acme.example/x1", 2}};
    EF_CHECK(evaluate_bios_update(b, "F10", rel).state == UpdateState::Latest);  // tarih eski ama son surum
    EF_CHECK(evaluate_bios_update(b, "F9", rel).state == UpdateState::UpdateAvailable);
    rel.push_back({"ACME", "X1", "", "", "F11", true, "", 3});  // beta guncelleme sayilmaz
    EF_CHECK(evaluate_bios_update(b, "F10", rel).state == UpdateState::Latest);
    rel.push_back({"ACME", "X1", "2.0", "", "F12", false, "", 4});  // baska revizyon
    EF_CHECK(evaluate_bios_update(b, "F10", rel).state == UpdateState::Latest);
    EF_CHECK(evaluate_bios_update({"ACME", "X2", "1.0", ""}, "F10", rel).state == UpdateState::Unknown);  // katalogda yok
    EF_CHECK(evaluate_bios_update(b, "", rel).state == UpdateState::Unknown);
    // Harf ekli surum: siralanamayan uygulanabilir surum varken "en son" denmez.
    const std::vector<BiosRelease> gb{{"ACME", "X1", "", "", "F10a", false, "", 1}, {"ACME", "X1", "", "", "F11", false, "", 2}, {"ACME", "X1", "", "", "F12", false, "", 3}};
    EF_CHECK(evaluate_bios_update(b, "F10a", gb).state == UpdateState::Unknown);
    const std::vector<BiosRelease> msi{{"ACME", "X1", "", "", "7D25v1A", false, "", 1}, {"ACME", "X1", "", "", "7D25v1B", false, "", 2}};
    EF_CHECK(evaluate_bios_update(b, "7D25v1A", msi).state == UpdateState::Unknown);
}

EF_TEST(x06_three_state_advisories_and_revocation) {
    std::vector<Advisory> a{{"ADV-1", "intel", {"CPU-A 900K"}, "0x120", "0x1FF", "0x200", false, "https://example.invalid/adv1", 1}};
    EF_CHECK(evaluate_advisories("intel", "CPU-A 900K", "0x104", a).at(0).verdict == AdvisoryVerdict::NotAffected);  // hata oncesi surum
    EF_CHECK(evaluate_advisories("intel", "CPU-A 900K", "0x129", a).at(0).verdict == AdvisoryVerdict::Affected);
    EF_CHECK(evaluate_advisories("intel", "CPU-A 900K", "0x200", a).at(0).verdict == AdvisoryVerdict::NotAffected);
    EF_CHECK(evaluate_advisories("intel", "CPU-A 900K", "", a).at(0).verdict == AdvisoryVerdict::NotVerified);  // T02
    EF_CHECK(evaluate_advisories("intel", "CPU-A 900", "0x129", a).empty());  // T01: aralik disi model uyari almaz
    a[0].revoked = true;  // T03
    EF_CHECK(evaluate_advisories("intel", "CPU-A 900K", "0x129", a).empty());
}

EF_TEST(x04_t01_t02_t03_guide_match_image_fallback_and_restart_proof) {
    std::vector<BiosGuide> g{{"ACME", "X1", "1.0", "ami", "F1", "F20", "xmp", {{"guide.step.a", "img1"}, {"guide.step.b", ""}}}};
    EF_CHECK(match_guide({"ACME", "X1", "1.0", "", "AMI"}, "F10", "xmp", g) == &g[0]);
    EF_CHECK(!match_guide({"ACME", "X1", "1.1", "", "ami"}, "F10", "xmp", g));  // yanlis revizyon
    EF_CHECK(!match_guide({"ACME", "X1", "", "", "ami"}, "F10", "xmp", g));     // revizyon bilinmiyor
    EF_CHECK(!match_guide({"ACME", "X9", "1.0", "", "ami"}, "F10", "xmp", g));  // yalniz marka
    EF_CHECK(!match_guide({"ACME", "X1", "1.0", "", "ami"}, "F30", "xmp", g));  // surum araligi disi
    EF_CHECK(!match_guide({"ACME", "X1", "1.0", "", "insyde"}, "F10", "xmp", g));  // baska BIOS ailesi
    EF_CHECK(!match_guide({"ACME", "X1", "1.0", "", ""}, "F10", "xmp", g));
    const auto have = [](std::string_view id) { return id == "img1"; };
    EF_CHECK(step_shows_image(g[0].steps[0], have) && !step_shows_image(g[0].steps[1], have));
    EF_CHECK(!step_shows_image(g[0].steps[0], {}));
    EF_CHECK(xmp_after_restart(std::nullopt, 6000, true) == BiosChangeState::Unverified);  // "yaptim" kanit degil
    EF_CHECK(xmp_after_restart(4800, 6000, true) == BiosChangeState::NotActive);
    EF_CHECK(xmp_after_restart(6000, 6000, false) == BiosChangeState::Active);
}
