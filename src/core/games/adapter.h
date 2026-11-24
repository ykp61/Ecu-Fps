#pragma once
// GAME-01: oyun kimligi ve adaptor sozlesmesi (saf; birim testlenir).
//  - Varsayilan yetenek yalniz "gozlem" (kullanici oynarken dis olcum). Yerlesik benchmark/replay/config
//    yazimi YALNIZ dogrulanmis adaptor kaydindan gelir ve o kayit belirli surum(ler)e baglidir.
//  - Gecersizlestirme: oyun surumu/yurutulebilir kimligi degisirse adaptor dogrulamasi gecersiz;
//    bulut esitleme aktifse config yazimi yapilmaz (oyunun esitlemesi degisikligi ezebilir/cakisir).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/inventory/disk_space.h"

namespace ef::core::games {

struct GameIdentity {
    std::string store;        // "steam" / "epic"
    std::string app_id;
    std::string name;
    std::string install_dir;
    std::string version;      // store build/surum; bos = bilinmiyor
    std::string exe_path;     // dogrulanan yurutulebilir (bos = henuz bilinmiyor)
    std::string exe_version;  // dosya surumu (bos = bilinmiyor)
};
GameIdentity identity_from(const inventory::GameInstall& g);

enum class Capability : std::uint32_t {
    Observation = 1,       // dis olcum (her oyun)
    BuiltinBenchmark = 2,  // oyunun kendi benchmark'i (dogrulanmis adaptor)
    Replay = 4,            // sabit demo/replay (dogrulanmis adaptor)
    ConfigRead = 8,
    ConfigWrite = 16,      // belgeli alanlar, kalici kayit + geri alma ile
};
inline std::uint32_t operator|(Capability a, Capability b) { return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b); }
inline std::uint32_t operator|(std::uint32_t a, Capability b) { return a | static_cast<std::uint32_t>(b); }

// Dogrulanmis adaptor kaydi: hangi oyun, hangi surumlerde, hangi yetenek dogrulandi.
struct AdapterRecord {
    std::string store, app_id;
    std::vector<std::string> verified_versions;  // bos = hicbir surum dogrulanmadi
    std::uint32_t capabilities = static_cast<std::uint32_t>(Capability::Observation);
    std::string evidence;  // artifacts/... kanit yolu
};

enum class Invalidation { None, VersionChanged, ExeChanged, VersionUnknown, CloudSyncActive, NoAdapter };
std::string_view to_string(Invalidation i) noexcept;

struct EffectiveCapabilities {
    std::uint32_t caps = static_cast<std::uint32_t>(Capability::Observation);
    std::vector<Invalidation> reasons;  // neden yalniz gozleme dusuruldu / config yazimi kapali
    bool has(Capability c) const noexcept { return (caps & static_cast<std::uint32_t>(c)) != 0; }
};

// Oyun icin gecerli yetenekleri hesaplar. cloud_sync: oyunun bulut esitlemesi aciksa (bilinmiyorsa true verin).
// known_exe_version: adaptor dogrulandiginda kaydedilen exe surumu (bos = kontrol yok).
EffectiveCapabilities effective(const GameIdentity& g, const std::vector<AdapterRecord>& registry, bool cloud_sync,
                                std::string_view known_exe_version = {});

}  // namespace ef::core::games
