#include "core/inventory/disk_space.h"

#include <algorithm>
#include <charconv>
#include <cmath>

#include "core/serialization/json.h"

namespace ef::core::inventory {

std::string_view to_string(SpaceStatus s) noexcept {
    switch (s) {
        case SpaceStatus::Ok: return "ok";
        case SpaceStatus::Warning: return "warning";
        case SpaceStatus::Critical: return "critical";
        case SpaceStatus::Unknown: return "unknown";
    }
    return "unknown";
}

namespace {

// VDF/ACF: "anahtar" "deger" ciftleri ve { } bloklari. Kacislar: \\ ve \".
struct VdfToken {
    enum Kind { Str, Open, Close, End, Invalid } kind;
    std::string text;
};

VdfToken next_token(std::string_view s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    if (i >= s.size()) return {VdfToken::End, {}};
    if (s[i] == '{') return ++i, VdfToken{VdfToken::Open, {}};
    if (s[i] == '}') return ++i, VdfToken{VdfToken::Close, {}};
    if (s[i] != '"') return i = s.size(), VdfToken{VdfToken::Invalid, {}};  // beklenmeyen: dur (bozuk)
    ++i;
    std::string out;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
            out.push_back(s[i + 1]);
            i += 2;
            continue;
        }
        out.push_back(s[i++]);
    }
    if (i >= s.size()) return {VdfToken::Invalid, {}};  // kapanmayan dize: dosya kesilmis
    ++i;
    return {VdfToken::Str, std::move(out)};
}

// Tum (anahtar, deger) ciftlerini derinlik bilgisiyle gezer.
template <typename F>
void walk_vdf(std::string_view s, F&& f) {
    std::size_t i = 0;
    int depth = 0;
    std::string pending;
    bool have_key = false;
    for (int guard = 0; guard < 200000; ++guard) {
        auto t = next_token(s, i);
        if (t.kind == VdfToken::End || t.kind == VdfToken::Invalid) return;
        if (t.kind == VdfToken::Open) {
            ++depth;
            have_key = false;
        } else if (t.kind == VdfToken::Close) {
            if (--depth < 0) return;
            have_key = false;
        } else if (!have_key) {
            pending = std::move(t.text);
            have_key = true;
        } else {
            f(depth, pending, t.text);
            have_key = false;
        }
    }
}

std::optional<std::uint64_t> to_u64(std::string_view s) {
    std::uint64_t v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) return std::nullopt;
    return v;
}

std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    for (auto& c : s)
        if (c == '/') c = '\\';
    return s;
}

}  // namespace

std::vector<std::string> parse_steam_libraryfolders(std::string_view vdf) {
    std::vector<std::string> out;
    walk_vdf(vdf, [&](int depth, const std::string& k, const std::string& v) {
        if (depth == 2 && k == "path" && !v.empty() && out.size() < 64) out.push_back(v);
    });
    return out;
}

bool steam_vdf_complete(std::string_view s) {
    std::size_t i = 0;
    int depth = 0;
    bool opened = false, have_key = false;
    for (int guard = 0; guard < 200000; ++guard) {
        const auto t = next_token(s, i);
        switch (t.kind) {
            case VdfToken::End: return opened && depth == 0 && !have_key;
            case VdfToken::Invalid: return false;
            case VdfToken::Open:
                if (!have_key) return false;
                ++depth;
                opened = true;
                have_key = false;
                break;
            case VdfToken::Close:
                if (have_key || --depth < 0) return false;
                break;
            case VdfToken::Str: have_key = !have_key; break;
        }
    }
    return false;  // sinir: sonu dogrulanamadi
}

