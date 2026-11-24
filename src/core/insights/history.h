#pragma once
// INS-01: yerel test gecmisi ve disa aktarma (saf; birim testlenir).
//  - Kayit: kaynak, baslik, zaman, kosul parmak izi, yalniz OLCULEN metrikler (eksik metrik yazilmaz, 0 degil),
//    gecersizlik nedenleri, uygulama surumu.
//  - Sinirli (spec 2026-11-12 C "Son 3 test"): diskte yalniz en yeni kMaxEntries (3) test kaydi SAKLANIR; dorduncu
//    kayit yazilinca en eski dusurulur (sayilir). Eski surumun daha uzun dosyasi okunurken fazlasi (dosyadaki sira
//    en yeni basta) alinmaz ve trimmed_on_load() sayisi doner: cagiran dosyayi ilk acilista uce indirerek yeniden yazar.
//  - Disa aktarma izin listesi alanlardan olusur; donanim seri numarasi / UUID / kullanici yolu YOK.
//  - Bozuk veya yeni semali dosya: gecmis bos baslar, dosya ezilmez (cagiran yedekler).

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"

namespace ef::core::insights {

inline constexpr int kHistorySchema = 1;
inline constexpr std::size_t kMaxEntries = 3;
// B3: kaynak basina olcum protokolu kimligi; kayit aninda protocol_version'a yazilir. Protokol anlamli
// degisirse artirilir. Eski kayitlarda alan yoktur; karsilastirma fail-closed davranir (bos = bilinmiyor).
inline constexpr std::string_view kBuiltinProtocolVersion = "bench_v1";
inline constexpr std::string_view kEnduranceProtocolVersion = "endurance_v1";
// Denge testi (spec 2026-11-12 A5): Uygulama Testi'nin kapsami; ayri kaynak ("balance") ve ayri protokol. "builtin"
// kayitlari ve protokolu degismez.
inline constexpr std::string_view kBalanceProtocolVersion = "balance_v1";
// Oyunda Test'te "sahne" dis oyunun kendisidir; kosul izi oyunu/cozunurlugu tasir.
inline constexpr std::string_view kGameSceneVersion = "game_capture";

struct HistoryEntry {
    std::int64_t when_unix_ms = 0;
    std::string source;       // "builtin" / "in_game" / "endurance" / "balance"
    std::string title;        // "scene_v1 high" / oyun adi
    std::string fingerprint;  // MEAS-04 / BENCH-06 kosul izi
    std::string app_version;
    // B3: karsilastirilabilirlik alanlari. Eski kayitlarda yoksa bos/false kalir (parcalanmis kayit).
    std::string scene_version;     // "scene_v3" (yerlesik/uzun test) / "game_capture" (oyunda test)
    std::string protocol_version;  // "bench_v1" / "capture_v3" / "endurance_v1"
    bool run_valid = false;        // kosu gecerli mi; alan yoksa false (fark/yuzde uretilmez)
    std::map<std::string, double> metrics;  // yalniz olculen degerler
    // Calisma kosullari (izin listesi anahtarlari: bkz. kConditionKeys). Eski kayitlarda yoktur; uydurulmaz.
    std::map<std::string, std::string> conditions;
    std::vector<double> fps_series;  // saniye basina gercek FPS; yoksa bos (eski kayit veya olcum yok)
    std::vector<std::string> exclusions;    // gecersizlik / atlanan alt test nedenleri
    // EF-X20: teshis kurali + kalibrasyon damgasi ("compute_v1:uncalibrated"). Eski kayitta bos (bilinmiyor).
    std::string diagnosis_version;
};

class History {
public:
    // Ekler (yeni kayit basa); sinir asilirsa en eski dusurulur. Dusurulen sayisi doner.
    std::size_t add(HistoryEntry e);
    const std::vector<HistoryEntry>& entries() const { return entries_; }
    std::size_t dropped_total() const { return dropped_; }
    // parse(): dosyada kMaxEntries'ten fazla gecerli kayit vardiysa alinmayan (en eski) kayit sayisi; 0 = dosya zaten
    // sinirin icinde (yeniden yazmaya gerek yok). Dusurulenler dropped_total'a eklenir.
    std::size_t trimmed_on_load() const { return trimmed_; }

    std::string serialize() const;
    static contracts::Result<History> parse(std::string_view json);

    // Disa aktarma: izin listesi alanlar + uretim zamani. Ham yerel yol/kimlik yok.
    // only_source dolu ise yalniz o kaynakli kayitlar (ornegin "balance"; P3 Denge raporu); bos = hepsi.
    std::string export_json(std::int64_t now_unix_ms, std::string_view only_source = {}) const;

private:
    std::vector<HistoryEntry> entries_;
    std::size_t dropped_ = 0;
    std::size_t trimmed_ = 0;
};

// Kaydedilebilen kosul anahtarlari (donanim seri numarasi/kullanici yolu YOK).
bool allowed_condition_key(std::string_view k) noexcept;

// Disa aktarmada metrik anahtari izinli mi (yalniz [a-z0-9_.:-], <= 64 karakter).
bool safe_metric_key(std::string_view k) noexcept;

}  // namespace ef::core::insights
