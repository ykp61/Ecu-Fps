#pragma once
// EF-X04/X05/X06: imzali, surumlu veri katalogu (saf; birim testlenir). Kod yurutme mekanizmasi DEGILDIR.
//  - Zarf imzasi tam bayt dizisi uzerinde dogrulanir; imzasiz, suresi dolmus veya eski surume donus reddedilir.
//  - Iptal edilen kayit onbellekte bile etkin sayilmaz.
//  - Dogrulanmis katalog yoksa ozellik hicbir iddia uretmez ("katalog yok"); icerik uydurulmaz.
//  - Surum karsilastirici: F9 < F10, 1.9 < 1.10; farkli bicim/kol/OEM karsilastirilmaz (bilinmiyor).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::catalog {

enum class CatalogStatus { Ok, Missing, BadSignature, Malformed, UnknownSchema, Expired, Rollback, WrongKind };
std::string_view to_string(CatalogStatus s) noexcept;

using SignatureVerifier = std::function<bool(std::string_view data, std::string_view signature)>;

struct CatalogEnvelope {
    std::string kind;              // "bios" / "advisory" / "bios_guide"
    std::uint64_t version = 0;     // monoton
    std::int64_t issued_unix = 0, expires_unix = 0;
};

// data: imzalanan JSON baytlari. last_version: daha once kabul edilen surum (yoksa nullopt). Eski surum reddedilir;
// ayni surum yalniz onbellekteki ayni kopya ise (same_as_accepted: bayt/ozet eslesmesi cagiranda) kabul edilir.
CatalogStatus verify_catalog(std::string_view data, std::string_view signature, const SignatureVerifier& verify, std::string_view kind,
                             std::int64_t now_unix, std::optional<std::uint64_t> last_version, bool same_as_accepted = false,
                             CatalogEnvelope* out = nullptr);

// ---- X05: uretici surum siralayici ----
// <0 a<b, 0 esit, >0 a>b; bicim (harf iskeleti) farkli veya beta/final karisik kol ise nullopt.
std::optional<int> compare_vendor_versions(std::string_view a, std::string_view b);

struct BiosRelease {
    std::string board_vendor, board_model, board_revision, region;  // bos revision/region = "tum"
    std::string version;
    bool beta = false;
    std::string source_url;
    std::int64_t release_unix = 0;
};
struct BoardIdentity {
    std::string vendor, model, revision, region;
    std::string bios_family;  // SMBIOS BIOS saglayicisi (AMI, Insyde ...); rehber eslesmesinin parcasi
};
enum class UpdateState { Unknown, Latest, UpdateAvailable };
struct UpdateResult {
    UpdateState state = UpdateState::Unknown;
    const BiosRelease* newest = nullptr;
};
// Tam uygulanabilirlik (uretici+model+revizyon+bolge) ve karsilastirilabilir surum yoksa Unknown. Uygulanabilir bir
// surum kurulu surumle karsilastirilamiyorsa (harf eki, farkli bicim) "en son" denmez: Unknown.
// Beta surum "guncelleme var" sayilmaz. Eski tarihli fakat en son surum: Latest.
UpdateResult evaluate_bios_update(const BoardIdentity& board, std::string_view installed, const std::vector<BiosRelease>& releases);

// ---- X06: donanim bultenleri ----
struct Advisory {
    std::string id, vendor;                 // "intel"/"amd"/kart ureticisi
    std::vector<std::string> models;        // tam model adlari; bos = gecersiz kayit
    std::string affected_min, affected_max;  // etkilenen firmware araligi (dahil); bos = acik uc
    std::string fixed_firmware;             // bu surum ve sonrasi duzeltilmis (bos = cozum yok)
    bool revoked = false;
    std::string source_url;
    std::int64_t verified_unix = 0;
};
enum class AdvisoryVerdict { Affected, NotVerified, NotAffected };
struct AdvisoryResult {
    const Advisory* advisory = nullptr;
    AdvisoryVerdict verdict = AdvisoryVerdict::NotVerified;
};
// model: tam CPU/kart modeli; firmware: kurulu surum (okunamadiysa bos). Nesil veya platform etiketi kullanilmaz.
std::vector<AdvisoryResult> evaluate_advisories(std::string_view vendor, std::string_view model, std::string_view firmware,
                                                const std::vector<Advisory>& advisories);

// ---- X04: BIOS rehberi eslestirme ----
struct GuideStep {
    std::string text_key;
    std::string image_id;  // bos veya varlik yok: aciklama gosterilir, sahte ekran degil
};
struct BiosGuide {
    std::string board_vendor, board_model, board_revision, bios_family;
    std::string version_min, version_max;  // kapsanan BIOS araligi
    std::string setting;                   // "xmp" / "rebar" ...
    std::vector<GuideStep> steps;
};
// Yalniz uretici+model+revizyon tam eslesirse ve BIOS surumu aralikta karsilastirilabilirse doner. Marka eslesmesi yetmez.
const BiosGuide* match_guide(const BoardIdentity& board, std::string_view bios_version, std::string_view setting,
                             const std::vector<BiosGuide>& guides);
// Adim gorseli yoksa aciklama metni gosterilir.
bool step_shows_image(const GuideStep& s, const std::function<bool(std::string_view)>& image_available);

// BIOS sonrasi dogrulama: kullanicinin "yaptim" demesi kanit degildir; yalniz yeniden okunan deger.
enum class BiosChangeState { Unverified, Active, NotActive };
BiosChangeState xmp_after_restart(std::optional<std::uint32_t> configured_mts_after, std::optional<std::uint32_t> rated_mts, bool user_said_done);

}  // namespace ef::core::catalog
