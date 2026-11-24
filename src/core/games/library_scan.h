#pragma once
// B2/A-B: kurulu oyun kitapligi taramasi (platformdan bagimsiz cekirdek; birim testlenir).
//  - Gecerli oyun / bilincli dislanan yardimci paket / bozuk / okunamayan kayit AYRI sayilir.
//  - "Kaynak yok" (Steam kurulu degil, klasor bulunmuyor) ile "kaynak okunamadi" (erisim reddi, G/C hatasi) ayridir.
//  - Oyun yok karari yalniz guvenilir taramada (Ok/Empty) verilir; hata nedeniyle bos liste "oyun yok" DEGILDIR.
// Gercek dosya sistemi ve kayit defteri platform/windows/games/library_scan.* icindeki LibraryFs uygulamasindadir.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/games/watcher.h"
#include "core/inventory/disk_space.h"

namespace ef::core::games {

enum class PathKind { Directory, File, Missing, Denied, Error };

struct FileRead {
    enum class Status { Ok, Missing, Denied, TooLarge, Truncated, Error };
    Status status = Status::Error;
    std::string data;
};

struct DirList {
    // Incomplete: numaralandirma yarim kaldi; names okunabilen kismi tasir.
    enum class Status { Ok, Missing, Denied, Incomplete, Error };
    Status status = Status::Error;
    std::vector<std::string> names;
};

struct SourceValue {
    enum class Status { Found, Missing, Denied, Error };
    Status status = Status::Error;
    std::string value;  // utf-8 yol
};

// EF-X11: kayit defterinden kurulu oyun listesi veren launcher'lar.
struct RegistryInstall {
    std::string id, name, path, exe;
};
struct RegistryList {
    SourceValue::Status status = SourceValue::Status::Missing;
    std::vector<RegistryInstall> items;
};
// EF-X11: Windows paket envanteri (PackageManager, yalniz Main paketler, mevcut/hedef kullanici). Salt okuma.
struct UserPackage {
    std::string family;  // PackageFamilyName (kimlik)
    std::string path;    // EffectivePath (utf-8)
    bool stub = false;   // icerik kurulu degil
};
struct PackageList {
    // Unavailable: bu Windows'ta API yok (Unsupported, hata degil). Truncated: bellek siniri.
    enum class Status { Ok, Unavailable, Denied, Error, Truncated };
    Status status = Status::Unavailable;
    std::vector<UserPackage> items;
};

class LibraryFs {
public:
    virtual ~LibraryFs() = default;
    virtual SourceValue steam_root() = 0;          // HKCU\Software\Valve\Steam\SteamPath
    virtual SourceValue epic_manifest_dir() = 0;   // ProgramData\Epic\...\Manifests
    // T05: ProgramData\Riot Games\Metadata (<urun>\<urun>.product_settings.yaml). Varsayilan: kaynak yok.
    virtual SourceValue riot_metadata_dir() { return {SourceValue::Status::Missing, {}}; }
    virtual PathKind probe(const std::string& path) = 0;
    virtual DirList list(const std::string& dir) = 0;  // yalniz ad (yol degil)
    virtual FileRead read_file(const std::string& path, std::size_t max_bytes) = 0;
    virtual RegistryList registry_installs(std::string_view provider) {
        (void)provider;
        return {};
    }
    virtual PackageList user_packages() { return {}; }
    virtual std::string package_display_name(const std::string& family) {
        (void)family;
        return {};
    }
};

enum class LibraryScanState {
    Ok,       // kaynaklar okundu, oyun listesi tam
    Empty,    // kaynaklar okundu, kurulu oyun yok (guvenilir "yok")
    Partial,  // bazi bildirimler/kaynaklar okunamadi; liste tam degil
    Failed,   // kaynak okunamadi; "oyun yok" DENMEZ
};

enum class LibraryIssueKind {
    SteamRegistry,       // SteamPath okunamadi (erisim/GC hatasi; "kurulu degil" DEGIL)
    SourceFolder,        // kok/kitaplik klasoru erisilemedi veya listelenemedi
    LibraryList,         // libraryfolders.vdf okunamadi/bozuk: kitaplik listesi eksik
    ManifestCorrupt,     // bildirim bozuk/kesilmis
    ManifestUnreadable,  // bildirim okunamadi (erisim, kilit, kesik okuma)
    ScanLimit,           // guvenlik siniri: tarama kesildi
    EpicLocation,        // Epic bildirim konumu okunamadi
    ProviderSource,      // EF-X11: launcher kaydi okunamadi
};
std::string_view issue_key(LibraryIssueKind k) noexcept;  // gate.library.*

struct LibraryIssue {
    LibraryIssueKind kind = LibraryIssueKind::SourceFolder;
    std::string launcher;  // "steam" / "epic"
    std::string path;      // ilgili yol (kullanicinin kendi diskinde; ayrinti metni icin)
};

inline constexpr std::size_t kMaxLibraryIssues = 16;

// EF-X11: kaynak basina sonuc. Unsupported hata degildir: bu launcher'in listesi okunmuyor (Riot/Battle.net dosyalari
// oyun listesi sayilmaz, tahminle cozulmez). Disabled: bicim gercek ornekle dogrulanana kadar uretimde kapali.
enum class ProviderStatus { Found, Missing, Denied, Error, Partial, Unsupported, Disabled };
struct ProviderResult {
    std::string provider;
    int schema_version = 0;
    ProviderStatus status = ProviderStatus::Missing;
    std::size_t games = 0;
};

struct LibraryScanResult {
    LibraryScanState state = LibraryScanState::Failed;
    std::vector<ProviderResult> providers;
    std::vector<inventory::GameInstall> games;
    std::size_t source_errors = 0;       // okunamayan kok/klasor/numaralandirma/kayit defteri
    std::size_t malformed_manifests = 0;
    std::size_t unreadable_manifests = 0;
    std::size_t skipped_manifests = 0;   // malformed + unreadable
    std::size_t excluded_non_games = 0;  // bilincli dislanan yardimci paket; hata degil
    std::size_t duplicates_merged = 0;   // P9-B3: ayni kurulum klasorunu bildiren ikinci/ucuncu kaynak satiri (tek satira indi)
    std::vector<LibraryIssue> issues;    // en fazla kMaxLibraryIssues
    std::uint64_t scanned_at_ms = 0;     // sistem saati (epoch ms)
    // "Oyun yok" yalniz Ok/Empty icin guvenilirdir; Partial/Failed fail-closed.
    bool trusted() const noexcept { return state == LibraryScanState::Ok || state == LibraryScanState::Empty; }
};

// Gercek cihaz ornegi olmayan saglayicilar kapali baslar; yalniz testler acar.
inline constexpr bool kRegistryProvidersValidated = false;
// EF-X11: gercek cihazda `ef_watch_now --xbox` ile dogrulanana kadar kapali (bkz. state kaniti).
inline constexpr bool kXboxPackagesValidated = false;
// P9-B3: EA app. YALNIZ kayit defterindeki kurulum yolu okunur; belgelenmemis/sifreli manifest (ProgramData\EA Desktop,
// Origin LocalContent) OKUNMAZ. Gercek cihazda dogrulanana kadar kapali: kapaliyken saglayici "Unsupported" (hata degil).
inline constexpr bool kEaRegistryValidated = false;
// EA icin dogrulanamayan alanlar: GameInstall'da bos/0 kalir (uydurulmaz); kanitta "Unsupported" sayilir.
inline constexpr std::array<std::string_view, 4> kEaUnsupportedFields = {"exe_hint", "version", "size_bytes", "install_manifest"};
struct LibraryScanOptions {
    bool registry_providers = kRegistryProvidersValidated;
    bool ea_registry = kEaRegistryValidated;
    bool xbox_packages = kXboxPackagesValidated;
    std::vector<std::string> manual_exes;  // Preferences::manual_game_exes (eklenirken dogrulandi)
};
std::string_view provider_status_key(ProviderStatus s) noexcept;  // games.source.*
// P9-B3: Battle.net oyunlari HKLM\...\Uninstall altindaki Blizzard/Activision girisleridir. Ham giris -> RegistryList suzmesi
// saf ve fixture ile test edilir (platform yalniz anahtarlari okuyup bu islevi cagirir). Launcher'in kendisi, baska yayinci,
// baska kaldirici ve ayni anahtar tekrari elenir; InstallLocation bossa yol bos kalir (cekirdek bozuk sayar, "temiz" demez).
struct UninstallEntry {
    std::string key, display_name, publisher, uninstall_string, install_location;
};
RegistryList battlenet_list_from_uninstall_entries(const std::vector<UninstallEntry>& entries);
// Izleyici/kapi eslesme anahtari (kucuk harf, sonda ayrac yok). Elle eklenen oyun yalniz TAM EXE yolu ile eslesir:
// ortak klasordeki (or. %LOCALAPPDATA%\Programs) baska surecler oyun sayilmaz. Digerleri kurulum klasorudur.
std::string game_watch_key(const inventory::GameInstall& g);
// F-47/F-57: yazma kokleri ve klasor taramasi yalniz launcher kaynakli kurulum klasorleridir (elle eklenen haric).
bool game_folder_trusted(const inventory::GameInstall& g) noexcept;
// T05: Riot product_settings.yaml icindeki `product_install_full_path: "..."` degeri (kanonik "\\" ayracli, mutlak yol).
// Bulunamayan/bozuk/goreli/".." iceren deger icin bos doner.
std::string parse_riot_product_install_path(std::string_view yaml);
// MicrosoftGame.config ilk <Executable Name="..."> (paket ici goreli .exe); yoksa/bozuksa bos.
std::string parse_microsoftgame_config_exe(std::string_view xml);
LibraryScanResult scan_library(LibraryFs& fs, const LibraryScanOptions& options = {});

// Kitaplik taramasinin yenilenme araligi (ms). Guvenilir bos: gate tazeligini canli tutan dusuk maliyetli
// yeniden dogrulama (yalniz birkac metadata cagrisi). Guvenilmez: toparlanma. Oyunlu: yeni kurulum yakalama.
inline constexpr std::int64_t kLibraryPollEmptyMs = 5000;
inline constexpr std::int64_t kLibraryPollUntrustedMs = 10000;
inline constexpr std::int64_t kLibraryPollGamesMs = 30000;
std::int64_t library_poll_ms(const LibraryScanResult& r) noexcept;

// Uyumluluk B8 (bosluk 20): islem siniri izni (LiveGamePermit) kitaplik taramasini UI is parcaciginda tekrarlamasin.
// Izleyici is parcacigi her taramasinin sonucunu izne verir; izin bu sonucu kitaplik tazeligi penceresi icinde (izleyici
// yoklama araligi + tarama payi) ve yalniz GUVENILIR ise kullanir. Surec listesi her dogrulamada yine canli okunur (oyun
// calisiyor mu sorusunun kaniti odur). Kendi taramasi kisa sure (ardisik adimlar) her durumda yeniden kullanilir.
inline constexpr std::int64_t kPermitOwnScanReuseMs = 2000;
inline constexpr std::int64_t kPermitWatcherReuseMs = kLibraryPollGamesMs + 5000;
enum class PermitLibrarySource { None, OwnScan, Watcher };
// B8 inceleme: izleyicinin BOS sonucu hic yeniden kullanilmaz (bos liste surec denetimini tamamen atlatir; yeni kurulan
// oyun o anda calisiyor olabilir). Kendi kisa sureli taramasi (2 sn) eski davranisla her durumda kullanilir.
bool permit_library_reusable(PermitLibrarySource source, bool trusted, bool has_games, std::int64_t age_ms) noexcept;
// Izleyici sonucunun izne alinma kurali (saf): yalniz HKCU (sid bos), ayni elle eklenen liste, guvenilir tarama ve
// elimizdekinden (current_at_ms; -1 = yok) daha yeni zaman.
bool permit_offer_accepted(bool sid_empty, bool manual_equal, bool trusted, std::int64_t scanned_at_ms,
                           std::int64_t current_at_ms) noexcept;
// B8 inceleme: izleyici sonucu yeniden kullanilirken taze surec listesinde, bilinen bir kitaplik kokunun (launcher
// oyunlarinin ust klasoru ya da steamapps\common / Epic Games / XboxGames) altinda calisan ama bilinen hicbir oyuna ait
// olmayan surec varsa true: yeni kurulmus oyun olabilir, izin kitapligi o an yeniden tarar. Yolu okunamayan surec
// burada sayilmaz (GameWatcher onu zaten belirsizlik olarak ele alir).
bool permit_reuse_needs_rescan(const std::vector<inventory::GameInstall>& games, const ProcessSnapshot& snapshot);

// Tarama sonucunun degisiklik izi (oyun kumesi + surum + guven). Ayni izde agir oyun kartlari yeniden kurulmaz.
std::string library_fingerprint(const LibraryScanResult& r);

// B2: tarama sonucundan izleyici baslangic karari (saf).
struct WatchBootstrap {
    bool trusted = false;
    bool no_games = false;
    DetectionState detection = DetectionState::Unknown;
};
WatchBootstrap watch_bootstrap(const LibraryScanResult& r);

}  // namespace ef::core::games
