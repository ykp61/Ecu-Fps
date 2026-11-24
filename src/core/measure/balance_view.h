#pragma once
// Denge testi (spec 2026-11-12 A5): sonuc gorunumu ve gecmis kaydi (saf; birim testlenir).
//  - Denge testi Uygulama Testi'nin bir KAPSAMIDIR (BuiltinScope); kullaniciya gorunen test kaynagi yine ikidir.
//    Gecmiste ayri kaynak ("balance") ve ayri protokol ("balance_v1") ile saklanir: "builtin" kayitlari ve
//    karsilastirmalari degismez.
//  - Kayit: duz metrik anahtarlari (core::diagnosis::balance_history_metrics + CPU paylari), hukumler kod olarak;
//    nedenler ve notlar dislama dizelerinde ("balance.global.<neden>", "balance.row_<r>.<neden>",
//    "balance.note.<not>"). Kart HER ZAMAN kayittan uretilir (balance_view_from_history): test bitince gorunen
//    kart ile yeniden acilista gorunen kart ayni koddan gelir.
//  - Yalniz olculen deger gosterilir; eksik deger 0 yazilmaz. Bilinmeyen / elle degistirilmis neden kodu ham
//    gosterilmez (genel "unknown" nedenine duser).
//  - Metinler katalog anahtaridir; her hukum "bu test sahnesinde" kapsamiyla sunulur (UI katmani).

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/diagnosis/balance.h"
#include "core/diagnosis/mismatch.h"
#include "core/insights/history.h"