SteamManifestParse classify_steam_appmanifest(std::string_view acf, std::string_view library_path) {
    // Once butunluk: kesilmis dosyada gerekli alanlar dolu olsa bile sonraki alanlar eksik olabilir.
    if (!steam_vdf_complete(acf)) return {ManifestKind::Malformed, std::nullopt};
    GameInstall g;
    g.launcher = "steam";
    std::optional<std::uint64_t> size;
    std::string dir, appid;
    walk_vdf(acf, [&](int depth, const std::string& k, const std::string& v) {
        if (depth != 1) return;
        if (k == "appid") appid = v;
        else if (k == "name") g.name = v;
        else if (k == "installdir") dir = v;
        else if (k == "SizeOnDisk") size = to_u64(v);
        else if (k == "buildid") g.version = v;
    });
    // Oyun degil: Steam ortak calisma zamani paketleri (Steamworks Common Redistributables, Steam Linux Runtime vb.).
    // Bu normal, gecerli bir kayittir; "bozuk/okunamadi" sonucuna karismaz.
    if (appid == "228980" || appid == "1070560" || appid == "1391110" || appid == "1628350" || g.name.rfind("Steamworks Common", 0) == 0)
        return {ManifestKind::NotGame, std::nullopt};
    if (g.name.empty() || dir.empty() || !size) return {ManifestKind::Malformed, std::nullopt};
    std::string base(library_path);
    if (!base.empty() && base.back() != '\\' && base.back() != '/') base.push_back('\\');
    g.install_dir = base + "steamapps\\common\\" + dir;
    g.size_bytes = *size;
    g.app_id = appid;
    return {ManifestKind::Game, std::move(g)};
}

std::optional<GameInstall> parse_steam_appmanifest(std::string_view acf, std::string_view library_path) {
    return classify_steam_appmanifest(acf, library_path).game;
}

std::optional<GameInstall> parse_epic_manifest(std::string_view text) {
    auto v = json::parse(text);
    if (!v.ok()) return std::nullopt;
    const auto& o = v.value();
    const auto* name = o.find("DisplayName");
    const auto* loc = o.find("InstallLocation");
    const auto* size = o.find("InstallSize");
    if (!name || !loc || !size || name->type() != json::Value::Type::String || loc->type() != json::Value::Type::String ||
        size->type() != json::Value::Type::Int || size->as_int() < 0)
        return std::nullopt;
    GameInstall g{name->as_string(), loc->as_string(), static_cast<std::uint64_t>(size->as_int()), "epic"};
    auto str = [&](const char* k) -> std::string {
        const auto* x = o.find(k);
        return x && x->type() == json::Value::Type::String ? x->as_string() : std::string{};
    };
    g.app_id = str("AppName");
    g.version = str("AppVersionString");
    g.exe_hint = str("LaunchExecutable");
    return g;
}

std::vector<VolumeAdvice> advise_volumes(const std::vector<VolumeInfo>& vols, const std::vector<StorageDevice>& disks,
                                         const std::vector<GameInstall>& games) {
    std::vector<VolumeAdvice> out;
    for (const auto& v : vols) {
        if (v.removable.has() && *v.removable.value) continue;  // USB bellek vb. degerlendirilmez
        VolumeAdvice a;
        a.root = v.root;
        a.total_gib = v.total_gib.value;
        a.free_gib = v.free_gib.value;
        if (const auto* d = disk_for_volume(disks, v)) {
            a.media = d->media;
            a.bus = d->bus;
            a.disk_model = d->model.value.value_or("");
        }
        const std::string root = lower(v.root);
        for (const auto& g : games) {
            if (lower(g.install_dir).rfind(root, 0) != 0) continue;
            ++a.games;
            const std::uint64_t gib = (g.size_bytes + (1ULL << 30) - 1) >> 30;
            if (gib > a.largest_game_gib) {
                a.largest_game_gib = gib;
                a.largest_game = g.name;
            }
        }
        if (a.games > 0) {
            a.recommended_free_gib = std::max<std::uint64_t>(kMinGameHeadroomGib, static_cast<std::uint64_t>(std::ceil(a.largest_game_gib * 0.25)));
            a.games_on_hdd = a.media == MediaKind::Hdd;
        }
        if (a.total_gib && a.free_gib && *a.total_gib > 0) {
            a.free_pct = 100.0 * static_cast<double>(*a.free_gib) / static_cast<double>(*a.total_gib);
            a.status = *a.free_pct < kCriticalFreePct  ? SpaceStatus::Critical
                       : *a.free_pct < kWarningFreePct ? SpaceStatus::Warning
                                                       : SpaceStatus::Ok;
            // Oyun guncellemesi icin tavsiye edilen alan yoksa en az uyari.
            if (a.recommended_free_gib && *a.free_gib < *a.recommended_free_gib && a.status == SpaceStatus::Ok)
                a.status = SpaceStatus::Warning;
        }
        out.push_back(std::move(a));
    }
    return out;
}

}  // namespace ef::core::inventory
