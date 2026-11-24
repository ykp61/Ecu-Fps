#pragma once
// Panel (spec 2026-11-12 B / D / F): saf bicim ve yerlesim yardimcilari. Cizim, katalog ve Win32 yoktur; birim
// testlenir. Kural: yalniz envanterin / canli ornegin gercekten okudugu parca yazilir; okunamayan parca atlanir,
// uydurma deger uretilmez.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/measure/balance_view.h"
#include "core/metrics/live.h"

namespace ef::ui {

// Performans bolumu (B.1): yan gruplar (solda SON TEST FPS + %1 DUSUK, sagda HAZIRLIK + "Testi baslat") yuvarlak
// dugmenin iki yanina sigiyor mu. Sigmiyorsa (dar pencere) yan gruplar dugmenin altina iner. edge_pad: gruplarin dis
// kenarinda bos kalmasi gereken pay (koyu temada yuva ic boslugu); yuva kirpilip metin kenara yapismaz.
inline bool panel_perf_side_by_side(float body_w, float button_d, float side_gap, float left_w, float right_w, float edge_pad = 0.f) {
    const float room = (body_w - button_d) / 2 - side_gap - edge_pad;
    return room >= left_w && room >= right_w;
}

// Envanter ayrintisindan fiziksel cekirdek sayisi: "8C/16T" -> 8. Bicim taninmiyorsa / 0 ise nullopt.
std::optional<int> cpu_core_count(std::string_view cpu_detail);
// RAM ad satiri: "64 GB · 2×32 GB DDR5 6000 MT/s" -> "64 GB DDR5 · 6000 MT/s". Tur ya da hiz okunamadiysa o parca
// yazilmaz ("64 GB DDR5", "64 GB"); bos girdi -> bos.
std::string ram_tile_name(std::string_view ram);
// Ekran kipi: "DELL S2721DGF · 2560×1440 · 165 Hz" -> "2560×1440 · 165 Hz" (yalniz cozunurluk ve yenileme hizi
// parcalari; ekran adi girmez). Hicbiri yoksa bos.
std::string display_mode_text(std::string_view display);

// Yuzde metni: Turkce "%68", aksi halde "68%".
std::wstring percent_text(int n, bool turkish);
// Olcek listesi (0..1) -> yuvarlanmis yuzdeler, " · " ile: TR "%67 · %58", EN "67% · 58%".
std::wstring scale_percents_text(const std::vector<double>& scales, bool turkish);

// Birim dolulugu (DISK kutucugu): okunabilen birimler, verilen sirada. Toplam ya da bos alan okunamayan birim girmez.
struct VolumeFill {
    std::wstring label;  // "C:"
    int used_pct = 0;    // 0..100 (yuvarlanmis)
    friend bool operator==(const VolumeFill&, const VolumeFill&) = default;
};
std::vector<VolumeFill> volume_fills(const std::vector<core::metrics::VolumeUse>& vols);
// Ilk `shown` birim: "C: %68 · D: %86 · E: %6"; gizli kalan varsa sonuna " +N" ("C: %68 · D: %86 +2"). shown birim
// sayisindan buyukse hepsi yazilir; bos liste ya da shown == 0 -> bos.
std::wstring volume_fill_list(const std::vector<VolumeFill>& v, std::size_t shown, bool turkish);

// ---- Darbogaz hukmu sunumu (spec 2026-11-12 D; denge_v1 kural esikleri degismez) ----
// Olculen pay: CPU sinirliyorsa N = round(100 x (1 - birlikte_fps / gpu_fps)) (ekran kartinin bosta kalan payi);
// GPU sinirliyorsa N = round(100 x (1 - birlikte_fps / cpu_fps)) (islemcide kalan pay). Paydadaki deger alt sinirsa
// ("en az") pay da "en az"dir. Deger eksik / gecersiz ya da sonuc negatifse pay yoktur (0'a kirpilmaz, satir yazilmaz).
struct BalanceShare {
    int pct = 0;
    bool at_least = false;
    friend bool operator==(const BalanceShare&, const BalanceShare&) = default;
};
std::optional<BalanceShare> balance_share(const core::measure::BalanceView& v, std::size_t row);
// Turkce iyelik eki (sayinin okunusuna gore): 32 -> "'si", 30 -> "'u", 6 -> "'sı", 100 -> "'ü". Negatif -> bos.
std::wstring_view tr_possessive_suffix(int n);
// Pay metni. possessive: Turkce'de iyelik ekiyle ("%32'si"; "ekran kartinin %32'si bosta kaliyor"). at_least_tpl:
// "en az {v}" / "at least {v}" kalibi (pay alt sinirsa uygulanir).
std::wstring share_text(const BalanceShare& s, bool turkish, bool possessive, std::wstring_view at_least_tpl);
// Kisa hukum sinifi (Panel karti): CpuLimited -> Cpu ("Islemci darbogazi"); GpuLimited / Balanced -> None
// ("Darbogaz yok"); digerleri mevcut kelimeyle (Belirlenemedi / Olculemedi).
enum class BottleneckWord { Cpu, None, Other };
BottleneckWord bottleneck_word(core::diagnosis::BalanceVerdict v);

// ---- Olcumler darbogaz kutulari (spec 2026-11-12 C) ----
// Birincil ekran ozetindeki cozunurluk ("AD · 2560×1440 · 165 Hz"; "×" ya da "x") -> denge satiri: 1920x1080 -> 0,
// 2560x1440 -> 1, 3840x2160 -> 2. Baska cozunurluk / okunamadi -> nullopt (vurgu yok; tahmin yapilmaz).
std::optional<std::size_t> balance_display_row(std::string_view display);
// Uc es kutu yan yana sigiyor mu: kutu genisligi kBottleneckTileMinW'den darsa kutular alt alta iner (1 sutun).
inline constexpr float kBottleneckTileMinW = 190.f;
inline int bottleneck_tile_columns(float inner_w, float gap) { return (inner_w - 2 * gap) / 3 >= kBottleneckTileMinW ? 3 : 1; }
// Test karti (spec C): "kalan N sn" — toplam sure biliniyorsa yukari yuvarlanmis kalan saniye (>= 0); bilinmiyorsa
// nullopt (sahte geri sayim yok).
std::optional<int> test_remaining_s(double total_s, double elapsed_s);
// "Son 3 test" acik satiri (spec C): liste degisince (yeni test basa eklenir, en eski duser) acik ayrinti AYNI testte
// kalir. before/after = satirlarin test zamani (en yeni basta). Acik test yeni listede yoksa ya da zamani belirsizse
// (cift kayit) -1 (kapali); baska bir testin ayrintisi acik gorunmez.
int follow_open_row(int open, const std::vector<std::int64_t>& before, const std::vector<std::int64_t>& after);

// ---- Olcumler Sonuc karti (0.2.1 geri bildirimi): sonuc varken kart hic tumuyle "—" olmaz ----
// Numbers: test suruyor (canli sayilar) ya da en yeni kayit Genel test / Oyunda Test (dort sayi kayittan).
// Balance: en yeni kayit denge testi (hukum + olculen FPS cifti kayittan). Empty: kayit yok (tek soluk satir + ipucu).
enum class ResultCardMode { Empty, Numbers, Balance };
ResultCardMode result_card_mode(bool busy, bool has_result, bool balance);
struct BalanceResultLine {
    std::size_t row = 0;  // 0 = 1080p, 1 = 1440p, 2 = 4K
    core::diagnosis::BalanceVerdict verdict = core::diagnosis::BalanceVerdict::Undetermined;
    BottleneckWord word = BottleneckWord::Other;
    std::optional<double> cpu_fps, gpu_fps;  // yalniz kayitta olculen deger; eksik -> bos (ekranda "—")
    bool cpu_at_least = false, gpu_at_least = false;
};
struct BalanceResultSummary {
    std::size_t headline = 0;          // baslik satiri: birincil ekranin cozunurlugu; yoksa ilk kesin hukum; yoksa 1080p
    bool headline_is_display = false;  // baslik ekran cozunurlugunden mi
    std::array<BalanceResultLine, core::diagnosis::kBalanceRows> lines;
};
BalanceResultSummary balance_result_summary(const core::measure::BalanceView& v, std::string_view display);

// ---- Test karti baslatma oncesi uyari (0.2.1 geri bildirimi) ----
// Yalniz Uygulama Testi baslatilabilir durumdayken (test surmuyor, kullanilabilir, uzun test / disk riski kilidi yok).
// Disk secili karisik yukte disk uyarisiyla birlesik tek satir (iki uzun uyari ust uste binmez). Oyunda Test oyunu
// yalniz izler: uyari yok.
enum class PretestWarning { None, Stress, StressDisk };
PretestWarning pretest_warning(bool builtin_source, bool busy, bool available, bool endurance_running, bool storage_blocked, bool mixed_load);

}  // namespace ef::ui
