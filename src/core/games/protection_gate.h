#pragma once
// Handoff v1 / EXT-AC-02 (docs/07_ANTICHEAT.md): korunan oyunlara mudahalesizlik kapisi (saf; birim testlenir).
//  - Canli oyun ici olcum: yalniz uyumluluk matrisinde ilgili oyunun TUM koruma kayitlari
//    verified_for_build + build eslesmesi + live_capture_enabled ise. Bilinmeyen kombinasyon kapali.
//    Kullanici "riski kabul ediyorum" override'i YOK (API'de parametre bile yok).
//  - Oyun calisirken (algilanan herhangi bir kurulu oyun; koruma tespiti kusursuz varsayilmaz) ayar
//    mutasyonu, disk temizligi ve agir ic benchmark BASLATILMAZ; islemler oyundan once/sonraya kalir.
//  - Algilama hatasi "oyun yok" demek degildir: fail-closed.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/games/game_guide.h"

namespace ef::core::games {

enum class CompatState { Unverified, Blocked, VerifiedForBuild, Stale };
std::string_view to_string(CompatState s) noexcept;

struct CompatEntry {
    std::string game_id;     // normalize oyun kimligi (Steam appid veya exe yolu kimligi)
    std::string game_name;
    std::string protection;  // "VAC / Trusted Mode", "FACEIT Anti-Cheat"
    CompatState state = CompatState::Unverified;
    // Kanit kapsami: dort alan da dolu ve calisan ortamla eslesmeden canli olcum acilmaz.
    std::string game_build, protection_build, collector_commit, os_driver_scope;  // bos: bilinmiyor
    bool live_capture_enabled = false;
    // EF-X18: matristeki dogrulanmis render API'si (meta veri). Unknown = dogrulanmadi; kapiyi ACMAZ.
    GuideRenderer renderer = GuideRenderer::Unknown;
};

// research/compatibility-matrix.json (as_of 2026-11-04) ile ayni icerik; derlemeye gomulu.
const std::vector<CompatEntry>& builtin_matrix();

// F-60: makine geneli koruma sistemi varlığı (yalnız bilgi; oyun-başına uyumluluk matrisi ayrıdır).
// Servis/ürün adları Windows katmanında toplanır; bu katman yalnız eşleme yapar.
enum class ProtectionSystem { EasyAntiCheat, BattlEye, Vanguard };
std::string_view to_string(ProtectionSystem s) noexcept;

struct ProtectionEvidence {
    bool query_ok = false;  // servis yöneticisi okunabildi mi (false -> "kurulu değil" denmez)
    bool eac_installed = false, eac_running = false;
    bool be_installed = false, be_running = false;
    bool vanguard_installed = false, vanguard_running = false;
    bool faceit_installed = false;  // T09: FACEIT istemci/servisi (yalniz oncelik ayari elemesi icin; durum listesinde yok)
};

struct ProtectionStatus {
    ProtectionSystem system;
    bool installed = false;
    bool running = false;
};

// Kurulu/çalışan koruma servislerini listeler. Sorgu başarısızsa boş döner (uydurma "yok" yok).
std::vector<ProtectionStatus> protection_status(const ProtectionEvidence& e);

enum class GuardedAction { SettingMutation, DiskCleanup, HeavyBenchmark, LiveCapture };

// Guvenilir oyun taramasinin azami yasi. Izleyici 3 sn'de bir tarar; 5 kacirilan tur = bayat.
constexpr std::int64_t kMaxDetectionAgeMs = 15000;

// 0 <= yas <= sinir. Bilinmeyen (-1) veya negatif yas guvenilir sayilmaz.
bool detection_fresh(std::int64_t age_ms) noexcept;

struct GateContext {
    std::vector<std::string> running_game_ids;  // izleyicinin algiladigi calisan kurulu oyunlar
    // Ilk tarama tamamlanmadi, tarama basarisiz veya oyun kimligi eksikse false. Varsayilan false:
    // cagiran taraf guvenilir taramayi acikca bildirmeden kapi acilmaz (fail-closed).
    bool detection_ok = false;
    // detection_ok=false iken kullaniciya gosterilecek gercek neden (gate.*). Bos: genel gate.detection_failed.
    std::string detection_reason_key;
    // Son guvenilir taramanin yasi. Varsayilan -1 (bilinmiyor) -> bayat sayilir; cagiran taze
    // taramayi acikca bildirmelidir. Uzun onay/UAC beklemesinden sonra islem sinirinda yeniden sorulur.
    std::int64_t detection_age_ms = -1;
    // LiveCapture icin: oyun + koruma + toplayici surumu + isletim sistemi/surucu kapsami birlikte
    // dogrulanir. Herhangi biri bos veya matristeki kanitla eslesmiyorsa kapali.
    std::string capture_game_id, capture_game_build, capture_protection_build, capture_collector_commit, capture_os_driver_scope;
};

struct GateDecision {
    bool allowed = false;
    std::string reason_key;  // katalog anahtari (gate.*); allowed iken bos
};

GateDecision check(GuardedAction a, const GateContext& ctx, const std::vector<CompatEntry>& matrix = builtin_matrix());
// EF-X18: gozlenebilen kanit kapsami. Bilinmeyen/kirli derleme kimligi bos doner (kapi kapali kalir).
std::string observed_collector_commit(std::string_view build_id);
// "os=<build.ubr>;gpu_driver=<surum>"; OS yapisi '?'/bos veya surucu bos/kontrol karakterliyse bos.
// Koruma surumu gozlenemez: GateContext::capture_protection_build bos kalir ve kapi gate.capture_evidence ile kapali.
std::string observed_os_driver_scope(std::string_view os_build, std::string_view gpu_driver);

// Oyun ici olcum kaynagi (view model): izleyicinin gercekten algiladigi calisan oyunlar + gozlenen surum.
struct RunningGameCapture {
    std::string game_id;     // kurulu oyun kimligi (Steam appid veya exe kimligi)
    std::string game_build;  // kurulu/gozlenen surum; bilinmiyorsa bos -> kapi kapali
    std::string protection_build;    // gozlenen anti-cheat/koruma surumu; bilinmiyorsa bos
    std::string collector_commit;    // kare toplayici commit kimligi; bilinmiyorsa bos
    std::string os_driver_scope;     // kanit kapsami (OS + GPU surucusu); bilinmiyorsa bos
};

// Oyunda Test kaynagi acilabilir mi? Calisan oyun yoksa gate.capture_no_game; varsa ilk uygun oyunun
// LiveCapture karari. Hicbiri acilmazsa ilk ret nedeni (uyumluluk kapisi aynen korunur; override yok).
GateDecision capture_source(const std::vector<RunningGameCapture>& running,
                            const std::vector<CompatEntry>& matrix = builtin_matrix());

}  // namespace ef::core::games
