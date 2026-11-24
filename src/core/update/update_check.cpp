#include "core/update/update_check.h"

namespace ef::core::update {

namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

// "1.2.3" -> uc sayi; bicimsizse nullopt.
std::optional<std::array<std::uint32_t, 3>> parse_version(std::string_view v) {
    std::array<std::uint32_t, 3> out{};
    std::size_t part = 0, digits = 0;
    std::uint64_t acc = 0;
    for (std::size_t i = 0; i <= v.size(); ++i) {
        if (i == v.size() || v[i] == '.') {
            if (digits == 0 || part >= 3) return std::nullopt;
            out[part++] = static_cast<std::uint32_t>(acc);
            acc = 0;
            digits = 0;
            continue;
        }
        const char c = v[i];
        if (c < '0' || c > '9' || ++digits > 9) return std::nullopt;
        acc = acc * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (part != 3) return std::nullopt;
    return out;
}

}  // namespace

// Yayin dosyasi yalniz sabit depo onekinin altinda; sorgu, parca, kacis veya ust dizin yok.
bool release_url_allowed(std::string_view url) {
    if (url.substr(0, kReleaseRepoPrefix.size()) != kReleaseRepoPrefix) return false;
    const auto rest = url.substr(kReleaseRepoPrefix.size());
    if (rest.empty() || rest.find("..") != std::string_view::npos) return false;
    for (char c : rest) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == '/';
        if (!ok) return false;
    }
    return true;
}

int compare_versions(std::string_view a, std::string_view b) {
    const auto pa = parse_version(a), pb = parse_version(b);
    if (!pa && !pb) return 0;
    if (!pa) return -1;
    if (!pb) return 1;
    for (std::size_t i = 0; i < 3; ++i)
        if ((*pa)[i] != (*pb)[i]) return (*pa)[i] < (*pb)[i] ? -1 : 1;
    return 0;
}

bool update_host_allowed(std::string_view url) {
    constexpr std::string_view kScheme = "https://";
    if (url.size() > 2048 || !iequals(url.substr(0, kScheme.size()), kScheme)) return false;
    const auto rest = url.substr(kScheme.size());
    const auto end = rest.find_first_of("/?#");
    const auto host = rest.substr(0, end);
    if (host.empty() || host.find_first_of("@:\\ ") != std::string_view::npos) return false;  // kullanici bilgisi, port, kacis yok
    for (const auto allowed : kUpdateHosts)
        if (iequals(host, allowed)) return true;
    return false;
}

UpdateDecision decide_update(std::optional<std::string_view> envelope_json, const catalog::ReleaseContext& ctx, const catalog::KeyLookup& keys,
                             const catalog::RawVerifier& verify, bool exe_dir_writable) {
    UpdateDecision d;
    if (!envelope_json) {
        d.outcome = UpdateOutcome::CheckFailed;
        return d;
    }
    const auto payload = catalog::open_envelope(*envelope_json, kReleaseDomain, keys, verify);
    if (!payload) {
        d.outcome = UpdateOutcome::VerifyFailed;
        return d;
    }
    catalog::ReleaseManifest m;
    const auto verdict = catalog::evaluate_release(*payload, ctx, &m);
    using V = catalog::ReleaseVerdict;
    switch (verdict) {
        case V::Malformed:
        case V::WrongPublisher:
        case V::Replay:
        case V::BadUrl:
        case V::UnsignedBuild:  // gomulu anahtar yok: dogrulanamaz, indirme yapilmaz
            d.outcome = UpdateOutcome::VerifyFailed;
            return d;
        default:
            break;
    }
    if (!release_url_allowed(m.url)) {
        d.outcome = UpdateOutcome::VerifyFailed;
        return d;
    }
    d.verified_manifest_version = m.manifest_version;
    switch (verdict) {
        case V::Offer:
            d.outcome = exe_dir_writable ? UpdateOutcome::Mandatory : UpdateOutcome::NotWritable;
            d.manifest = m;
            return d;
        default:  // NotNewer, WrongArch, WrongChannel, OsTooOld: bu kurulum icin sunulmaz
            d.outcome = UpdateOutcome::UpToDate;
            return d;
    }
}

std::uint64_t replay_floor(std::uint64_t highest_seen) { return highest_seen > 0 ? highest_seen - 1 : 0; }

std::uint64_t next_highest_seen(std::uint64_t highest_seen, const UpdateDecision& d) {
    return d.verified_manifest_version > highest_seen ? d.verified_manifest_version : highest_seen;
}

bool download_matches(std::uint64_t size, std::string_view sha256_hex, const catalog::ReleaseManifest& m) {
    if (m.size == 0 || size != m.size || sha256_hex.size() != 64 || m.sha256.size() != 64) return false;
    for (std::size_t i = 0; i < 64; ++i) {
        const char c = lower(sha256_hex[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) || c != lower(m.sha256[i])) return false;
    }
    return true;
}

catalog::SwapPaths update_swap_paths(const std::string& exe) { return {exe, exe + ".new", exe + ".old", exe + ".updated"}; }

bool writes_allowed(UpdateOutcome o) {
    return o == UpdateOutcome::UpToDate || o == UpdateOutcome::CheckFailed || o == UpdateOutcome::VerifyFailed;
}

}  // namespace ef::core::update
