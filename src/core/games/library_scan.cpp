#include "core/games/library_scan.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "core/games/exe_resolver.h"
#include "core/games/manual_games.h"

namespace ef::core::games {

using inventory::GameInstall;

namespace {

constexpr std::size_t kMaxFile = 1u << 20;  // 1 MiB: bildirim dosyalari kucuktur
constexpr int kMaxManifests = 4000;

void add_issue(LibraryScanResult& out, LibraryIssueKind k, std::string launcher, std::string path) {
    if (out.issues.size() < kMaxLibraryIssues) out.issues.push_back({k, std::move(launcher), std::move(path)});
}

void source_error(LibraryScanResult& out, LibraryIssueKind k, const char* launcher, std::string path) {
    ++out.source_errors;
    add_issue(out, k, launcher, std::move(path));
}

std::string join(const std::string& a, std::string_view b) {
    std::string out = a;
    if (!out.empty() && out.back() != '\\' && out.back() != '/') out.push_back('\\');
    out.append(b);
    return out;
}

bool has_suffix(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void manifest_read_failed(LibraryScanResult& out, const char* launcher, const std::string& path, FileRead::Status st) {
    if (st == FileRead::Status::Missing) return;  // liste ile okuma arasinda silindi: oyun artik yok, hata degil
    ++out.unreadable_manifests;
    add_issue(out, LibraryIssueKind::ManifestUnreadable, launcher, path);
}

void scan_steam(LibraryFs& fs, LibraryScanResult& out) {
    const auto root = fs.steam_root();
    switch (root.status) {
        case SourceValue::Status::Missing: return;  // Steam kurulu degil: kaynak yok (hata degil)
        case SourceValue::Status::Denied:
        case SourceValue::Status::Error:
            source_error(out, LibraryIssueKind::SteamRegistry, "steam", {});  // erisim hatasi "kurulu degil" sayilmaz
            return;
        case SourceValue::Status::Found: break;
    }
    const std::string steamapps = join(root.value, "steamapps");
    switch (fs.probe(steamapps)) {
        case PathKind::Missing: return;  // kayit kalmis ama Steam klasoru yok: kaynak yok
        case PathKind::Denied:
        case PathKind::Error:
        case PathKind::File:
            source_error(out, LibraryIssueKind::SourceFolder, "steam", steamapps);
            return;
        case PathKind::Directory: break;
    }
    std::vector<std::string> libs;
    const std::string vdf = join(steamapps, "libraryfolders.vdf");
    const auto kind = fs.probe(vdf);
    if (kind == PathKind::File) {
        const auto r = fs.read_file(vdf, kMaxFile);
        if (r.status == FileRead::Status::Ok) {
            if (inventory::steam_vdf_complete(r.data)) libs = inventory::parse_steam_libraryfolders(r.data);
            if (libs.empty()) source_error(out, LibraryIssueKind::LibraryList, "steam", vdf);  // bozuk/kesilmis liste
        } else if (r.status != FileRead::Status::Missing) {
            source_error(out, LibraryIssueKind::LibraryList, "steam", vdf);
        }
    } else if (kind != PathKind::Missing) {
        source_error(out, LibraryIssueKind::LibraryList, "steam", vdf);
    }
    if (libs.empty()) libs.push_back(root.value);  // en az kok kutuphane
    int count = 0;
    for (const auto& lib : libs) {
        const std::string apps = join(lib, "steamapps");
        const auto pk = fs.probe(apps);
        if (pk == PathKind::Missing) continue;  // listede olup kaldirilmis/takili olmayan kutuphane: hata degil
        if (pk != PathKind::Directory) {
            source_error(out, LibraryIssueKind::SourceFolder, "steam", apps);
            continue;
        }
        const auto listing = fs.list(apps);
        if (listing.status == DirList::Status::Missing) continue;
        if (listing.status != DirList::Status::Ok && listing.status != DirList::Status::Incomplete) {
            source_error(out, LibraryIssueKind::SourceFolder, "steam", apps);
            continue;
        }
        for (const auto& name : listing.names) {
            if (name.rfind("appmanifest_", 0) != 0 || !has_suffix(name, ".acf")) continue;
            if (++count > kMaxManifests) {
                source_error(out, LibraryIssueKind::ScanLimit, "steam", apps);  // sinir: "tam" denmez
                return;
            }
            const std::string path = join(apps, name);
            const auto r = fs.read_file(path, kMaxFile);
            if (r.status != FileRead::Status::Ok) {
                manifest_read_failed(out, "steam", path, r.status);
                continue;
            }
            auto parsed = inventory::classify_steam_appmanifest(r.data, lib);
            switch (parsed.kind) {
                case inventory::ManifestKind::Game: out.games.push_back(std::move(*parsed.game)); break;
                case inventory::ManifestKind::NotGame: ++out.excluded_non_games; break;
                case inventory::ManifestKind::Malformed:
                    ++out.malformed_manifests;
                    add_issue(out, LibraryIssueKind::ManifestCorrupt, "steam", path);
                    break;
            }
        }
        if (listing.status == DirList::Status::Incomplete) source_error(out, LibraryIssueKind::SourceFolder, "steam", apps);
    }
}

void scan_epic(LibraryFs& fs, LibraryScanResult& out) {
    const auto dirv = fs.epic_manifest_dir();
    if (dirv.status == SourceValue::Status::Missing) return;
    if (dirv.status != SourceValue::Status::Found) {
        source_error(out, LibraryIssueKind::EpicLocation, "epic", {});  // yol okunamadi: Epic yok denmez
        return;
    }
    const auto pk = fs.probe(dirv.value);
    if (pk == PathKind::Missing) return;  // Epic kurulu degil: kaynak yok (hata degil)
    if (pk != PathKind::Directory) {
        source_error(out, LibraryIssueKind::SourceFolder, "epic", dirv.value);
        return;
    }
    const auto listing = fs.list(dirv.value);
    if (listing.status == DirList::Status::Missing) return;
    if (listing.status != DirList::Status::Ok && listing.status != DirList::Status::Incomplete) {
        source_error(out, LibraryIssueKind::SourceFolder, "epic", dirv.value);
        return;
    }
    int count = 0;
    for (const auto& name : listing.names) {
        if (!has_suffix(name, ".item")) continue;
        if (++count > kMaxManifests) {
            source_error(out, LibraryIssueKind::ScanLimit, "epic", dirv.value);
            return;
        }
        const std::string path = join(dirv.value, name);
        const auto r = fs.read_file(path, kMaxFile);
        if (r.status != FileRead::Status::Ok) {
            manifest_read_failed(out, "epic", path, r.status);
            continue;
        }
        if (auto g = inventory::parse_epic_manifest(r.data)) {
            out.games.push_back(std::move(*g));
        } else {
            ++out.malformed_manifests;
            add_issue(out, LibraryIssueKind::ManifestCorrupt, "epic", path);
        }
    }
    if (listing.status == DirList::Status::Incomplete) source_error(out, LibraryIssueKind::SourceFolder, "epic", dirv.value);
}

}  // namespace

std::string_view issue_key(LibraryIssueKind k) noexcept {
    switch (k) {
        case LibraryIssueKind::SteamRegistry: return "gate.library.steam_registry";
        case LibraryIssueKind::SourceFolder: return "gate.library.folder";
        case LibraryIssueKind::LibraryList: return "gate.library.list";
        case LibraryIssueKind::ManifestCorrupt: return "gate.library.manifest_corrupt";
        case LibraryIssueKind::ManifestUnreadable: return "gate.library.manifest_unreadable";
        case LibraryIssueKind::ScanLimit: return "gate.library.limit";
        case LibraryIssueKind::ProviderSource: return "gate.library.provider_source";
        case LibraryIssueKind::EpicLocation: return "gate.library.epic_location";
    }
    return "gate.library.folder";
}

std::string_view provider_status_key(ProviderStatus s) noexcept {
    switch (s) {
        case ProviderStatus::Found: return "games.source.found";
        case ProviderStatus::Missing: return "games.source.none";
        case ProviderStatus::Denied:
        case ProviderStatus::Error: return "games.source.unreadable";
        case ProviderStatus::Partial: return "games.source.partial";
        case ProviderStatus::Unsupported: return "games.source.unsupported";
        case ProviderStatus::Disabled: return "games.source.disabled";
    }
    return "games.source.unreadable";
}

RegistryList battlenet_list_from_uninstall_entries(const std::vector<UninstallEntry>& entries) {
    RegistryList out;
    const auto lower = [](std::string_view v) {
        std::string s(v);
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    for (const auto& e : entries) {
        if (lower(e.uninstall_string).find("battle.net") == std::string::npos) continue;
        const std::string publisher = lower(e.publisher);
        if (publisher.find("blizzard entertainment") == std::string::npos && publisher.find("activision") == std::string::npos) continue;
        if (lower(e.key) == "battle.net" || lower(e.display_name) == "battle.net") continue;  // launcher oyun degildir
        if (std::any_of(out.items.begin(), out.items.end(), [&](const RegistryInstall& it) { return it.id == e.key; })) continue;
        std::string location = e.install_location;
        while (!location.empty() && (location.back() == '\\' || location.back() == '/')) location.pop_back();
        out.items.push_back({e.key, e.display_name, std::move(location), {}});  // bos yol: cekirdek bozuk sayar, "temiz" demez
    }
    out.status = out.items.empty() ? SourceValue::Status::Missing : SourceValue::Status::Found;
    return out;
}

std::string parse_microsoftgame_config_exe(std::string_view xml_in) {
    // XML yorumlari (<!-- -->) icindeki etiketler yoksayilir; kapanmayan yorumdan sonrasi guvenilmez, kesilir.
    std::string stripped;
    stripped.reserve(xml_in.size());
    for (std::size_t i = 0; i < xml_in.size();) {
        if (xml_in.compare(i, 4, "<!--") == 0) {
            const std::size_t end = xml_in.find("-->", i + 4);
            if (end == std::string_view::npos) break;
            i = end + 3;
            stripped.push_back(' ');
            continue;
        }
        stripped.push_back(xml_in[i++]);
    }
    const std::string_view xml = stripped;
    constexpr std::string_view kTag = "<Executable";
    const auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    for (std::size_t at = xml.find(kTag); at != std::string_view::npos; at = xml.find(kTag, at + kTag.size())) {
        const std::size_t after = at + kTag.size();
        if (after >= xml.size()) return {};
        if (!ws(xml[after])) continue;  // <ExecutableList> vb.
        const std::size_t end = xml.find('>', after);
        if (end == std::string_view::npos) return {};  // kesik etiket
        const std::string_view tag = xml.substr(after, end - after);
        for (std::size_t n = tag.find("Name"); n != std::string_view::npos; n = tag.find("Name", n + 4)) {
            if (n == 0 || !ws(tag[n - 1])) continue;  // OverrideDisplayName vb.
            std::size_t q = n + 4;
            while (q < tag.size() && ws(tag[q])) ++q;
            if (q >= tag.size() || tag[q] != '=') continue;
            ++q;
            while (q < tag.size() && ws(tag[q])) ++q;
            if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\'')) return {};
            const std::size_t close = tag.find(tag[q], q + 1);
            if (close == std::string_view::npos) return {};
            const std::string_view value = tag.substr(q + 1, close - q - 1);
            if (value.empty() || value.size() > 260 || value.find("..") != std::string_view::npos || value.find(':') != std::string_view::npos) return {};
            const std::string lower = canonical_manual_exe(value);
            if (lower.size() <= 4 || lower.compare(lower.size() - 4, 4, ".exe") != 0) return {};
            return std::string(value);
        }
        return {};  // ilk <Executable> etiketinde Name yok
    }
    return {};
}

std::string parse_riot_product_install_path(std::string_view yaml) {
    constexpr std::string_view kKey = "product_install_full_path:";
    std::size_t pos = 0;
    while (pos <= yaml.size()) {
        std::size_t eol = yaml.find('\n', pos);
        if (eol == std::string_view::npos) eol = yaml.size();
        std::string_view line = yaml.substr(pos, eol - pos);
        pos = eol + 1;
        std::size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        line.remove_prefix(i);
        if (line.rfind(kKey, 0) != 0) continue;
        line.remove_prefix(kKey.size());
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
        if (line.size() < 2 || (line.front() != '"' && line.front() != '\'') || line.back() != line.front()) return {};
        const bool dq = line.front() == '"';
        std::string v(line.substr(1, line.size() - 2));
        if (dq) {  // YAML cift tirnak: iki ters bolu -> bir; baska kacis dizisi tanimsiz
            std::string u;
            for (std::size_t k = 0; k < v.size(); ++k) {
                if (v[k] == '\\') {
                    if (k + 1 < v.size() && v[k + 1] == '\\') {
                        u.push_back('\\');
                        ++k;
                        continue;
                    }
                    return {};
                }
                u.push_back(v[k]);
            }
            v = std::move(u);
        }
        std::replace(v.begin(), v.end(), '/', '\\');
        while (v.size() > 3 && v.back() == '\\') v.pop_back();
        const bool drive = v.size() >= 3 && std::isalpha(static_cast<unsigned char>(v[0])) && v[1] == ':' && v[2] == '\\';
        if (!drive || v.size() <= 3 || v.size() > 260 || v.find("..") != std::string::npos) return {};  // surucu koku kurulum klasoru sayilmaz
        for (const char c : v)
            if (static_cast<unsigned char>(c) < 0x20) return {};
        return v;
    }
    return {};
}

namespace {

// T05: Riot istemcisi kendi urun klasorunu <ProgramData>\Riot Games\Metadata altindaki yaml'a yazar. Yalniz bilinen urunler
// taranir (dizin numaralandirmasi gerekmez); bilinmeyen urun ve riot_client yoksayilir.
struct RiotProduct {
    const char* id;
    const char* display;
    const char* exe;  // kurulum kokune gore
};
constexpr RiotProduct kRiotProducts[] = {
    {"valorant.live", "VALORANT", "ShooterGame\\Binaries\\Win64\\VALORANT-Win64-Shipping.exe"},
    {"league_of_legends.live", "League of Legends", "Game\\League of Legends.exe"},
};

void scan_riot(LibraryFs& fs, LibraryScanResult& out) {
    ProviderResult pr;
    pr.provider = "riot";
    pr.schema_version = 1;
    const auto md = fs.riot_metadata_dir();
    const auto src_failed = [&](SourceValue::Status st, const std::string& where) {
        pr.status = st == SourceValue::Status::Denied ? ProviderStatus::Denied : ProviderStatus::Error;
        source_error(out, LibraryIssueKind::ProviderSource, "riot", where);
        out.providers.push_back(pr);
    };
    if (md.status == SourceValue::Status::Missing) {
        pr.status = ProviderStatus::Missing;
        out.providers.push_back(std::move(pr));
        return;
    }
    if (md.status != SourceValue::Status::Found) return src_failed(md.status, {});
    switch (fs.probe(md.value)) {
        case PathKind::Missing:
            pr.status = ProviderStatus::Missing;
            out.providers.push_back(std::move(pr));
            return;
        case PathKind::Denied: return src_failed(SourceValue::Status::Denied, md.value);
        case PathKind::Error:
        case PathKind::File: return src_failed(SourceValue::Status::Error, md.value);
        case PathKind::Directory: break;
    }
    bool damaged = false;
    for (const auto& prod : kRiotProducts) {
        const std::string yaml = join(join(md.value, prod.id), std::string(prod.id) + ".product_settings.yaml");
        const auto r = fs.read_file(yaml, kMaxFile);
        if (r.status == FileRead::Status::Missing) continue;  // bu urun kurulu degil
        if (r.status != FileRead::Status::Ok) {
            ++out.unreadable_manifests;
            add_issue(out, LibraryIssueKind::ManifestUnreadable, "riot", yaml);
            damaged = true;
            continue;
        }
        const std::string root = parse_riot_product_install_path(r.data);
        if (root.empty()) {
            ++out.malformed_manifests;
            add_issue(out, LibraryIssueKind::ManifestCorrupt, "riot", yaml);
            damaged = true;
            continue;
        }
        const std::string exe = join(root, prod.exe);
        const auto kind = fs.probe(exe);
        if (kind == PathKind::Missing) continue;  // eski kayit: kaldirilmis oyun
        if (kind != PathKind::File) {
            ++out.unreadable_manifests;
            add_issue(out, LibraryIssueKind::ManifestUnreadable, "riot", exe);
            damaged = true;
            continue;
        }
        GameInstall g;
        g.launcher = "riot";
        g.app_id = prod.id;
        g.name = prod.display;
        g.install_dir = root;
        g.exe_hint = prod.exe;
        out.games.push_back(std::move(g));
        ++pr.games;
    }
    pr.status = damaged ? ProviderStatus::Partial : pr.games ? ProviderStatus::Found : ProviderStatus::Missing;
    out.providers.push_back(std::move(pr));
}

void scan_registry_provider(LibraryFs& fs, const char* provider, bool enabled, LibraryScanResult& out) {
    ProviderResult pr;
    pr.provider = provider;
    pr.schema_version = 1;
    if (!enabled) {
        pr.status = ProviderStatus::Disabled;
        out.providers.push_back(std::move(pr));
        return;
    }
    const auto list = fs.registry_installs(provider);
    switch (list.status) {
        case SourceValue::Status::Missing: pr.status = ProviderStatus::Missing; break;  // kurulu degil: hata degil
        case SourceValue::Status::Denied:
        case SourceValue::Status::Error:
            pr.status = list.status == SourceValue::Status::Denied ? ProviderStatus::Denied : ProviderStatus::Error;
            ++out.source_errors;
            if (out.issues.size() < kMaxLibraryIssues) out.issues.push_back({LibraryIssueKind::ProviderSource, provider, {}});
            break;
        case SourceValue::Status::Found: {
            pr.status = ProviderStatus::Found;
            std::size_t seen = 0;
            for (const auto& it : list.items) {
                if (++seen > 4000) {  // mevcut aday siniri
                    ++out.source_errors;
                    if (out.issues.size() < kMaxLibraryIssues) out.issues.push_back({LibraryIssueKind::ScanLimit, provider, {}});
                    break;
                }
                if (it.id.empty() || it.path.empty()) {
                    ++out.malformed_manifests;
                    continue;
                }
                const auto kind = fs.probe(it.path);
                if (kind == PathKind::Missing) continue;  // eski kayit: kaldirilmis oyun
                if (kind != PathKind::Directory) {
                    ++out.unreadable_manifests;
                    continue;
                }
                inventory::GameInstall g;
                g.launcher = provider;
                g.app_id = it.id;
                g.name = it.name.empty() ? it.id : it.name;
                g.install_dir = it.path;
                g.exe_hint = it.exe;
                out.games.push_back(std::move(g));
                ++pr.games;
            }
            break;
        }
    }
    out.providers.push_back(std::move(pr));
}

// P9-B3: EA app. Yalniz kayit defterindeki kurulum yolu (RegistryInstall.path) kullanilir; hicbir dosya okunmaz, hicbir klasor
// listelenmez (belgelenmemis/sifreli manifest dahil). exe/surum/boyut dogrulanamaz: kayit exe verse bile bos birakilir.
// Kapaliyken (gercek cihazda dogrulanmadi) saglayici "Unsupported" olur ve kayit defteri hic sorulmaz.
void scan_ea(LibraryFs& fs, bool enabled, LibraryScanResult& out) {
    if (!enabled) {
        ProviderResult pr;
        pr.provider = "ea";
        pr.schema_version = 1;
        pr.status = ProviderStatus::Unsupported;
        out.providers.push_back(std::move(pr));
        return;
    }
    const std::size_t first = out.games.size();
    scan_registry_provider(fs, "ea", true, out);
    for (std::size_t i = first; i < out.games.size(); ++i) {
        out.games[i].exe_hint.clear();
        out.games[i].version.clear();
        out.games[i].size_bytes = 0;
    }
}

// P9-B3: capraz kaynak tekillestirme. Ayni kurulum klasorunu (game_watch_key) bildiren ikinci ve sonraki launcher satirlari
// tek satira iner; tarama sirasinda ilk gelen kalir (Steam, Epic, GOG, Ubisoft, Xbox, Battle.net, Riot, EA). Elle eklenen
// satirlar bu asamada yoktur (scan_manual sonra ekler ve launcher satirina baglar). Guven hesabi (source_errors, bozuk/
// okunamayan sayaclar) etkilenmez; saglayicinin kendi "gordum" sayisi (ProviderResult::games) da degismez.
void merge_duplicate_installs(LibraryScanResult& out) {
    std::set<std::string> seen;
    std::vector<GameInstall> kept;
    kept.reserve(out.games.size());
    for (auto& g : out.games) {
        const std::string key = game_watch_key(g);
        if (!key.empty() && !seen.insert(key).second) {
            ++out.duplicates_merged;
            continue;
        }
        kept.push_back(std::move(g));
    }
    out.games = std::move(kept);
}

// EF-X11: Xbox/MS Store. Oyun uygunlugu = paket kokunde MicrosoftGame.config (GDK). Uygulama paketi hata degildir (T02).
void scan_xbox(LibraryFs& fs, bool enabled, LibraryScanResult& out) {
    ProviderResult pr;
    pr.provider = "xbox";
    pr.schema_version = 1;
    if (!enabled) {
        pr.status = ProviderStatus::Disabled;
        out.providers.push_back(std::move(pr));
        return;
    }
    using PS = PackageList::Status;
    const auto list = fs.user_packages();
    if (list.status == PS::Unavailable) {
        pr.status = ProviderStatus::Unsupported;
        out.providers.push_back(std::move(pr));
        return;
    }
    if (list.status == PS::Denied || list.status == PS::Error) {
        pr.status = list.status == PS::Denied ? ProviderStatus::Denied : ProviderStatus::Error;
        source_error(out, LibraryIssueKind::ProviderSource, "xbox", {});
        out.providers.push_back(std::move(pr));
        return;
    }
    bool damaged = false;
    if (list.status == PS::Truncated) {
        source_error(out, LibraryIssueKind::ScanLimit, "xbox", {});
        damaged = true;
    }
    std::size_t seen = 0;
    for (const auto& p : list.items) {
        if (++seen > static_cast<std::size_t>(kMaxManifests)) {
            source_error(out, LibraryIssueKind::ScanLimit, "xbox", {});
            damaged = true;
            break;
        }
        if (p.stub || p.path.empty()) continue;
        const std::string cfg = join(p.path, "MicrosoftGame.config");
        const auto kind = fs.probe(cfg);
        if (kind == PathKind::Missing) continue;  // oyun paketi degil
        if (kind != PathKind::File) {  // oyun mu bilinemez: "yok" denmez
            ++out.unreadable_manifests;
            add_issue(out, LibraryIssueKind::ManifestUnreadable, "xbox", cfg);
            damaged = true;
            continue;
        }
        std::string exe;
        const auto r = fs.read_file(cfg, kMaxFile);
        if (r.status == FileRead::Status::Ok) exe = parse_microsoftgame_config_exe(r.data);  // okunamazsa ipucu bos, klasor eslesmesi surer
        const std::string shown = fs.package_display_name(p.family);
        GameInstall g;
        g.launcher = "xbox";
        g.app_id = p.family;
        g.name = !shown.empty() && shown.rfind("ms-resource:", 0) != 0 ? shown : p.family;
        g.install_dir = p.path;
        g.exe_hint = std::move(exe);
        out.games.push_back(std::move(g));
        ++pr.games;
    }
    pr.status = damaged ? ProviderStatus::Partial : pr.games ? ProviderStatus::Found : ProviderStatus::Missing;
    out.providers.push_back(std::move(pr));
}

// EF-X11: elle eklenen EXE'ler (eklenirken dogrulandi). Kayitli HER giris bir satirda gorunur (kaldirilabilir):
//  - launcher klasoru altindaysa o satira baglanir (ikinci satir acilmaz; kaldirma dugmesi o satirda),
//  - dosya yoksa "bulunamadi" satiri olarak kalir (sessizce kaybolmaz, sinir/tekrar denetimi gorunur kalir),
//  - okunamayan dosya da izlenir (fail-safe).
void scan_manual(LibraryFs& fs, const std::vector<std::string>& exes, LibraryScanResult& out) {
    if (exes.empty()) return;
    ProviderResult pr;
    pr.provider = "manual";
    pr.schema_version = 1;
    const std::size_t launcher_games = out.games.size();  // yalniz launcher satirlarina baglanilir
    std::vector<std::string> seen;
    bool missing = false;
    for (const auto& exe : exes) {
        if (check_manual_exe(exe, {}, {}) != ManualExeCheck::Ok) continue;
        const std::string key = canonical_manual_exe(exe);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;  // elle duzenlenmis tekrar
        seen.push_back(key);
        const auto kind = fs.probe(exe);
        const bool gone = kind == PathKind::Missing || kind == PathKind::Directory;
        if (!gone) {
            bool attached = false;
            for (std::size_t i = 0; i < launcher_games && !attached; ++i) {
                auto& g = out.games[i];
                if (g.manual_exe.empty() && path_under_dir(exe, g.install_dir)) {
                    g.manual_exe = exe;
                    attached = true;
                }
            }
            if (attached) {
                ++pr.games;
                continue;
            }
        }
        GameInstall g;
        g.launcher = "manual";
        g.app_id = key;
        g.name = manual_exe_name(exe);
        g.install_dir = manual_exe_dir(exe);
        g.exe_hint = exe;
        g.manual_exe = exe;
        g.manual_missing = gone;
        out.games.push_back(std::move(g));
        if (gone) missing = true;
        else ++pr.games;
    }
    pr.status = missing ? ProviderStatus::Partial : pr.games ? ProviderStatus::Found : ProviderStatus::Missing;
    out.providers.push_back(std::move(pr));
}

}  // namespace

LibraryScanResult scan_library(LibraryFs& fs, const LibraryScanOptions& options) {
    LibraryScanResult out;
    const auto track = [&out](const char* name, bool denied, auto&& fn) {
        const auto errors = out.source_errors, games = out.games.size();
        const auto bad = out.malformed_manifests + out.unreadable_manifests;
        fn();
        ProviderResult pr;
        pr.provider = name;
        pr.schema_version = 1;
        pr.games = out.games.size() - games;
        const bool failed = out.source_errors != errors, damaged = out.malformed_manifests + out.unreadable_manifests != bad;
        pr.status = denied ? ProviderStatus::Denied : (failed || damaged) ? (pr.games ? ProviderStatus::Partial : failed ? ProviderStatus::Error : ProviderStatus::Partial)
                                                     : pr.games ? ProviderStatus::Found : ProviderStatus::Missing;
        out.providers.push_back(std::move(pr));
    };
    track("steam", fs.steam_root().status == SourceValue::Status::Denied, [&] { scan_steam(fs, out); });
    track("epic", fs.epic_manifest_dir().status == SourceValue::Status::Denied, [&] { scan_epic(fs, out); });
    scan_registry_provider(fs, "gog", options.registry_providers, out);
    scan_registry_provider(fs, "ubisoft", options.registry_providers, out);
    scan_xbox(fs, options.xbox_packages, out);
    scan_registry_provider(fs, "battlenet", true, out);  // T05: gercek cihazda DOGRULANMADI; kayit yoksa Missing
    scan_riot(fs, out);
    scan_ea(fs, options.ea_registry, out);
    merge_duplicate_installs(out);  // manuelden once: ayni klasoru bildiren launcher kaynaklari tek satira iner
    scan_manual(fs, options.manual_exes, out);  // en son: diger kaynaklardaki oyunlarla tekillestirilir
    out.skipped_manifests = out.malformed_manifests + out.unreadable_manifests;
    const bool clean = out.source_errors == 0 && out.skipped_manifests == 0;
    if (clean)
        out.state = out.games.empty() ? LibraryScanState::Empty : LibraryScanState::Ok;
    else if (out.games.empty() && out.source_errors > 0)
        out.state = LibraryScanState::Failed;  // hicbir guvenilir veri yok: "oyun yok" denmez
    else
        out.state = LibraryScanState::Partial;
    return out;
}

std::string game_watch_key(const GameInstall& g) {
    std::string d = g.launcher == "manual" ? canonical_manual_exe(g.exe_hint) : canonical_manual_exe(g.install_dir);
    while (!d.empty() && d.back() == '\\') d.pop_back();
    return d;
}

bool game_folder_trusted(const GameInstall& g) noexcept { return g.launcher != "manual"; }

std::int64_t library_poll_ms(const LibraryScanResult& r) noexcept {
    if (!r.trusted()) return kLibraryPollUntrustedMs;
    return r.games.empty() ? kLibraryPollEmptyMs : kLibraryPollGamesMs;
}

bool permit_library_reusable(PermitLibrarySource source, bool trusted, bool has_games, std::int64_t age_ms) noexcept {
    if (age_ms < 0) return false;  // saat geri gitti / gelecekteki zaman: guvenilmez
    switch (source) {
        case PermitLibrarySource::OwnScan: return age_ms <= kPermitOwnScanReuseMs;
        case PermitLibrarySource::Watcher: return trusted && has_games && age_ms <= kPermitWatcherReuseMs;
        case PermitLibrarySource::None: break;
    }
    return false;
}

bool permit_offer_accepted(bool sid_empty, bool manual_equal, bool trusted, std::int64_t scanned_at_ms,
                           std::int64_t current_at_ms) noexcept {
    if (!sid_empty || !manual_equal || !trusted) return false;
    return current_at_ms < 0 || scanned_at_ms > current_at_ms;
}

namespace {
std::string ascii_lower_backslash(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) out.push_back(c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}
bool path_is_or_under(const std::string& path, const std::string& dir) {
    if (dir.empty()) return false;
    if (path.size() < dir.size() || path.compare(0, dir.size(), dir) != 0) return false;
    return path.size() == dir.size() || path[dir.size()] == '\\';
}
}  // namespace

bool permit_reuse_needs_rescan(const std::vector<GameInstall>& games, const ProcessSnapshot& snapshot) {
    std::vector<std::string> known, roots;
    for (const auto& g : games) {
        const auto key = game_watch_key(g);
        if (key.empty()) continue;
        known.push_back(key);
        if (!game_folder_trusted(g)) continue;  // elle eklenen EXE'nin klasoru kitaplik koku degildir
        const auto slash = key.rfind('\\');
        if (slash != std::string::npos && slash > 0) roots.push_back(key.substr(0, slash));
    }
    static constexpr std::string_view kMarkers[] = {"\\steamapps\\common\\", "\\epic games\\", "\\xboxgames\\"};
    for (const auto& p : snapshot.processes) {
        if (!p.path_readable || p.exe_path.empty()) continue;
        const auto path = ascii_lower_backslash(p.exe_path);
        if (std::any_of(known.begin(), known.end(), [&](const std::string& k) { return path_is_or_under(path, k); })) continue;
        if (std::any_of(roots.begin(), roots.end(), [&](const std::string& r) { return path_is_or_under(path, r); })) return true;
        for (const auto m : kMarkers)
            if (path.find(m) != std::string::npos) return true;
    }
    return false;
}

std::string library_fingerprint(const LibraryScanResult& r) {
    std::vector<std::string> rows;
    rows.reserve(r.games.size());
    for (const auto& g : r.games) rows.push_back(g.launcher + "|" + g.app_id + "|" + g.install_dir + "|" + g.version + "|" + g.exe_hint + "|" + g.manual_exe + (g.manual_missing ? "|m" : ""));
    std::sort(rows.begin(), rows.end());
    std::string out(r.trusted() ? "t" : "u");
    for (const auto& row : rows) {
        out.push_back('\n');
        out += row;
    }
    return out;
}

WatchBootstrap watch_bootstrap(const LibraryScanResult& r) {
    WatchBootstrap b;
    b.trusted = r.trusted();
    b.no_games = b.trusted && r.games.empty();
    if (b.trusted)
        b.detection = DetectionState::Ok;
    else if (r.state == LibraryScanState::Failed)
        b.detection = DetectionState::Failed;
    else
        b.detection = DetectionState::Incomplete;
    return b;
}

}  // namespace ef::core::games
