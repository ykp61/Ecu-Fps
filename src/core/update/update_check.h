#pragma once
// Acilista zorunlu guncelleme karari (spec 2026-11-11-zorunlu-guncelleme-design.md, ADR-028). Saf mantik: ag,
// dosya ve Win32 cagrisi yok. Zarf/manifest/swap icin catalog::open_envelope, evaluate_release, run_swap ve
// recover_swap yeniden kullanilir.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/catalog/envelope.h"

namespace ef::core::update {

inline constexpr std::string_view kReleaseRepoPrefix = "https://github.com/ykp61/Ecu-Fps-releases/releases/download/";
inline constexpr std::string_view kManifestUrl = "https://github.com/ykp61/Ecu-Fps-releases/releases/latest/download/manifest.json";
// "Indirme sayfasini ac" (klasor yazilamiyorsa): tarayicida acilan yayin sayfasi.
inline constexpr std::string_view kReleasePageUrl = "https://github.com/ykp61/Ecu-Fps-releases/releases/latest";
inline constexpr std::array<std::string_view, 3> kUpdateHosts{"github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"};
inline constexpr std::uint64_t kManifestMaxBytes = 64 * 1024, kExeMaxBytes = 64ull << 20;
inline constexpr int kCheckTimeoutMs = 8000, kConnectTimeoutMs = 4000, kHealthWaitMs = 20000;
inline constexpr std::string_view kReleaseDomain = "release";  // zarf alani

enum class UpdateOutcome { Checking, UpToDate, Mandatory, CheckFailed, VerifyFailed, NotWritable };

// Sayisal major.minor.patch. <0: a eski, 0: esit, >0: a yeni. Bicimsiz surum en dusuk sayilir.
int compare_versions(std::string_view a, std::string_view b);

// Yalniz https, kullanici bilgisi/port yok, host tam olarak kUpdateHosts icinde (buyuk/kucuk harf duyarsiz).
bool update_host_allowed(std::string_view url);

struct UpdateDecision {
    UpdateOutcome outcome = UpdateOutcome::CheckFailed;
    catalog::ReleaseManifest manifest;  // yalniz Mandatory/NotWritable icin anlamli
    // Imzasi ve guvenlik denetimleri gecen manifestin manifest_version'i (UpToDate/Mandatory/NotWritable); aksi 0.
    std::uint64_t verified_manifest_version = 0;
};

// Tekrar oynatma korumasi (spec §2): kalici olarak saklanan en yuksek dogrulanmis manifest_version'dan daha eski
// manifest Replay sayilir. Ayni (en son) manifest her acilista yeniden indirilir, bu nedenle esiti kabul edilir.
// Donus: ReleaseContext::last_manifest_version'a yazilacak deger (evaluate_release "<=" ile reddeder).
std::uint64_t replay_floor(std::uint64_t highest_seen);

// Karardan sonra kalici saklanacak en yuksek dogrulanmis manifest_version (asla geriye gitmez).
std::uint64_t next_highest_seen(std::uint64_t highest_seen, const UpdateDecision& d);

// envelope_json nullopt: indirilemedi (ag yok, zaman asimi, izinsiz yonlendirme) -> CheckFailed.
// Imza/manifest guvenlik reddi veya url kReleaseRepoPrefix disi -> VerifyFailed. Hedef disi (arch/kanal/OS) ve
// daha yeni olmayan -> UpToDate. Offer + yazilamayan klasor -> NotWritable, Offer -> Mandatory.
UpdateDecision decide_update(std::optional<std::string_view> envelope_json, const catalog::ReleaseContext& ctx, const catalog::KeyLookup& keys,
                             const catalog::RawVerifier& verify, bool exe_dir_writable);

// Yayin dosyasi yalniz kReleaseRepoPrefix altinda; sorgu, parca, kacis veya ust dizin yok (hizmet de yeniden denetler).
bool release_url_allowed(std::string_view url);

// Indirilen EXE boyutu ve SHA-256 (hex, harf duyarsiz) imzali manifestle ayni mi.
bool download_matches(std::uint64_t size, std::string_view sha256_hex, const catalog::ReleaseManifest& m);

// EXE degisim yollari (ayni klasor, ayni birim): yeni = exe.new, yedek = exe.old, saglik isareti = exe.updated.
catalog::SwapPaths update_swap_paths(const std::string& exe);

// Sistem yazan eylemler yalniz kontrol bittiyse ve zorunlu guncelleme bekleniyor degilse serbest.
bool writes_allowed(UpdateOutcome o);

}  // namespace ef::core::update
