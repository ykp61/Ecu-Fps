#pragma once
// EF-X01: Windows olay gunlugunden cokme/donma ozeti (saf mantik; Win32 yok, birim testlenir).
//  - Kaynak: olayin saglayici + olay no + icerigi. Olay no tek basina kanit degildir.
//  - Kesin donanim arizasi hukmu verilmez: tek Kernel-Power 41 "guc kaynagi bozuk" demek degildir.
//  - "Erisim reddedildi" ile "olay yok" ayri sonuclardir.
//  - XML siniri: olay basina en cok kMaxEventXml bayt, DOCTYPE/ENTITY reddedilir, dis varlik acilmaz.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/readiness/readiness.h"

namespace ef::core::diagnostics {

inline constexpr std::size_t kMaxEventXml = 64 * 1024;
inline constexpr std::size_t kMaxEvents = 2000;
inline constexpr std::size_t kMaxTotalBytes = 8 * 1024 * 1024;
inline constexpr std::size_t kMaxDataFields = 64;
inline constexpr int kDefaultDays = 7;
inline constexpr int kLongDays = 30;  // kullanici secimi; okuma sinirlari (2000 olay / 8 MiB) ayni kalir
// Yalniz 7 veya 30; baska her deger varsayilan 7'ye duser (bozuk tercih dosyasi genis tarama acmaz).
constexpr int normalize_incident_days(int d) noexcept { return d == kLongDays ? kLongDays : kDefaultDays; }

enum class ReadStatus { Ok, AccessDenied, ReadError };

struct RawEvent {
    std::string channel, provider;
    std::uint32_t event_id = 0;
    std::uint32_t version = 0;
    std::optional<std::uint32_t> level;        // 1-2 kritik/hata, 3 uyari, 4 bilgi
    std::uint64_t record_id = 0;
    std::optional<std::int64_t> time_unix_ms;  // UTC; ayristirilamazsa yok
    std::string raw_time;                      // kaynaktaki metin korunur
    std::map<std::string, std::string> data;
};

// Sinir asimi, DOCTYPE, kapanmayan etiket veya zorunlu alan eksikse nullopt (istisna atmaz).
std::optional<RawEvent> parse_event_xml(std::string_view xml);
// "2026-11-09T21:14:33.1234567Z" -> unix ms (UTC). Saat dilimi bilgisi Z degilse nullopt.
std::optional<std::int64_t> parse_system_time(std::string_view s);

struct EventReadResult {
    ReadStatus status = ReadStatus::ReadError;
    std::vector<RawEvent> events;
    std::size_t rejected = 0;   // ayristirilamayan olay
    bool truncated = false;     // adet, bayt veya sure siniri asildi
    bool corrected_truncated = false;  // duzeltilmis WHEA sayisi alt sinirdir
    std::optional<std::int64_t> oldest_ms;  // gunlukteki en eski kayit: istenen donemi kapsiyor mu
    std::string detail;         // tanilama (UI'da gosterilmez)
};

enum class IncidentKind { UnexpectedShutdown, BugCheck, WheaFatal, WheaCorrected, DisplayReset, DiskError };

struct IncidentFinding {
    IncidentKind kind = IncidentKind::UnexpectedShutdown;
    std::size_t count = 0;
    std::optional<std::int64_t> last_ms;
    std::string bugcheck;                  // son durdurma kodu ("0x0000009f"); yoksa bos
    std::vector<std::string> evidence;     // saglayici + olay no + alan
    std::vector<std::string> counter;      // karsi kanit / belirsizlik
    bool payload_invalid = false;          // WHEA verisi uzunlukla uyusmadi
    bool at_least = false;                 // sayi alt sinirdir (sinirli okuma)
    bool related_whea = false;             // mavi ekran ayni zaman penceresinde WHEA ile veya 0x124 kodu
    bool bad_block = false;                // disk: kotu blok kaydi (7); yalniz sayfalama uyarisi degil
    std::size_t power_button = 0, sleeping = 0;  // 41: kayittaki guc dugmesi / uyku bilgisi
    std::vector<std::uint32_t> disks;      // disk olaylarinin fiziksel disk numaralari (cozulebilenler)
};

struct IncidentReport {
    ReadStatus status = ReadStatus::ReadError;
    std::size_t events_seen = 0;           // tekillestirme sonrasi taninan olay
    std::size_t rejected = 0;
    bool truncated = false;
    bool clock_uncertain = false;          // kayit sirasi ile zaman celisiyor: olaylar eslestirilmedi
    bool coverage_short = false;           // gunluk istenen donemin tamamini kapsamiyor
    std::size_t unrecognized = 0;          // taninmayan seviyede WHEA olayi
    int days = kDefaultDays;
    std::vector<IncidentFinding> findings;
};

// Ayni olay (kanal + saglayici + kayit no) iki kez gelse de bir sayilir.
IncidentReport analyze_incidents(const EventReadResult& read, int days = kDefaultDays, std::optional<std::int64_t> now_ms = std::nullopt);

// Oyun Hazirlik satirlari: mevcut ekranda gosterilir; hicbir sey yazmaz.
// utc_offset_min: yerel tarih yazmak icin (Turkiye +180).
std::vector<readiness::Check> incident_checks(const IncidentReport& report, int utc_offset_min = 0);
// Okuma basariliysa ilk olay satirina pencere degistirme eylemi ekler ("30" veya "7"). Okunamadiysa eklemez.
void add_incident_window_action(std::vector<readiness::Check>& rows, const IncidentReport& rep);

}  // namespace ef::core::diagnostics
