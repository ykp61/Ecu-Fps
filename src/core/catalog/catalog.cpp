#include "core/catalog/catalog.h"

#include <algorithm>
#include <cctype>

#include "core/serialization/json.h"

namespace ef::core::catalog {

namespace {

constexpr std::int64_t kSchema = 1;

std::string lower(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

struct Tokens {
    std::string skeleton;              // harf/ayirac iskeleti, rakam gruplari '#'
    std::vector<std::uint64_t> numbers;
    bool beta = false;
};

std::optional<Tokens> tokenize(std::string_view raw) {
    auto s = lower(trim(raw));
    Tokens t;
    if (const auto p = s.find("beta"); p != std::string::npos) {
        if (s.find("beta", p + 4) != std::string::npos) return std::nullopt;
        t.beta = true;
        s.replace(p, 4, "~");  // ayirac: komsu rakam gruplari birlesmez
    }
    s = trim(s);
    if (s.empty() || s.size() > 64) return std::nullopt;
    // Mikrokod gibi onaltilik surumler ("0x12B"): tek sayi olarak siralanir.
    if (s.size() > 2 && s.size() <= 18 && s[0] == '0' && s[1] == 'x' &&
        std::all_of(s.begin() + 2, s.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; })) {
        std::uint64_t v = 0;
        for (std::size_t i = 2; i < s.size(); ++i) v = v * 16 + static_cast<std::uint64_t>(std::isdigit(static_cast<unsigned char>(s[i])) ? s[i] - '0' : s[i] - 'a' + 10);
        t.skeleton = "0x#";
        t.numbers.push_back(v);
        return t;
    }
    for (std::size_t i = 0; i < s.size();) {
        if (std::isdigit(static_cast<unsigned char>(s[i]))) {
            std::uint64_t v = 0;
            std::size_t n = 0;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                if (++n > 12) return std::nullopt;
                v = v * 10 + static_cast<std::uint64_t>(s[i] - '0');
                ++i;
            }
            t.numbers.push_back(v);
            t.skeleton.push_back('#');
        } else {
            t.skeleton.push_back(s[i]);
            ++i;
        }
    }
    if (t.numbers.empty()) return std::nullopt;
    return t;
}

bool same_ci(std::string_view a, std::string_view b) { return lower(trim(a)) == lower(trim(b)); }

}  // namespace

std::string_view to_string(CatalogStatus s) noexcept {
    switch (s) {
        case CatalogStatus::Ok: return "ok";
        case CatalogStatus::Missing: return "missing";
        case CatalogStatus::BadSignature: return "bad_signature";
        case CatalogStatus::Malformed: return "malformed";
        case CatalogStatus::UnknownSchema: return "unknown_schema";
        case CatalogStatus::Expired: return "expired";
        case CatalogStatus::Rollback: return "rollback";
        case CatalogStatus::WrongKind: return "wrong_kind";
    }
    return "malformed";
}

CatalogStatus verify_catalog(std::string_view data, std::string_view signature, const SignatureVerifier& verify, std::string_view kind,
                             std::int64_t now_unix, std::optional<std::uint64_t> last_version, bool same_as_accepted,
                             CatalogEnvelope* out) {
    if (data.empty()) return CatalogStatus::Missing;
    if (data.size() > 4 * 1024 * 1024) return CatalogStatus::Malformed;
    if (!verify || signature.empty() || !verify(data, signature)) return CatalogStatus::BadSignature;  // once imza, sonra ayristirma
    const auto doc = json::parse(data);
    if (!doc.ok()) return CatalogStatus::Malformed;
    const auto schema = json::get_int(doc.value(), "schema", 0, 1000);
    if (!schema.ok()) return CatalogStatus::Malformed;
    if (schema.value() != kSchema) return CatalogStatus::UnknownSchema;
    const auto k = json::get_string(doc.value(), "kind", 32);
    const auto ver = json::get_int(doc.value(), "version", 0, 0x7FFFFFFFFFFFFFFFll);
    const auto issued = json::get_int(doc.value(), "issued_unix", 0, 0x7FFFFFFFFFFFFFFFll);
    const auto expires = json::get_int(doc.value(), "expires_unix", 0, 0x7FFFFFFFFFFFFFFFll);
    if (!k.ok() || !ver.ok() || !issued.ok() || !expires.ok()) return CatalogStatus::Malformed;
    if (k.value() != kind) return CatalogStatus::WrongKind;
    if (now_unix >= expires.value() || issued.value() > expires.value()) return CatalogStatus::Expired;
    const auto v = static_cast<std::uint64_t>(ver.value());
    if (last_version && (v < *last_version || (v == *last_version && !same_as_accepted))) return CatalogStatus::Rollback;  // eski kopya yeniden oynatilamaz
    if (out) *out = {k.value(), static_cast<std::uint64_t>(ver.value()), issued.value(), expires.value()};
    return CatalogStatus::Ok;
}

