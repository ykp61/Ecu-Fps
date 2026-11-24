#pragma once
// UI-09: canli kaynak halkalari icin saf veri modeli ve yardimcilar (Win32 yok, birim testlenir).
// Kurallar:
//  - Olculmeyen deger 0 degil Measurement::missing(neden); halka bos cizilir, metin "Olculemedi".
//  - NaN/sonsuz deger hicbir zaman gosterime gecmez; yuzdeler [0,100] araligina kirpilir.
//  - Yumusatma (Smoother) yalniz gosterim icindir; kayit/karsilastirma ham deger kullanir.
//  - Ornekleme yalniz pencere gorunurken yapilir (ui::Activity); bu katman zamanlayici icermez.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/measure.h"

namespace ef::core::metrics {

struct VolumeUse {
    std::string root;                      // "C:\" (utf-8)
    std::optional<std::uint64_t> total_gib;  // okunamazsa nullopt (0 degil)
    std::optional<std::uint64_t> free_gib;
};

// EF-X24.02: bagdastirici basina GPU kullanimi. luid envanter bicimindedir ("hhhhhhhh:llllllll", kucuk harf).
struct GpuAdapterUsage {
    std::string luid;
    double pct = 0;  // aggregate_gpu ile ayni anlam: motor toplami 100'e kirpilir, bagdastirici = motorlarin en buyugu
};

struct LiveSnapshot {
    contracts::Measurement cpu_pct = contracts::Measurement::missing(contracts::Unit::Percent,
                                                                     contracts::MissingReason::NotSampled);
    contracts::Measurement gpu_pct = contracts::Measurement::missing(contracts::Unit::Percent,
                                                                     contracts::MissingReason::NotSampled);
    // Uyumluluk B4 (bosluk 15): oyun surecinin GPU kullanimi, Gorev Yoneticisi yontemi (en yuklu motor turu, LUID
    // bazinda; async compute dahil). gpu_pct oyun ici olcumde surecin 3D motorudur (drop kurallari onu kullanir).
    contracts::Measurement game_gpu_engine_max_pct = contracts::Measurement::missing(contracts::Unit::Percent,
                                                                                     contracts::MissingReason::NotSampled);
    contracts::Measurement ram_pct = contracts::Measurement::missing(contracts::Unit::Percent,
                                                                     contracts::MissingReason::NotSampled);
    contracts::Measurement disk_pct = contracts::Measurement::missing(contracts::Unit::Percent,
                                                                      contracts::MissingReason::NotSampled);
    // DIAG-08 (oyun ici drop raporu) icin ek gercek sayaclar. Desteklenmeyen/okunamayan deger 0 degil,
    // kendi missing_reason'i ile bos kalir. Yalniz ornekleyicinin okudugu degerler doldurulur.
    contracts::Measurement cpu_max_core_pct = contracts::Measurement::missing(contracts::Unit::Percent,
                                                                              contracts::MissingReason::NotSampled);
    contracts::Measurement disk_queue_len = contracts::Measurement::missing(contracts::Unit::Count,
                                                                            contracts::MissingReason::NotSampled);
    contracts::Measurement disk_io_ms = contracts::Measurement::missing(contracts::Unit::Milliseconds,
                                                                        contracts::MissingReason::NotSampled);
    contracts::Measurement commit_used_pct = contracts::Measurement::missing(contracts::Unit::Percent,
                                                                             contracts::MissingReason::NotSampled);
    contracts::Measurement hard_fault_rate = contracts::Measurement::missing(contracts::Unit::Count,
                                                                             contracts::MissingReason::NotSampled);
    // DIAG-08: oyun disi sureclerin toplam CPU yuzdesi (toplam - hedef surec). Iki taraftan biri
    // olculemezse missing kalir; tahmin uretilmez.
    contracts::Measurement other_process_cpu_pct = contracts::Measurement::missing(
        contracts::Unit::Percent, contracts::MissingReason::NotSampled);
    // DIAG-10: hedef oyunun ana is parcacigi (en erken olusturulan thread) tek cekirdege gore CPU
    // yuzdesi (100 = bir cekirdek tam dolu). Yalniz gercekten olculduyse dolar; erisim yoksa
    // permission_denied/not_sampled ile missing kalir ve CPU kararinda kanit sayilmaz.
    contracts::Measurement cpu_main_thread_pct = contracts::Measurement::missing(
        contracts::Unit::Percent, contracts::MissingReason::NotSampled);
    // F-67: hedef oyunun ozel bellek kullanimi (GetProcessMemoryInfo PrivateUsage). Yalniz olculduyse
    // dolar; erisim yoksa permission_denied ile eksik kalir. Kosu boyunca buyume analizi icin.
    contracts::Measurement game_private_mib = contracts::Measurement::missing(
        contracts::Unit::MiB, contracts::MissingReason::NotSampled);
    // F-63: hedef oyunun adaptorlerdeki ayrik (dedicated) VRAM kullanimi. PDH "GPU Process Memory"
    // sayacindan; erisim/sayac yoksa missing kalir. Adaptor toplam VRAM'i degildir ve isletim
    // sisteminin oyun basina butcesi baska surecler icin okunamaz; rapor bunu boyle soyler.
    contracts::Measurement game_vram_dedicated_mib = contracts::Measurement::missing(
        contracts::Unit::MiB, contracts::MissingReason::NotSampled);
    // F-61: NVAPI birincil fiziksel GPU'nun surucu telemetrisi. Uc alan ayni okumada dolar; okuma
    // araligi disindaki orneklerde missing(NotSampled) kalir. Surucu/API yoksa missing(NotSupported),
    // sifir yazilmaz. Bu alanlar yalniz oyun ici drop raporunda limit baglami icin kullanilir.
    contracts::Measurement gpu_vendor_utilization_pct = contracts::Measurement::missing(
        contracts::Unit::Percent, contracts::MissingReason::NotSampled);
    contracts::Measurement gpu_vendor_clock_mhz = contracts::Measurement::missing(
        contracts::Unit::MHz, contracts::MissingReason::NotSampled);
    contracts::Measurement gpu_vendor_perf_decrease_mask = contracts::Measurement::missing(
        contracts::Unit::Count, contracts::MissingReason::NotSampled);
    // EF-X24: GPU sicakligi (yalniz gercek surucu okumasi; kaynak ve cihaz adiyla). Eski deger gosterilmez.
    contracts::Measurement gpu_temp_c = contracts::Measurement::missing(contracts::Unit::Celsius, contracts::MissingReason::NotSampled);
    std::string gpu_temp_source, gpu_temp_device;
    // EF-X24.02: gpu_pct'yi ureten bagdastirici kimlikleri (ham; yumusatilmaz). Bos = okunamadi.
    std::vector<GpuAdapterUsage> gpu_adapters;
    // Task 9: canli disk akisi (PDH PhysicalDisk Read/Write Bytes/sec, _Total haric fiziksel disk
    // toplami, MB/s = 1e6 B/s) ve varsayilan yol bagdastiricisinin ag hizi (GetIfEntry2 In/OutOctets
    // farki, Mbps) ile baglanti hizi (ReceiveLinkSpeed). Ilk ornek / bagdastirici degisimi / sayac
    // geri gitmesi NotSampled; baglanti hizi 0 veya bilinmiyor (VPN/tunel) NotSupported. 0 yazilmaz.
    contracts::Measurement disk_read_mbs = contracts::Measurement::missing(contracts::Unit::MBps,
                                                                           contracts::MissingReason::NotSampled);
    contracts::Measurement disk_write_mbs = contracts::Measurement::missing(contracts::Unit::MBps,
                                                                            contracts::MissingReason::NotSampled);
    contracts::Measurement net_down_mbps = contracts::Measurement::missing(contracts::Unit::Mbps,
                                                                           contracts::MissingReason::NotSampled);
    contracts::Measurement net_up_mbps = contracts::Measurement::missing(contracts::Unit::Mbps,
                                                                         contracts::MissingReason::NotSampled);
    contracts::Measurement net_link_mbps = contracts::Measurement::missing(contracts::Unit::Mbps,
                                                                           contracts::MissingReason::NotSampled);
    std::uint64_t ram_used_mib = 0;   // ram_pct eksikse anlamsizdir; gosterilmez
    std::uint64_t ram_total_mib = 0;
    std::vector<VolumeUse> volumes;
    std::int64_t mono_ms = 0;  // ornegin alindigi monoton zaman (ms)
};

// Yuzde degeri [0,100] araligina kirpar. NaN/sonsuz -> nullopt.
std::optional<double> clamp_pct(double v) noexcept;

// Ham yuzdeden Measurement: gecersiz (NaN/sonsuz) -> missing(Invalid).
contracts::Measurement percent(double v) noexcept;

// Halka doluluk orani 0..1. Eksik/gecersiz -> 0; cagiran bu durumda "Olculemedi" ve nedeni gosterir.
float ring_fraction(const contracts::Measurement& m) noexcept;

// Kullanilan / toplam bellekten yuzde. total == 0 veya used > total -> missing(Invalid).
contracts::Measurement ram_percent(std::uint64_t used, std::uint64_t total) noexcept;

// Hedef surec disi CPU yuzdesi = toplam - hedef. Iki degerden biri gecersiz/eksikse missing(NotSampled);
// toplamdan buyuk hedef (ornekleme kaymasi) 0'a kirpilir, negatif uretilmez.
contracts::Measurement other_process_percent(const contracts::Measurement& total,
                                             const contracts::Measurement& target) noexcept;

// --- Baska sureclerin CPU kullanimi, CPU SURESI biriminde (denge testi karistiricisi; spec 2026-11-12 A3) ---
// Birikimli sayaclar (100 ns): makinenin butun mantiksal islemcilerinin toplam zamani, bunun mesgul kismi
// (toplam - bosta) ve tek bir surecin cekirdek + kullanici zamani. Ucu de ayni birimdedir; frekansla olceklenen
// "% Processor Utility" sayaci sure tabanli surec zamaniyla KARISTIRILMAZ (turbo altinda toplam, surecin payindan
// fazla gorunur ve aradaki fark yanlislikla "baska surecler" sayilirdi).
struct CpuTimes {
    std::uint64_t total = 0;
    std::uint64_t busy = 0;
    std::uint64_t own = 0;
    // denge_v2: "baska program" sayilmayan mesgul sure (kendi oturumumuzun dwm.exe sureci + donanim kesmesi / DPC).
    std::uint64_t exempt = 0;
    // B3 inceleme: exempt toplamina GERCEKTEN giren parcalar (bit 0 = DPC/kesme, bit 1+i = i. muaf surec). Bir okumada
    // bir parca okunamazsa (gecici hata) maske degisir; maskesi farkli iki okuma karsilastirilmaz (kismi okuma ile
    // sonraki tam okuma arasindaki sahte buyuk muaf fark "%0 baska program" uydurmasin). Hep okunamayan parca
    // maskede hic yer almaz ve olcumu durdurmaz.
    std::uint32_t exempt_parts = 0;
};
struct CpuTimeReading {
    std::int64_t t_us = 0;  // tek duze saat (mikrosaniye); pencere sinirlariyla ayni saat
    CpuTimes times;
};
struct TimeWindow {
    std::int64_t begin_us = 0, end_us = 0;
};
// Iki okuma arasinda baska sureclerin (ve cekirdegin) CPU yuzdesi = (mesgul farki - surec farki) / toplam farki.
// Toplam ilerlemediyse veya bir sayac geri gittiyse missing(NotSampled); surec farki mesgul farkindan buyukse
// (sayac taneligi) 0. Sonuc [0,100].
contracts::Measurement other_cpu_time_percent(const CpuTimes& begin, const CpuTimes& end) noexcept;
// Olcum pencereleri uzerinden: her pencerede o pencereye dusen ILK ve SON okuma arasindaki farklar toplanir
// (pencereler arasi zaman sayilmaz), yuzde toplam farklardan hesaplanir (sure agirlikli). Okumalar zamana gore
// sirali verilir. Hicbir pencerede iki gecerli okuma yoksa missing(NotSampled): deger uydurulmaz.
contracts::Measurement other_cpu_time_percent_in(const std::vector<CpuTimeReading>& readings,
                                                 const std::vector<TimeWindow>& windows) noexcept;

// R-E1: test baslamadan once sakinlik beklemesi (saf mantik; okuma/uyku/iptal disaridan verilir). Her turda iki okuma
// arasindaki "baska programlar" CPU yuzdesi max_pct'nin altina inerse sakin; inmezse en cok max_polls tur beklenir.
// Okuma yapilamazsa ya da olculemezse beklemeden doner (unreadable): deger uydurulmaz, test yine baslar.
struct CalmWaitResult {
    bool calm = false;        // son turda baska programlarin payi max_pct altindaydi
    bool unreadable = false;  // okuma yapilamadi: sakin ya da mesgul denemez
    bool cancelled = false;
    std::uint32_t polls = 0;  // tamamlanan tur sayisi
    std::optional<double> last_other_pct;
};
CalmWaitResult wait_for_calm_cpu(const std::function<std::optional<CpuTimes>()>& read, const std::function<void()>& pause,
                                 const std::function<bool()>& cancelled, double max_pct, std::uint32_t max_polls);

// B3 inceleme (bosluk 2): tek bir pencere icin (drop penceresi). Pencere min_span_us'tan kisaysa merkezine gore
// simetrik genisletilir (GetSystemTimes tik taneligi ~15,6 ms). Pencereye dusen ILK okuma ile ayni exempt_parts
// maskeli SON okuma arasindaki fark kullanilir; aralarindaki sure min_span_us / 2'den kisaysa missing(NotSampled).
// Deger pencerenin kendi zamanidir: kayan ortalama gibi pencere disina yayilmaz ve gecikmez.
contracts::Measurement other_cpu_time_percent_around(const std::vector<CpuTimeReading>& readings, TimeWindow window,
                                                     std::int64_t min_span_us = 250'000) noexcept;

// Uyumluluk B3 (bosluk 2): oyun ici / drop teshisi icin kayan pencere. Her ornekte birikimli sureler (own = OYUN
// sureci, exempt = DPC/kesme + oturumun dwm.exe'si) itilir; deger, pencere suresi kadar onceki okumadan bu okumaya
// other_cpu_time_percent'tir. Frekansla olceklenen "% Processor Utility" toplamindan surec zamani CIKARILMAZ (turbo
// altinda sahte "baska program" yuku, kisilan dizustunde gizlenen gercek yuk). 50 ms'lik tek aralik GetSystemTimes
// tik taneligine (~15,6 ms) gore gurultuludur; bu yuzden en az min_span_us kadar geriye bakilir, oncesinde
// missing(NotSampled). Okunamayan ornek (nullopt) gecmisi siler ve missing doner: eski farkla deger uydurulmaz.
class CpuTimeWindow {
public:
    explicit CpuTimeWindow(std::int64_t span_us = 1'000'000, std::int64_t min_span_us = 250'000) noexcept;
    contracts::Measurement push(std::int64_t t_us, const std::optional<CpuTimes>& times);

private:
    std::int64_t span_us_, min_span_us_;
    std::vector<CpuTimeReading> history_;  // zamana gore sirali; ilk eleman pencerenin basi
};

// Uyumluluk B3 (bosluk 3): en yuklu mantiksal islemci. Ornekler "\Processor Information(*)\% Processor Time"
// ("0,3", "0,_Total", "_Total") ya da "\Processor(*)\% Processor Time" ("3", "_Total") adlariyla gelir; toplam
// ornekleri ("_Total", "<grup>,_Total") cekirdek sayilmaz. Deger SURE tabanlidir (cekirdegin mesgul oldugu zaman
// orani): %90 cekirdek doygunluk esigi frekansla olceklenen Utility ile karsilastirilmaz (turbo cekirdek %100'u
// asar gibi gorunur, taban saatin altindaki dizustu gercek doygunlugu kacirir). NaN/sonsuz atlanir, [0,100]'e
// kirpilir. Gecerli cekirdek yoksa missing(NotSampled).
struct CoreCpuSample {
    std::string instance;
    double value = 0;
};
bool is_processor_total_instance(std::string_view instance) noexcept;
contracts::Measurement max_core_percent(const std::vector<CoreCpuSample>& samples) noexcept;

// Olculen degerlerin aritmetik ortalamasi (yalniz has_value() olanlar). Hic deger yoksa
// missing(NotSampled); birimler karisiksa missing(Invalid). Yuzde degerleri [0,100]'e kirpilir.
contracts::Measurement mean_measured(const std::vector<contracts::Measurement>& values) noexcept;

// --- GPU ---
// PDH "\GPU Engine(*)\Utilization Percentage" ornek adi:
//   pid_1234_luid_0x00000000_0x0000C2E3_phys_0_eng_3_engtype_3D
struct GpuEngineId {
    std::uint32_t pid = 0;
    std::string luid;     // "0x00000000_0x0000C2E3" (adaptor kimligi)
    std::uint32_t phys = 0;
    std::uint32_t eng = 0;
    std::string engtype;  // "3D", "Compute", "Copy", "VideoDecode", ... (bos olabilir)
};
std::optional<GpuEngineId> parse_gpu_engine_instance(std::string_view name) noexcept;

struct GpuEngineSample {
    std::string instance;  // PDH ornek adi
    double utilization = 0;
};

// Toplam GPU kullanim yuzdesi (Gorev Yoneticisi anlami):
//  1) Ayni motor (luid+phys+eng) icin surec degerleri toplanir, 100'e kirpilir.
//  2) Adaptor (luid) kullanimi = motorlarinin en buyugu. Farkli motor turleri (3D + Copy + Video)
//     paralel calisir; toplamak 100'u asan anlamsiz deger uretir.
//  3) Genel deger = adaptorlerin en buyugu. Bosta iGPU + dolu dGPU ortalamasi gercek darbogazi gizler;
//     halka "en yuklu GPU"yu gosterir.
// Ayrisamayan/NaN ornekler atlanir. Gecerli ornek yoksa missing(NotSampled).
contracts::Measurement aggregate_gpu(const std::vector<GpuEngineSample>& samples);

// EF-X24.02: aggregate_gpu'nun bagdastirici adimi, kimlikle. Farkli LUID'lerin motorlari ASLA birlestirilmez (ayni
// phys/eng numarasi olsa bile). Sirali (kucuk LUID once); ayrisamayan/NaN ornek atlanir.
std::vector<GpuAdapterUsage> gpu_usage_by_adapter(const std::vector<GpuEngineSample>& samples);

// EF-X24.02: hibrit sistemde canli GPU kutucugunun kimligi. Kutucuk halkasi en yuklu GPU'yu (aggregate_gpu) gosterir;
// ad ve sicaklik o olcumu ureten karta ait olmalidir, aksi halde yazilmaz (yanlis karta deger yazmaktansa ad/sicaklik yok).
struct LiveGpuCard {
    std::string name;   // envanter adi (eslesme icin)
    std::string label;  // gosterilecek satir (ad + bellek)
    std::string luid;   // envanter LUID; bos = bilinmiyor
};
struct LiveGpuPick {
    std::optional<std::size_t> card;  // adi gosterilecek kart; nullopt = hicbir kart adlandirilmaz
    bool show_temp = false;           // sicaklik secilen karta (ya da adsiz kutucuga) aittir
};
// usage: gpu_usage_by_adapter sonucu; temp_device: sicakligi ureten surucunun bildirdigi kart adi (bos olabilir).
// Ad eslesmesi buyuk/kucuk harf duyarsiz; bir ad digerinin sonu ise de eslesir (ornek "GeForce X" / "NVIDIA GeForce X").
LiveGpuPick pick_live_gpu(const std::vector<LiveGpuCard>& cards, const std::vector<GpuAdapterUsage>& usage,
                          std::string_view temp_device, bool temp_present);

// --- Disk ---
struct DiskIdleSample {
    std::string instance;  // "0 C:" ; "_Total" yok sayilir
    double idle_pct = 0;   // "\PhysicalDisk(*)\% Idle Time"
};
// Disk etkinligi = fiziksel diskler arasinda en buyuk (100 - bosta %), [0,100]. Gecerli disk yoksa
// missing(NotSampled).
contracts::Measurement aggregate_disk(const std::vector<DiskIdleSample>& samples);

// Disk akisi: "\PhysicalDisk(*)\Disk Read Bytes/sec" (veya Write) ornekleri.
struct DiskRateSample {
    std::string instance;  // "0 C:" ; "_Total" yok sayilir (cift sayim olmasin)
    double bytes_per_sec = 0;
};
// Fiziksel disklerin toplami / 1e6 -> Unit::MBps. NaN/sonsuz/negatif ornek atlanir; gecerli disk
// yoksa missing(NotSampled).
contracts::Measurement aggregate_disk_rate(const std::vector<DiskRateSample>& samples);

// --- Ag ---
// Iki sayac okumasi arasi hiz (Mbps) = (cur-prev)*8/seconds/1e6. seconds <= 0 (veya NaN) ya da
// cur < prev (sayac sifirlandi/tasti, bagdastirici degisti) -> nullopt; negatif/sonsuz uretilmez.
std::optional<double> octet_rate_mbps(std::uint64_t prev, std::uint64_t cur, double seconds) noexcept;

// Indirme hizinin baglanti hizina orani [0,1]. Baglanti <= 0 / gecersiz (VPN, bilinmiyor) -> nullopt.
std::optional<double> link_fraction(double down_mbps, double link_mbps) noexcept;

// Gosterim birimi donusumu: Mbit/s -> MB/s (megabayt/sn) = Mbit/s / 8 (1000 Mbit/s -> 125,0 MB/s).
// NaN/sonsuz/negatif -> nullopt (sayi uydurulmaz).
std::optional<double> mbit_to_mbyte_per_s(double mbit_per_s) noexcept;

// Bir GetIfEntry2 okumasi (Win32 tipi icermez).
struct NetCounters {
    std::uint64_t if_index = 0;
    std::uint64_t in_octets = 0;
    std::uint64_t out_octets = 0;
    std::uint64_t link_bps = 0;  // ReceiveLinkSpeed; 0 veya ~0 = bilinmiyor
    std::int64_t mono_ms = 0;
};
struct NetRates {
    contracts::Measurement down = contracts::Measurement::missing(contracts::Unit::Mbps, contracts::MissingReason::NotSampled);
    contracts::Measurement up = contracts::Measurement::missing(contracts::Unit::Mbps, contracts::MissingReason::NotSampled);
    contracts::Measurement link = contracts::Measurement::missing(contracts::Unit::Mbps, contracts::MissingReason::NotSampled);
};
// Onceki okuma yoksa, bagdastirici farkliysa, zaman ilerlemediyse veya sayac geri gittiyse hiz
// missing(NotSampled). Baglanti hizi her okumada cur'dan; 0/~0 -> missing(NotSupported).
NetRates net_rates(const std::optional<NetCounters>& prev, const NetCounters& cur) noexcept;

// Gosterim icin ustel yumusatma: y = y + alpha * (x - y). Eksik giris durumu sifirlar ve eksik doner
// (bayat deger tasinmaz). Ilk deger oldugu gibi gecer.
class Smoother {
public:
    explicit Smoother(double alpha = 0.5) noexcept;
    contracts::Measurement push(const contracts::Measurement& m) noexcept;
    void reset() noexcept { y_.reset(); }

private:
    double alpha_;
    std::optional<double> y_;
};

}  // namespace ef::core::metrics
