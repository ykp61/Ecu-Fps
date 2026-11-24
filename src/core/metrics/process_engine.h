#pragma once
// Spec 2026-11-12 B (D5): oyun surecinin kendi 3D motor kullanimi, bagdastirici (LUID) bazinda (saf; birim testlenir).
// PDH "\GPU Engine(pid_<pid>_luid_0x<hi>_0x<lo>_phys_<n>_eng_<k>_engtype_3D)\Utilization Percentage" ornekleri
// LUID'e gore toplanir ve surecin EN COK kullandigi bagdastirici secilir: bu, oyunun cizdigi ekran kartidir.
// Sistem toplami ("en yuklu motor") baska sureclerin isini de tasir; burada yalniz hedef surecin ornekleri vardir.
// Ornek yoksa / ad cozulemiyorsa deger uretilmez (0 yazilmaz).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::metrics {

struct EngineInstance {
    std::string name;  // PDH ornek adi
    double pct = 0;    // Utilization Percentage (ust siniri kirpilmamis)
};

struct ProcessEnginePick {
    std::optional<double> pct;  // secilen bagdastiricidaki 3D motorlarin toplami (>= 0; 100'u asabilir)
    std::string luid;           // "hhhhhhhh:llllllll" (kucuk harf onaltilik; envanter LUID bicimi); yoksa bos
    std::size_t adapters = 0;   // surecin 3D ornegi bulunan bagdastirici sayisi
};

// Ornek adindan LUID: "…luid_0x00000000_0x0000C9E5_…" -> "00000000:0000c9e5". Cozulemezse bos.
std::string engine_instance_luid(std::string_view instance_name);
// Ad "engtype_3D" ile bitiyor mu (buyuk/kucuk harf duyarsiz).
bool engine_instance_is_3d(std::string_view instance_name) noexcept;

// Yalniz 3D ornekleri, sonlu ve >= 0 degerler. Esitlikte LUID metni kucuk olan secilir (belirlenimci).
ProcessEnginePick pick_process_3d_engine(const std::vector<EngineInstance>& instances);

// Uyumluluk B4 (bosluk 15): surecin GPU kullanimi, Gorev Yoneticisi yontemi. Butun motor turleri
// ("\GPU Engine(pid_<pid>_*)\Utilization Percentage") verilir: her bagdastiricida (LUID) motor TURU basina ornekler
// toplanir (ayni turun birden cok motoru, 3D'de oldugu gibi), bagdastirici degeri turlerin EN BUYUGUDUR (farkli turler
// paralel calisir, toplanmaz). Surecin en yuksek degerli bagdastiricisi secilir. Async compute (Compute_0/1) agirlikli
// DX12/Vulkan oyunlarinda yalniz 3D motoru GPU'yu oldugundan dusuk gosterir.
//  - pct_3d: AYNI bagdastiricida 3D turunun toplami (ikincil alan; o bagdastiricida 3D ornegi yoksa bos).
//  - Ornek yok / ad cozulemiyor: pct bos (0 yazilmaz). Esitlikte kucuk LUID / alfabetik tur (belirlenimci).
struct ProcessGpuPick {
    std::optional<double> pct;     // secilen bagdastiricida en yuklu motor turu (kirpilmamis)
    std::string luid;              // secilen bagdastirici; pct yoksa bos
    std::string engine_type;       // en yuklu tur, kucuk harf ("3d", "compute_0", ...)
    std::optional<double> pct_3d;  // ayni bagdastiricida 3D turu
    std::size_t adapters = 0;      // surecin (herhangi bir turde) ornegi bulunan bagdastirici sayisi
};
// "…_engtype_Compute_0" -> "compute_0" (kucuk harf). "engtype_" yoksa ya da bossa bos.
std::string engine_instance_type(std::string_view instance_name);
ProcessGpuPick pick_process_gpu_engine(const std::vector<EngineInstance>& instances);

// Uyumluluk B4 (bosluk 16): PDH joker sayaclari eklendikleri anda genisletilir; sonradan olusan ornekler (oyun
// cihazi yeniden kurdu, bagdastirici degisti, MUX dizustu) okunmaz. Yeniden ekleme (sayaci yeniden genisletme) karari:
//  - son okumada bu surece ait hic ornek yoksa: en cok kProcessGpuRetryMs'de bir;
//  - ayrica sorgu acildiktan ~kProcessGpuLateReexpandMs sonra BIR KEZ (ornek gorulse bile; yeni LUID / motor icin).
// Yeniden ekleme surerken (yeni sayac henuz hazir degil) karar sorulmaz; cagiran bunu kendisi engeller.
inline constexpr std::int64_t kProcessGpuRetryMs = 5'000;
inline constexpr std::int64_t kProcessGpuLateReexpandMs = 30'000;
struct ProcessGpuReexpand {
    std::int64_t opened_ms = 0;       // sorgunun acildigi an
    std::int64_t last_expand_ms = 0;  // son genisletme (acilis dahil)
    bool late_done = false;
};
// Simdi yeniden genisletme baslatilmali mi? Evetse durum guncellenir (genisletme ani = now_ms).
bool process_gpu_reexpand_due(ProcessGpuReexpand& state, std::int64_t now_ms, bool instance_seen) noexcept;

// Spec 2026-11-12 A3 (D6): denge testinin "arka plan GPU" karistiricisi. Butun sureclerin 3D motor ornekleri
// ("\GPU Engine(*engtype_3D)\Utilization Percentage") verilir; `luid` bagdastiricisinda `own_pid` DISINDAKI
// sureclerin kullanimi doner: ayni motor (phys + eng) icin surecler toplanir, bagdastirici degeri motorlarin en
// buyugudur (aggregate_gpu ile ayni anlam; farkli motorlar paralel calisir, toplanmaz). Ust sinir kirpilmaz.
//  - Ornek listesi bossa ya da hicbir 3D ornegi cozulemiyorsa nullopt (sayac okunamadi; 0 yazilmaz).
//  - Liste okundu ama o bagdastiricida baska surec yoksa 0 (olculdu: kimse kullanmiyor).
//  - `luid` bossa nullopt.
// exempt_pids: "baska program" sayilmayan surecler (denge_v2: kendi oturumumuzdaki dwm.exe).
std::optional<double> other_process_3d_pct(const std::vector<EngineInstance>& instances, std::string_view luid, std::uint32_t own_pid,
                                           const std::vector<std::uint32_t>& exempt_pids = {});

}  // namespace ef::core::metrics