std::optional<int> compare_vendor_versions(std::string_view a, std::string_view b) {
    const auto x = tokenize(a), y = tokenize(b);
    if (!x || !y || x->skeleton != y->skeleton || x->beta != y->beta) return std::nullopt;  // farkli kol/OEM bicimi
    for (std::size_t i = 0; i < x->numbers.size(); ++i) {
        if (x->numbers[i] < y->numbers[i]) return -1;
        if (x->numbers[i] > y->numbers[i]) return 1;
    }
    return 0;
}

UpdateResult evaluate_bios_update(const BoardIdentity& board, std::string_view installed, const std::vector<BiosRelease>& releases) {
    UpdateResult r;
    if (board.vendor.empty() || board.model.empty() || installed.empty()) return r;
    const BiosRelease* newest = nullptr;
    bool installed_known = false, incomparable = false;
    for (const auto& rel : releases) {
        if (!same_ci(rel.board_vendor, board.vendor) || !same_ci(rel.board_model, board.model)) continue;
        // Revizyon/bolge: kayit belirtiyorsa kart da ayni olmali; kart bilinmiyorsa uygulanabilirlik kanitlanamaz.
        if (!rel.board_revision.empty() && (board.revision.empty() || !same_ci(rel.board_revision, board.revision))) continue;
        if (!rel.region.empty() && (board.region.empty() || !same_ci(rel.region, board.region))) continue;
        if (rel.beta) continue;
        const auto vs = compare_vendor_versions(rel.version, installed);
        if (!vs) {
            incomparable = true;  // F10a / 7D25v1B gibi: siralanamayan uygulanabilir surum gizlenmez
            continue;
        }
        installed_known = true;
        if (!newest || compare_vendor_versions(rel.version, newest->version).value_or(0) > 0) newest = &rel;
    }
    if (!installed_known || !newest) return r;  // karsilastirilabilir kayit yok: Unknown
    if (incomparable && compare_vendor_versions(newest->version, installed).value_or(0) <= 0) return r;  // "en son" kanitlanamaz
    r.newest = newest;
    r.state = compare_vendor_versions(newest->version, installed).value_or(0) > 0 ? UpdateState::UpdateAvailable : UpdateState::Latest;
    return r;
}

std::vector<AdvisoryResult> evaluate_advisories(std::string_view vendor, std::string_view model, std::string_view firmware,
                                                const std::vector<Advisory>& advisories) {
    std::vector<AdvisoryResult> out;
    for (const auto& a : advisories) {
        if (a.revoked || a.models.empty() || !same_ci(a.vendor, vendor)) continue;  // iptal edilen kayit etkin degil
        const bool model_hit = std::any_of(a.models.begin(), a.models.end(), [&](const std::string& m) { return same_ci(m, model); });
        if (!model_hit) continue;  // aralik disi cihaz uyari almaz
        AdvisoryResult r;
        r.advisory = &a;
        if (firmware.empty()) {
            r.verdict = AdvisoryVerdict::NotVerified;  // surum okunamadi: kesin denmez
        } else {
            const auto lo = a.affected_min.empty() ? std::optional<int>(1) : compare_vendor_versions(firmware, a.affected_min);
            const auto hi = a.affected_max.empty() ? std::optional<int>(-1) : compare_vendor_versions(firmware, a.affected_max);
            const auto fix = a.fixed_firmware.empty() ? std::optional<int>(-1) : compare_vendor_versions(firmware, a.fixed_firmware);
            if (!lo || !hi || !fix) r.verdict = AdvisoryVerdict::NotVerified;
            else if (*lo < 0 || *hi > 0 || *fix >= 0) r.verdict = AdvisoryVerdict::NotAffected;  // aralik disi veya duzeltilmis
            else r.verdict = AdvisoryVerdict::Affected;
        }
        out.push_back(r);
    }
    return out;
}

const BiosGuide* match_guide(const BoardIdentity& board, std::string_view bios_version, std::string_view setting,
                             const std::vector<BiosGuide>& guides) {
    if (board.vendor.empty() || board.model.empty() || board.revision.empty() || board.bios_family.empty() || bios_version.empty()) return nullptr;
    for (const auto& g : guides) {
        if (g.setting != setting || !same_ci(g.board_vendor, board.vendor) || !same_ci(g.board_model, board.model)) continue;
        if (!same_ci(g.board_revision, board.revision) || !same_ci(g.bios_family, board.bios_family)) continue;  // yanlis revizyon/aile
        const auto lo = compare_vendor_versions(bios_version, g.version_min);
        const auto hi = compare_vendor_versions(bios_version, g.version_max);
        if (lo && hi && *lo >= 0 && *hi <= 0) return &g;
    }
    return nullptr;
}

bool step_shows_image(const GuideStep& s, const std::function<bool(std::string_view)>& image_available) {
    return !s.image_id.empty() && image_available && image_available(s.image_id);
}

BiosChangeState xmp_after_restart(std::optional<std::uint32_t> configured_after, std::optional<std::uint32_t> rated, bool user_said_done) {
    (void)user_said_done;  // beyan olcum yerine gecmez
    if (!configured_after || !rated || *rated == 0) return BiosChangeState::Unverified;
    return *configured_after * 100 >= *rated * 95 ? BiosChangeState::Active : BiosChangeState::NotActive;
}

}  // namespace ef::core::catalog