namespace ef::core::measure {

// Uygulama Testi kapsami (oturumluk secim; varsayilan Genel test).
enum class BuiltinScope { General, Balance };

inline constexpr std::string_view kBalanceSource = "balance";
inline constexpr std::string_view kBalanceUnknownReason = "unknown";

struct BalanceRowView {
    diagnosis::BalanceVerdict verdict = diagnosis::BalanceVerdict::Undetermined;
    std::string reason;                 // balance_reason kodu ya da "unknown"; hukum varsa bos
    std::optional<double> gpu_fps;      // "GPU cizebiliyor"
    bool gpu_lower_bound = false;       // ">=" (en az)
    std::optional<double> coupled_fps;  // birlikte adimi (kayitta; kartta gosterilmez)
};

struct BalanceView {
    std::array<BalanceRowView, diagnosis::kBalanceRows> rows;  // 1080p, 1440p, 4K
    std::optional<double> cpu_fps;      // "CPU hazirlayabiliyor" (uc satirda ayni olcum)
    bool cpu_lower_bound = false;
    std::vector<std::string> global_reasons;  // dolu: butun satirlar bu nedenle(rle) Belirlenemedi (ilk = oncelikli)
    // denge_v4: genel neden "cpu_step_invalid" / "cpu_repeat_invalid" ise o adimin asil nedeni (balance_reason kodu ya da
    // "unknown"); eski kayitlarda ve diger durumlarda bos.
    std::string cpu_step_reason, cpu_repeat_reason;
    std::vector<std::string> notes;           // balance_note kodlari (katalogda olanlar)
    std::optional<double> cpu_sim_share, cpu_draw_share;  // [0,1]
    std::string adapter;                // cizen bagdastirici (utf-8); bilinmiyorsa bos
    bool quick = true;                  // Hizli / Tam
    bool preview = false;               // canli test ekrani acik miydi (kosul)
    // Spec 2026-11-12 C2: S3 sirasinda (GPU yuk altinda) okunan PCIe linki azaminin altindaysa dilden bagimsiz
    // metin ("x8 / x16, Gen 3 / Gen 4"); azamideyse, okunmadiysa ya da yuk altinda olculmediyse bos.
    std::string pcie_link;
    std::int64_t when_unix_ms = 0;
    bool has(diagnosis::BalanceVerdict v) const noexcept;
};

// ---- Kayit (app katmani cagirir) ----
// Duz metrikler: balance_history_metrics + "balance.cpu_sim_share" / "balance.cpu_draw_share" (olculduyse).
std::map<std::string, double> balance_metrics(const diagnosis::BalanceResult& r);
// Dislamalar: genel nedenler (oncelik sirasinda), satir nedenleri, notlar.
std::vector<std::string> balance_exclusions(const diagnosis::BalanceResult& r);
// Kosu karsilastirmada gecerli mi: CPU kapasitesi olculdu ve butun satirlari cekimser birakan genel neden yok.
bool balance_run_valid(const diagnosis::BalanceResult& r) noexcept;

// ---- Uyum denetimleri ve kosu kosullari (spec C + A3; app katmani doldurur) ----
struct BalanceRunChecks {
    std::optional<diagnosis::PcieLoadReading> pcie;          // S3 sirasinda okunan link; okunamadiysa bos
    diagnosis::CpuSpeedFinding cpu_speed;                    // CPU yuklu adimlarda "% Processor Performance" / sinir
    std::optional<std::uint32_t> cpu_firmware_limit_events;  // Kernel-Processor-Power 37, son acilistan beri
    std::optional<double> other_gpu_pct;                     // ilk adimdan once baska sureclerin 3D kullanimi
    std::optional<double> other_cpu_pct;                     // olcum pencerelerinde baska sureclerin CPU yuzdesi (saha teshisi)
    std::uint32_t retries = 0;                               // kararsiz adim tekrar sayisi
};
// Duz metrikler (yalniz olculenler; eksik deger 0 yazilmaz):
//   balance.pcie_width / _width_max / _gen / _gen_max / _slot_width_max / _slot_gen_max / _load_pct
//     (YALNIZ link yuk altinda okunup azamiyle karsilastirilabildiyse: kayitta varsa "yuk altinda" demektir),
//   balance.cpu_perf_first_pct / _last_pct, balance.cpu_perf_limit_min_pct, balance.cpu_fw_limit_events,
//   balance.other_gpu_pct, balance.other_cpu_pct, balance.retries.
void add_balance_check_metrics(std::map<std::string, double>& metrics, const BalanceRunChecks& c);
// Kosul anahtarlari: "retries" ("0".."2"), "other_gpu" ("low" < %10 <= "high"; okunamadiysa "unknown").
std::map<std::string, std::string> balance_check_conditions(const BalanceRunChecks& c);
// Kayittaki yuk alti link okumasi (yukaridaki anahtarlar); yoksa / bozuksa nullopt. Bilesen raporu ve kart kullanir.
std::optional<diagnosis::PcieLoadReading> balance_pcie_from_metrics(const std::map<std::string, double>& metrics);
// Bilesen raporu icin: kayittaki link okumasi + kaydin alindigi ekran karti (kosul "gpu"; yoksa bos). Deger kopyasidir;
// envanteri okuyan is parcacigina tasinabilir.
struct BalancePcieRecord {
    std::optional<diagnosis::PcieLoadReading> reading;
    std::string gpu;
};
BalancePcieRecord balance_pcie_record(const insights::HistoryEntry& e);
// Okuma yalniz kayit BUGUNKU ekran kartiyla alindiysa kullanilir (kart degistiyse eski kartin linki yeni karta
// yazilmaz). Kaydin "gpu" kosulu varsa bugunku ad (kosul gibi ilk kConditionCompareBytes bayti) ayni olmalidir;
// bugunku kart bilinmiyorsa (envanter henuz okunmadi / okunamadi) okuma KULLANILMAZ. Kayitta "gpu" kosulu yoksa
// (kosu envanter okunmadan yapildi) karsilastirilacak kanit yoktur: okuma oldugu gibi doner.
inline constexpr std::size_t kConditionCompareBytes = 160;
std::optional<diagnosis::PcieLoadReading> balance_pcie_for_gpu(const BalancePcieRecord& rec, std::string_view current_gpu);

// ---- Gorunum ----
// Kaynak "balance" + protokol "balance_v1" + en az bir hukum kodu olan kayittan gorunum; degilse nullopt
// (yarida kesilmis kosu, baska kaynak, eski / bilinmeyen protokol).
std::optional<BalanceView> balance_view_from_history(const insights::HistoryEntry& e);

// Katalog anahtarlari (hepsi katalogda vardir; birim testi dogrular).
std::string_view balance_verdict_key(diagnosis::BalanceVerdict v) noexcept;  // "balance.verdict.cpu" ...
std::string_view balance_short_key(diagnosis::BalanceVerdict v) noexcept;    // "balance.short.cpu" ...
std::string_view balance_row_key(std::size_t row) noexcept;                  // "balance.card.row_1080" ...
std::string balance_reason_key(std::string_view code);                       // "balance.reason.<kod>" | ...unknown
std::string balance_note_key(std::string_view code);                         // "balance.note.<kod>"; bilinmeyen -> bos

// P3: kart "Bu PC'de olculemedi" gosteriyor mu (genel neden var ya da bir satir Belirlenemedi / Olculemedi ya da CPU olculmedi).
// Kural / esik degildir: yalniz "Raporu kaydet" dugmesinin gorunme kosulu.
bool balance_view_unmeasured(const BalanceView& v) noexcept;

// ---- Hata ----
// Kosucunun kosuyu bitiren hata kodu -> Olcumler sayfasindaki hata nedeni kodu ("bench.reason.<kod>"). Bos kod
// "step_failed"; "timeout" -> "step_timeout" (Genel testin "timeout" metni "o ana kadarki olcum gosteriliyor" der;
// denge testi hata ile bittiginde sonuc YAZILMAZ, o cumle burada dogru olmazdi). Digerleri aynen.
std::string balance_fail_reason(std::string_view runner_error);

// ---- Calisan test asamalari ----
// Denge kosucusunun adim kodu (StepId + 1; 1..9) -> asama anahtari ("measure.stage_balance_s1" ...); gecersiz -> bos.
inline constexpr int kBalanceStages = 9;
std::string_view balance_stage_key(std::uint32_t step_code) noexcept;
// Asama anahtari -> 1..9; denge asamasi degilse nullopt.
std::optional<int> balance_stage_index(std::string_view stage_key) noexcept;

}  // namespace ef::core::measure
