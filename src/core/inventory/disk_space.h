#pragma once
// DIAG-08: disk dolulugu ve oyun icin bos alan tavsiyesi (saf kurallar + kutuphane ayristiricilari).
// Durust: tavsiye, kutuphaneden okunan gercek oyun boyutuna dayali bir genel kuraldir (en buyuk oyunun
// ~%25'i, en az 20 GB) — uretici gereksinimi diye sunulmaz; oyun verisi yoksa yalniz yuzde esikleri.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/inventory/devices.h"

namespace ef::core::inventory {

struct GameInstall {
    std::string name;
    std::string install_dir;  // tam yol (utf-8)
    std::uint64_t size_bytes = 0;
    std::string launcher;     // "steam" / "epic"
    std::string app_id;       // GAME-01: Steam appid / Epic AppName (bos = bilinmiyor)
    std::string version;      // GAME-01: Steam buildid / Epic AppVersionString (bos = bilinmiyor)
    std::string exe_hint;     // Epic LaunchExecutable (goreli); Steam'de manifestte yok
    std::string manual_exe;   // EF-X11: bu satira bagli elle eklenen EXE (kayitli yazim); bos = yok
    bool manual_missing = false;  // EF-X11: elle eklenen EXE artik yok (satir kaldirilabilsin diye gosterilir)
};

// Steam libraryfolders.vdf -> kutuphane yollari ("C:\Program Files (x86)\Steam" ...). Bozuksa bos.
std::vector<std::string> parse_steam_libraryfolders(std::string_view vdf);
// VDF metni tam mi: dizeler kapali, bloklar dengeli, en az bir blok var. Kesilmis dosya false.
bool steam_vdf_complete(std::string_view vdf);
// Bildirim sinifi: Game (gecerli oyun), NotGame (gecerli ama bilincli dislanan Steam yardimci paketi),
// Malformed (bozuk/kesilmis/eksik alan). "Dislandi" ile "bozuk" ayni sonuc DEGILDIR.
enum class ManifestKind { Game, NotGame, Malformed };
struct SteamManifestParse {
    ManifestKind kind = ManifestKind::Malformed;
    std::optional<GameInstall> game;  // yalniz Game'de dolu
};
SteamManifestParse classify_steam_appmanifest(std::string_view acf, std::string_view library_path);
// appmanifest_*.acf -> oyun (installdir kutuphane/steamapps/common altina cozulur). Oyun degilse/bozuksa nullopt.
std::optional<GameInstall> parse_steam_appmanifest(std::string_view acf, std::string_view library_path);
// Epic *.item (JSON) -> oyun. Eksik alan: nullopt.
std::optional<GameInstall> parse_epic_manifest(std::string_view json);

enum class SpaceStatus { Ok, Warning, Critical, Unknown };
std::string_view to_string(SpaceStatus s) noexcept;

struct VolumeAdvice {
    std::string root;
    SpaceStatus status = SpaceStatus::Unknown;
    std::optional<std::uint64_t> total_gib, free_gib;
    std::optional<double> free_pct;
    MediaKind media = MediaKind::Unknown;
    BusKind bus = BusKind::Unknown;
    std::string disk_model;
    std::size_t games = 0;
    std::string largest_game;
    std::uint64_t largest_game_gib = 0;
    std::optional<std::uint64_t> recommended_free_gib;  // yalniz bu birimde oyun varsa
    bool games_on_hdd = false;
};

inline constexpr double kCriticalFreePct = 10.0;
inline constexpr double kWarningFreePct = 15.0;
inline constexpr std::uint64_t kMinGameHeadroomGib = 20;

std::vector<VolumeAdvice> advise_volumes(const std::vector<VolumeInfo>& vols, const std::vector<StorageDevice>& disks,
                                         const std::vector<GameInstall>& games);

}  // namespace ef::core::inventory
