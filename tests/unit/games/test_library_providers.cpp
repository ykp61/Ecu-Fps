// EF-X11: saglayici fikstur matrisi (T01). Gercek launcher ornegi degildir.
#include <algorithm>

#include "core/games/library_scan.h"
#include "ef_test.h"

using namespace ef::core::games;

namespace {
struct Fake final : LibraryFs {
    RegistryList gog, ubi;
    PathKind kind = PathKind::Directory;
    SourceValue steam_root() override { return {SourceValue::Status::Missing, {}}; }
    SourceValue epic_manifest_dir() override { return {SourceValue::Status::Missing, {}}; }
    PathKind probe(const std::string&) override { return kind; }
    DirList list(const std::string&) override { return {DirList::Status::Missing, {}}; }
    FileRead read_file(const std::string&, std::size_t) override { return {FileRead::Status::Missing, {}}; }
    RegistryList registry_installs(std::string_view p) override { return p == "gog" ? gog : ubi; }
};
const ProviderResult* of(const LibraryScanResult& r, const char* name) {
    for (const auto& p : r.providers)
        if (p.provider == name) return &p;
    return nullptr;
}
LibraryScanOptions on() {
    LibraryScanOptions o;
    o.registry_providers = true;
    return o;
}
}  // namespace

EF_TEST(x11_providers_stay_disabled_until_validated) {
    Fake f;
    f.gog.status = SourceValue::Status::Found;
    f.gog.items.push_back({"1", "G", "c:\\g", ""});
    const auto r = scan_library(f);
    EF_CHECK(of(r, "gog")->status == ProviderStatus::Disabled && r.games.empty() && r.state == LibraryScanState::Empty);
}

EF_TEST(x11_t01_valid_missing_denied_corrupt_for_provider) {
    Fake f;
    auto r = scan_library(f, on());  // kurulu degil: hata degil, "oyun yok" guvenilir
    EF_CHECK(of(r, "gog")->status == ProviderStatus::Missing && r.trusted());
    f.gog.status = SourceValue::Status::Denied;
    r = scan_library(f, on());
    EF_CHECK(of(r, "gog")->status == ProviderStatus::Denied && !r.trusted());
    f.gog.status = SourceValue::Status::Error;
    EF_CHECK(!scan_library(f, on()).trusted());
    f.gog.status = SourceValue::Status::Found;
    f.gog.items = {{"1", "Game", "c:\\g", "g.exe"}};
    r = scan_library(f, on());
    EF_CHECK(r.games.size() == 1 && r.games[0].launcher == "gog" && r.games[0].exe_hint == "g.exe" && of(r, "gog")->games == 1 && r.trusted());
    f.gog.items = {{"", "x", "c:\\g", ""}};  // bozuk kayit
    r = scan_library(f, on());
    EF_CHECK(r.games.empty() && r.malformed_manifests == 1 && !r.trusted());
    f.gog.items = {{"1", "x", "c:\\g", ""}};
    f.kind = PathKind::Denied;  // klasor okunamadi: "yok" sayilmaz
    EF_CHECK(!scan_library(f, on()).trusted());
    f.kind = PathKind::Missing;  // eski kayit
    EF_CHECK(scan_library(f, on()).trusted());
    f.kind = PathKind::Directory;
    f.gog.items.clear();  // asiri aday: sinirli ve guvenilmez (P9-B3: ayni klasoru gosteren kayitlar tekillesir; farkli klasorler)
    for (int i = 0; i < 4100; ++i) f.gog.items.push_back({"1", "x", "c:\\g" + std::to_string(i), ""});
    r = scan_library(f, on());
    EF_CHECK(r.games.size() == 4000 && !r.trusted());
}

EF_TEST(x11_unsupported_launchers_are_not_errors) {
    Fake f;
    const auto r = scan_library(f);
    for (const char* n : {"ea"}) EF_CHECK(of(r, n)->status == ProviderStatus::Unsupported);
    // EF-X11: Xbox urunde cihazda dogrulanana kadar kapali; aciksa sahte FS'te API yok = desteklenmiyor (hata degil)
    EF_CHECK(of(r, "xbox")->status == (kXboxPackagesValidated ? ProviderStatus::Unsupported : ProviderStatus::Disabled));
    EF_CHECK(r.state == LibraryScanState::Empty && r.source_errors == 0);
}
