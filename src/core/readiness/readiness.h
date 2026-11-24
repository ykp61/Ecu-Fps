#pragma once
// CHK-01..06 (plan v3.5): "Oyun Hazirlik Kontrolu" — saf kurallar (Win32 yok; birim testlenir).
// Platform okuyuculari (platform/windows/readiness/*) bu yapilari doldurur; kurallar bulgu uretir.
//  - Okunamayan deger nullopt -> bulgu "Unknown" (dogrulanamadi); tahmin yok.
//  - Bulgu metni katalog anahtari + parametreler ({hz}, {max} ...); UI yerlestirir.
//  - Duzeltme turu: otomatik (journal'li, Evet/Hayir), Windows ayar sayfasi, resmi web sayfasi, test calistir.
//  - Guvenligi azaltan oneriler (VBS/HVCI) yalniz bilgi; uygulama kapatmaz.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/inventory/problem_devices.h"
#include "core/measure/polling_rate.h"
#include "core/net/dns_bench.h"

namespace ef::core::readiness {

enum class Level { Good, Advice, Problem, Unknown, Info };
enum class Section { Display, Graphics, Latency, Windows, Hardware, Network, Software };
// IncidentWindow (EF-X01): olay gunlugu tarama penceresini 7/30 gun arasinda degistirir; action_arg "30" | "7".
// Yalniz uygulamanin kendi tercihidir; sistem ayari yazmaz, Evet/Hayir gerektirmez.
enum class ActionKind { None, AutoFix, OpenSettings, OpenUrl, RunNetworkTest, IncidentWindow, MeasureMouse, RunDnsBench, RunHealthTest };

struct Check {
    std::string id;         // kararli kimlik ("display.0.refresh" ...)
    Section section = Section::Display;
    Level level = Level::Unknown;
    std::string text_key;   // "chk.display.below_max" (tr/en katalogda)
    std::map<std::string, std::string> args;  // {"hz":"60","max":"144","name":"DELL ..."}
    ActionKind action = ActionKind::None;
    std::string action_arg; // ms-settings: uri, https url, otomatik duzeltme plan anahtari
};

// ---- Ekran (CHK-01/02) ----
struct DisplayInfo {
    std::uint32_t index = 0;
    std::string name;                        // EDID/monitor adi (bos olabilir)
    // Uyumluluk B6 (bosluk 10): kaynagin GDI aygit adi (ornek "\\.\DISPLAY1", UTF-8); okunamadiysa bos. Otomatik
    // yenileme hizi duzeltmesi yalniz bu ad biliniyorsa sunulur (sira numarasi baska ekrani hedefleyebilir).
    std::string gdi_device;
    bool primary = false;
    std::uint32_t width = 0, height = 0;
    std::optional<double> hz_current;        // fiziksel sinyal (QueryDisplayConfig vSyncFreq)
    std::optional<double> hz_max_same_res;   // ayni cozunurlukte desteklenen en yuksek
    bool dynamic_refresh = false;            // Windows dinamik yenileme hizi (masaustu dusuk Hz normal)
    std::optional<bool> hdr_supported, hdr_enabled;
    std::optional<bool> vrr_supported;       // ekran/surucu VRR bildiriyor mu (bilinmiyorsa nullopt)
    // F-44: etkin ekran yolunu (QueryDisplayConfig sourceInfo.adapterId) surun GPU. Bilinmiyorsa gpu_known
    // false kalir; bu durumda yol-GPU eslemesi iddia edilmez.
    bool gpu_known = false;
    std::string gpu_name;                    // DXGI adaptor adi (bos olabilir)
    std::string gpu_vendor;                  // "nvidia" | "amd" | "intel" | "microsoft" | "other"
    bool gpu_high_perf = false;              // bilinen adaptorler arasinda yuksek performans tercihi
    bool multi_gpu = false;                  // sistemde yazilim olmayan en az iki adaptor var
};
std::vector<Check> display_checks(const std::vector<DisplayInfo>& displays);
// Panel Sistem karti (spec 2026-11-11 §4): birincil ekranin (yoksa ilk etkin yolun) dile bagli olmayan ozeti:
// "AD · 2560×1440 · 165 Hz". Yalniz okunan alanlar yazilir (ad bos, cozunurluk 0 veya Hz bilinmiyorsa o parca yok);
// hic ekran yoksa bos metin ("okunamadi").
std::string display_summary(const std::vector<DisplayInfo>& displays);

// ---- Grafik / DirectX / surucu (CHK-03/04) ----
struct GraphicsInfo {
    std::string gpu_name;
    // "nvidia" | "amd" | "intel" | "other" (taninmayan PCI uretici)
    // | "microsoft" (yazilim olmayan 0x1414 adaptor: RDP/Hyper-V/dolayli ekran olabilir; surucu durumu bilinmiyor)
    // | "basic_display" (KANITLI surucusuzluk: SetupAPI ekran sinifinda yalniz BasicDisplay hizmeti)
    // | "" (adaptor okunamadi)
    std::string vendor;
    std::string driver_version;
    std::optional<std::int64_t> driver_date_unix;
    std::optional<std::uint32_t> d3d12_max_feature_level;  // 0xc200 = 12_2 ...
    std::optional<std::uint32_t> d3d11_feature_level;      // yalniz D3D12 cihazi olusmadiysa: D3D11 yedegi
    std::optional<std::uint32_t> shader_model;             // 0x66 = 6.6
    std::optional<std::uint32_t> raytracing_tier;          // 0 = yok, 10 = 1.0, 11 = 1.1
    std::optional<bool> mesh_shaders, vrs_tier2, sampler_feedback;
    std::optional<bool> dx9_legacy_runtime;  // d3dx9_43.dll (June 2010) mevcut mu
    std::optional<bool> gsync_enabled;       // NVAPI (yalniz NVIDIA; yoksa nullopt)
    std::optional<bool> gsync_possible;      // NVAPI bIsVRRPossible: bu ekranda G-SYNC/VRR mumkun mu (ek alan)
    std::optional<std::string> latest_driver_version;  // kullanici "Kontrol et" dediyse resmi kaynaktan
    std::int64_t now_unix = 0;
};
bool dx12_ultimate(const GraphicsInfo& g);  // 12_2 + DXR 1.1 + mesh + VRS2 + sampler feedback
std::vector<Check> graphics_checks(const GraphicsInfo& g);

// ---- Windows oyun ayarlari (CHK-05) ----
struct WindowsGamingInfo {
    std::optional<bool> game_mode;
    std::optional<bool> hags_supported, hags_enabled;
    std::optional<bool> game_dvr_background;     // "Olanlari kaydet" arka plan kaydi
    std::optional<bool> windowed_optimizations;  // pencereli oyunlar icin iyilestirmeler
    std::optional<bool> windows_vrr;             // Windows VRR anahtari
    std::optional<bool> fast_startup;            // F-48: hizli baslatma (HiberbootEnabled) acik mi; yoksa bilinmiyor
    std::optional<bool> vbs_running, hvci_enabled;
    std::optional<std::string> power_mode;       // "best_performance" | "balanced" | "best_efficiency" | plan adi
    // Etkin planin turu (GUID'den). power_mode bir plan ADI da olabildiginden ("balanced" adli ozel plan),
    // otomatik duzeltme (fix.power) yalniz Builtin'de onerilir; Custom her zaman ozel plan olarak gosterilir.
    enum class PowerPlanKind { Unknown, Builtin, Custom };
    PowerPlanKind power_plan_kind = PowerPlanKind::Unknown;
};
std::vector<Check> windows_checks(const WindowsGamingInfo& w);

// F-05: VRR acikken surucu/oyun ici V-Sync ve kare siniri onerisi; VRR yokken V-Sync secenekleri.
// Salt okuma; hicbir ayar yazmaz, AutoFix uretmez. Durum bilinmiyorsa (ekranlar + Windows anahtari)
// bulgu uretmez; "VRR yok" yalniz tum ekranlar acikca desteklemiyorsa soylenir.
std::vector<Check> frame_sync_checks(const std::vector<DisplayInfo>& displays, const WindowsGamingInfo& w);

// ---- Dizustu hibrit GPU: oyun basina Windows "Grafik tercihi" (P6; salt okuma) ----
// Kayit: HKCU\Software\Microsoft\DirectX\UserGpuPreferences\<exe yolu> = "GpuPreference=N;" (0 sistem, 1 guc tasarrufu,
// 2 yuksek performans). Bulgu yalniz dizustunde VE tumlesik + ayrik GPU ayrimi bilindiginde uretilir; bilinmiyorsa iddia yok.
enum class GpuPrefKind { Unknown, SystemDefault, PowerSaving, HighPerformance };
// Kayit degerinden ("GpuPreference=2;" ...). Deger yok -> SystemDefault (Windows kendisi secer); bicim/sayi tanimsiz -> Unknown.
GpuPrefKind gpu_pref_from_setting(const std::optional<std::string>& value, bool read_error);
struct GameGpuPref {
    std::string game;  // gosterilecek ad (exe dosya adi)
    GpuPrefKind pref = GpuPrefKind::Unknown;
    std::string exe;   // T01: okunan tam EXE yolu (utf-8); plan hedefi buradan kurulur
};
struct GpuPrefInfo {
    std::optional<bool> laptop;      // pil var mi
    std::optional<bool> hybrid;      // DXGI guc tasarrufu ve yuksek performans tercihleri farkli donanim kartlarini veriyor
    std::optional<bool> on_battery;  // AC yok
    std::vector<GameGpuPref> games;  // kullanicinin sectigi/tespit edilen oyunlar (en cok 3 gosterilir)
};
std::vector<Check> gpu_preference_checks(const GpuPrefInfo& g);

// ---- Ag (CHK-06 / NET-01) ----
struct NetworkInfo {
    // "ethernet" | "wifi" | "other" | "unknown" (okuma hatasi; baglanti var/yok bilinmiyor) | "" (baglanti yok)
    std::string adapter_kind;
    std::optional<std::uint64_t> link_bps;
    std::optional<int> wifi_signal_pct;        // 0..100
    std::optional<double> wifi_band_ghz;       // 2.4 / 5 / 6
    // Okunmaz (belgelenmis salt okuma API'si yok); kurallar bu alani kullanmaz.
    std::optional<bool> adapter_power_saving;
    std::optional<double> background_rx_mbps;  // son birkac sn arayuz alma hizi
};
struct PingStats {
    std::string target;   // "gateway" | "1.1.1.1" | "8.8.8.8"
    int sent = 0, received = 0;
    double avg_ms = 0, jitter_ms = 0, p95_ms = 0;
    bool failed = false;  // olcum calistirilamadi (or. ICMP tutamaci acilamadi); sent = 0
    double loss_pct() const { return sent > 0 ? 100.0 * (sent - received) / sent : 0.0; }
};
// Ham RTT orneklerinden istatistik (kayip = nullopt ornek). Jitter: ardisik RTT farklarinin ortalamasi.
PingStats ping_stats(const std::string& target, const std::vector<std::optional<double>>& rtt_ms);
std::vector<Check> network_checks(const NetworkInfo& n, const std::vector<PingStats>& pings);

// ---- Sistem sağlığı (F-65/F-67; salt okuma) ----
// Sayfa dosyası, disk boş alanı/TRIM, bellek sıkıştırma, çekirdek havuzu ve uptime.
// Okunamayan değer nullopt → "doğrulanamadı"; sıfır veya "sorun yok" varsayılmaz.
struct SystemHealthInfo {
    std::optional<std::uint64_t> uptime_s;
    std::optional<double> commit_used_pct;
    std::optional<bool> pagefile_configured;      // commit limit > fiziksel RAM (sayfa dosyası var)
    std::optional<double> pagefile_usage_pct;     // \Paging File(_Total)\% Usage
    std::optional<double> standby_mib;            // standby önbellek (normal öncelik)
    std::optional<double> nonpaged_pool_mib;      // çekirdek nonpaged havuzu
    std::optional<double> compressed_mib;         // "Memory Compression" çalışma kümesi (yaklaşık)
    std::optional<double> system_drive_free_ratio;  // 0..1
    std::optional<bool> trim_enabled;             // StorageDeviceTrimProperty
};
std::vector<Check> system_health_checks(const SystemHealthInfo& s);

// ---- Depolama sagligi (F-81; salt okuma) ----
// Oyun biriminin SSD/HDD turu, bos alan orani ve SMART ariza ongorusu okunur. Parcalanma orani
// belgelenmis hizli API ile okunamaz; metin bunu soyler. HDD'de Windows optimize/defrag bakimi
// onerilir; SSD'de defrag asla onerilmez. Okunamayan alan nullopt -> iddia yok.
struct StorageVolumeInfo {
    std::string root;                      // "C:\\"
    std::optional<bool> ssd;               // true = SSD, false = HDD (seek penalty), nullopt = bilinmiyor
    std::optional<double> free_ratio;      // 0..1
    std::optional<bool> smart_failure;     // IOCTL_STORAGE_PREDICT_FAILURE; nullopt = okunamadi
    std::optional<std::uint32_t> disk_number;  // ayni diskin bolumleri tek uyari uretir
};
// EF-X23: NVMe saglik gunlugu (log sayfasi 2). Temiz kayit diskin saglam oldugunu KANITLAMAZ.
struct NvmeHealthLog {
    std::uint8_t critical_warning = 0;
    std::optional<double> temperature_c;   // 0 K gecersiz: yok
    std::uint8_t available_spare = 0, spare_threshold = 0, percent_used = 0;
    std::uint64_t media_errors = 0;        // 128-bit sayac; tasarsa UINT64_MAX
    std::uint64_t power_on_hours = 0;
    std::uint64_t unsafe_shutdowns = 0;
};
std::optional<NvmeHealthLog> parse_nvme_health_log(const std::uint8_t* data, std::size_t size);
enum class DiskHealthSupport { Nvme, NvmeReadFailed, AtaPredictOnly, Unsupported, Unknown };
struct DiskHealthInfo {
    std::uint32_t disk = 0;
    std::string model;
    DiskHealthSupport support = DiskHealthSupport::Unsupported;
    std::optional<NvmeHealthLog> nvme;
};
std::vector<Check> disk_health_checks(const std::vector<DiskHealthInfo>& disks);
// Veri riski kaniti olan diskte yazma stresi calistirilmaz: once yedek.
// disk: secilen birimin fiziksel diski; bilinmiyorsa herhangi bir kanit engeller. Baska diskin sorunu bu diski engellemez.
bool storage_write_stress_allowed(const std::vector<Check>& checks, std::optional<std::uint32_t> disk = std::nullopt) noexcept;
std::optional<std::uint32_t> disk_of_volume(const struct StorageHealthInfo& s, const std::string& root) noexcept;
struct StorageHealthInfo {
    std::vector<StorageVolumeInfo> volumes;
    std::vector<DiskHealthInfo> disks;
    std::optional<std::string> game_volume_root;  // oyun kurulum birimi; bilinmiyorsa nullopt
};
std::vector<Check> storage_health_checks(const StorageHealthInfo& s);

// ---- Arka plan yigini (F-79; salt okuma) ----
// Bilinen overlay/yakalama, izleme ve RGB surecleri exe adindan siniflanir. Varlik, oyun
// karesine karistigi veya performans dusurdugu iddiasi degildir; metinler kosullu ve temkinlidir.
struct BackgroundStack {
    std::vector<std::string> overlays;    // gameoverlayui, nvidia overlay/share, gamebar, rtss, obs...
    std::vector<std::string> monitoring;  // hwinfo, aida64, hwmonitor, gpuz, afterburner...
    std::vector<std::string> rgb;         // icue, armoury, mystic light, rgb fusion, signalrgb...
};
BackgroundStack classify_background_stack(std::vector<std::string> exe_names);  // kucuk harf
std::vector<Check> background_stack_checks(const BackgroundStack& b);

// ---- Sistem tweak denetimi (F-52; salt okuma) ----
// NetworkThrottlingIndex / SystemResponsiveness (Multimedia\SystemProfile) ve
// GlobalTimerResolutionRequests (kernel) degerleri okunur. Deger yoksa Windows varsayilani
// gecerlidir; okunamayan anahtar nullopt kalir ve bulgu uretilmez. Hicbir deger yazilmaz.
struct SystemTweaksInfo {
    bool network_readable = false;  // SystemProfile anahtari okunabildi (degerler yoksa varsayilan)
    std::optional<std::uint32_t> network_throttling_index;  // yoksa varsayilan 10
    std::optional<std::uint32_t> system_responsiveness;     // yoksa varsayilan 20
    bool timer_readable = false;  // kernel anahtari okunabildi
    std::optional<std::uint32_t> global_timer_resolution_requests;  // yoksa 0
};
std::vector<Check> system_tweak_checks(const SystemTweaksInfo& t);

// ---- Bellek yerlesimi (F-62; salt okuma) ----
// SMBIOS tip 17 yuva etiketleri ve modul ozellikleri. Fiziksel kanal sayisi DIMM sayisindan
// kesinlestirilemez; etiketlerden turetilen sonuc "olasilik" olarak sunulur. Hicbir ayar yazilmaz.
struct MemoryModuleInfo {
    std::optional<std::uint64_t> size_mib;
    std::optional<std::uint32_t> configured_mts;
    std::string locator;
    std::string bank_locator;
};
struct MemoryLayoutInfo {
    std::optional<std::uint32_t> total_slots;
    std::optional<std::uint32_t> populated_slots;
    std::vector<MemoryModuleInfo> modules;
    bool virtual_machine = false;
};
std::vector<Check> memory_layout_checks(const MemoryLayoutInfo& m);

// ---- Bekleme (standby) belleginde esik bulgusu (B5, ADR-040 m.6; salt okuma) ----
// Esik kod sabitidir: bekleme bellegi toplam RAM'in yarisindan BUYUK (siki) VE bos bellek (bos + sifirlanmis liste;
// bekleme DAHIL DEGIL) toplamin onda birinden KUCUK (siki). Okunamayan deger nullopt -> bulgu yok. Bulgu yalniz olculen
// uc bayt degerini (GiB, bir basamak) gosterir; tavsiye/sayi eklemez. "Duzelt" mevcut bakim isini (maint:standby_purge)
// cagirir: tek Evet/Hayir; zamanlayici zorlama ve arka plan temizligi yoktur.
struct MemoryPressureInfo {
    std::optional<std::uint64_t> total_bytes;    // fiziksel bellek
    std::optional<std::uint64_t> standby_bytes;  // bekleme listesi toplami
    std::optional<std::uint64_t> free_bytes;     // bos + sifirlanmis sayfa listesi
};
inline constexpr char kStandbyFixArg[] = "fix.standby_purge";
std::vector<Check> standby_pressure_checks(const MemoryPressureInfo& m);
// Esik bulgusu varken genel "bekleme onbellegi normaldir" bilgisi (system.standby) celisir: o satir dusurulur.
void drop_superseded_standby_info(std::vector<Check>& checks);

// ---- Islemci / hibrit (F-41; salt okuma) ----
// P/E topolojisi ve Windows surumu okunur; Thread Director/APO/DTT durumu genel API ile
// dogrulanamaz. Bu yuzden bulgular "olabilir/onerilir" der, etkinlik iddia etmez; E cekirdek
// kapatma onerilmez. Hicbir ayar yazilmaz; AutoFix yoktur.
struct CpuHybridInfo {
    std::string vendor;  // "intel" | "amd" | "other" | "" (okunamadi)
    std::optional<std::uint32_t> physical_cores, logical_processors;
    std::optional<std::uint32_t> performance_cores, efficiency_cores;
    std::optional<bool> hybrid;
    std::optional<std::uint32_t> windows_build;  // RtlGetVersion build
};
std::vector<Check> cpu_hybrid_checks(const CpuHybridInfo& c);

// ---- Islemci guc ozellikleri (F-34; salt okuma, yazma yok) ----
// CPUID ile yalniz DESTEK (HWP/Speed Shift, EIST) okunur. Gercek BIOS/MSR etkinlik durumu ring0
// olmadan okunamaz; metin bunu soyler ve uygulama hicbir ayar yazmaz. Destek yoksa bulgu uretilmez.
struct CpuPowerInfo {
    std::string vendor;                 // "intel" | "amd" | "other" | "" (okunamadi)
    bool cpuid_ok = false;              // CPUID guvenle calisti
    std::optional<bool> speed_shift;    // CPUID leaf 6 EAX bit 7 (Intel HWP)
    std::optional<bool> eist;           // CPUID leaf 1 ECX bit 7 (Enhanced SpeedStep)
};
std::vector<Check> cpu_power_checks(const CpuPowerInfo& p);

// Spec 2026-11-12 C1: CPU hiz siniri (salt okuma; bostayken okunabilen iki kanit). Yuk altindaki dusus Denge
// testinde olculur ve sonucunda not olarak yazilir; burada sicaklik ya da neden iddiasi YOKTUR.
struct CpuSpeedLimitInfo {
    std::optional<double> performance_limit_pct;         // PDH "% Performance Limit" (_Total), anlik; okunamadiysa bos
    std::optional<std::uint32_t> firmware_limit_events;  // Kernel-Processor-Power 37 kaydi, son acilistan beri
    bool events_capped = false;                          // sayim ust sinirda kesildi ("en az")
};
// Tek satir ("cpu.speed_limit"), eylemsiz: sinir < %100 (su an olculen durum) -> Advice; kayit > 0 (gecmis kayit;
// sure / neden tasimaz) -> Info; ikisi de okundu ve temiz -> Good; biri okunamadi ve bulgu yok -> Unknown.
std::vector<Check> cpu_speed_limit_checks(const CpuSpeedLimitInfo& s);

// ---- Sanallastirma isaretleri (F-50; salt okuma, yazma yok) ----
// Hyper-V/WHP/WSL2/Sanal Makine Platformu yalniz hizmet ve dosya varligiyla ISARET olarak okunur.
// Istege bagli ozelligin kesin durumu bu API'lerle kanitlanamaz; metin bunu soyler. VBS/HVCI
// guvenligi icin gerekli olabilir; uygulama hicbir ozelligi kapatmaz, AutoFix yoktur.
struct VirtualizationInfo {
    bool hypervisor_present = false;  // CPUID leaf 1 ECX bit 31 (firmware/VBS/Hyper-V isareti)
    bool hyperv_service = false;      // vmms hizmeti kayitli
    bool vmp_service = false;         // vmcompute hizmeti kayitli
    bool whp_dll = false;             // %SystemRoot%\System32\WinHvPlatform.dll
    bool wsl_exe = false;             // %SystemRoot%\System32\wsl.exe
    bool wsl_default_v2 = false;      // HKCU Lxss DefaultVersion == 2
};
std::vector<Check> virtualization_checks(const VirtualizationInfo& v);

// ---- Goruntu surucusu sagligi (F-68; salt okuma, yazma yok) ----
// Ekran sinifi aygitlarinda sorun kodu ve DriverStore'daki uretici surucu paketi sayisi okunur.
// Birden fazla paket tek basina kalinti kaniti degildir; Basic Display zaten graphics_checks
// tarafindan kanitla raporlanir. Surucu kurulmaz, kaldirilmaz, hicbir ayar yazilmaz.
struct DisplayDriverHealthInfo {
    bool enumeration_ok = false;              // SetupAPI ekran sinifi listesi alinabildi
    std::vector<std::string> problem_devices; // sorun kodlu aygit adlari (bos: sorun yok/bilinmiyor)
    std::optional<std::uint32_t> nvidia_packages, amd_packages, intel_packages;  // DriverStore sayilari
};
std::vector<Check> display_driver_checks(const DisplayDriverHealthInfo& d);

// ---- Sorunlu aygitlar (T17; tum siniflar, salt okuma) ----
// hardware.problem_devices: sorunsuz -> Good; sorun kodlu aygit varsa Advice (sayi + ilk 3 ad; kod 22 "kapali" sayilmaz);
// liste alinamadiysa Unknown (sorun yok denmez). Eylem: Aygit Yoneticisi'ni acar; surucu kurulmaz/kaldirilmaz.
std::vector<Check> problem_device_checks(const inventory::ProblemDeviceReport& r);

// ---- Yazilim / BIOS (CHK-04) ----
struct SoftwareItem {
    std::string name, version;
    std::optional<std::int64_t> date_unix;
};
struct SoftwareInfo {
    std::vector<SoftwareItem> items;          // VC++ yeniden dagitilabilir, NVIDIA App/Adrenalin, Steam, Epic ...
    std::optional<std::int64_t> bios_date_unix;
    std::string board_vendor, board_model;
    std::int64_t now_unix = 0;
    // .NET (salt okuma; kayit yoksa "kurulu degil" sayilir, VC++ ile ayni mantik).
    std::vector<std::string> dotnet_desktop_versions;       // Microsoft.WindowsDesktop.App (.NET 5+) kurulu surumleri
    std::optional<std::uint32_t> dotnet_framework_release;  // NDP\v4\Full "Release" (528040 = 4.8, 533320 = 4.8.1)
};
std::vector<Check> software_checks(const SoftwareInfo& s);

// Kullanici tiklayinca acilacak hedef guvenli mi: ms-settings:<ad>, windowsdefender://coreisolation veya
// izin listesindeki resmi alan adlarina https (kullanici bilgisi/sorgu dizesi tasimayan sayfa baglantisi).
bool action_uri_allowed(std::string_view uri);
// AutoFix henuz baglanmamissa ilgili Windows ayar sayfasi (bos = yok).
std::string fallback_settings_uri(std::string_view fix_key);

// GUIDE-01: tanınan oyun -> prosettings.net/games/<slug>/ (2026-11-05 dogrulanan sayfalar). Bos = yok.
std::string pro_settings_slug(std::string_view launcher, std::string_view app_id, std::string_view name);

// Bolum sirasi + seviye (Problem > Advice > Unknown > Info > Good) ile siralama.
void sort_checks(std::vector<Check>& v);

// EF-X25: gecikmeyle ilgili kontroller tek bolumde. Yeni mod veya yeni ayar degildir; satir sayisi ve eylemleri degismez.
// Olculmemis uctan uca gecikme sayisi yazilmaz; oyun ici ayarlar el ile kalir.
void group_latency_checks(std::vector<Check>& v);

// T06: fare rapor hizi satiri (Gecikme bolumu). Salt okuma; sistem ayari yazilmaz. Bilgi satiridir (puan/Good degil).
// measuring: olcum suruyor (dugme yok). result: son olcum (bellekte; kalici iddia degil). Olcum yoksa sayi uretilmez.
Check mouse_polling_check(bool measuring, const std::optional<measure::PollingResult>& result);

// T10: DNS karsilastirma satirlari (Ag bolumu). Salt okuma olcumu (yalniz DNS sorgusu); sistem ayari yazilmaz.
// existing: son tarama bulgulari ("network.dns.{GUID}" satirindan arayuz ve ilke durumu okunur). Dondurulen satirlar
// "network.dns_bench" ile baslar. Belirgin fark varsa hazir liste secili gelen mevcut fix.dns eylemi sunulur
// (kimlik "network.dns_bench.fix.{GUID}"); aksi halde yalniz bilgi. Rapor yoksa sayi uretilmez.
std::vector<Check> dns_bench_checks(bool measuring, const std::optional<net::DnsBenchReport>& report, const std::vector<Check>& existing);

// EF-X17: kapsam ozeti. Puan degildir: agirlik yok, FPS veya ariza olasiligina cevrilmez.
// Payda = Good + Advice + Problem + Unknown; Info bilgi satiridir, payda disi. Unknown asla "dogrulandi" sayilmaz.
inline constexpr int kReadinessRulesVersion = 1;
struct ReadinessSummary {
    std::size_t total = 0, verified = 0, action = 0, unknown = 0;
    bool has_problem = false;
};
ReadinessSummary summarize_checks(const std::vector<Check>& v) noexcept;

// Oyun Hazirligi "Geri al": bulgu kimligi -> otomatik duzeltmenin journal hedefi ("windows.game_mode" ->
// "gaming:game_mode", "display.1.refresh" -> "display:1:refresh", "windows.hags" -> "hags:hwschmode"). Bos = yok.
// Tek adimlik duzeltmelerde birincil hedef; cok adimli duzeltmede ilk hedef (geri alma icin fix_targets_for_check).
std::string fix_target_for_check(std::string_view check_id);
// Ayni bulgunun TUM journal hedefleri (F-73: arka plan kaydi tek "Duzelt"te uc anahtari kapatir; "Geri al" hepsini
// birlikte geri alir). Bos = otomatik duzeltme yok.
std::vector<std::string> fix_targets_for_check(std::string_view check_id);
// Satirin kendisinden: yenileme hizi satiri args["device"] tasiyorsa hedef aygit ekli olur
// ("display:1:refresh:DISPLAY2"); yoksa kimlige gore (eski bicim).
std::vector<std::string> fix_targets_for_check(const Check& check);
// Uyumluluk B6 (bosluk 10): bulgu hedefi journal hedefiyle eslesir mi. Ayni metin; eksiz yenileme hizi hedefinde
// ayni konumun aygit ekli kaydi ("display:1:refresh" ~ "display:1:refresh:DISPLAY2"); aygit ekli hedefte yalniz
// ayni GDI adli kayit (konum farkli olabilir) veya ayni konumun eksiz eski kaydi.
bool fix_target_matches(std::string_view check_target, std::string_view op_target);
// Her bulgu icin "Geri al" gosterilir mi: hedefinde fixes journal'inda ETKIN duzeltme var (kullanici veya makine).
std::vector<bool> undo_rows(const std::vector<Check>& checks, const std::vector<std::string>& active_fix_targets);

// Satirin geri alma durumu: Undo = etkin duzeltme ("Geri al"); Retry = geri alinmasi dogrulanamamis (RecoveryRequired)
// veya yarim kalmis kayit ("Geri al / Yeniden dene" + durum satiri). Ikisi birden varsa Retry (geri alma hepsini dener).
// Undo/Retry olan satirda ikinci "Duzelt" gosterilmez (hedefte geri alinmamis kayit varken yeni yazma reddedilir).
// Interrupted = yarim kalmis kayit (geri alma henuz DENENMEDI; metin "denendi" demez). Conflict = deger sonradan
// disaridan degistirildi: "Geri al / Yeniden dene" + "Mevcut degeri koru" (ayar yazmadan kaydi kapatir).
// Oncelik: Conflict > Retry > Interrupted > Undo.
enum class UndoState : std::uint8_t { None, Undo, Retry, Interrupted, Conflict };
struct FixTargets {
    std::vector<std::string> active;       // kullanici + makine fixes journal'i: Active
    std::vector<std::string> recovery;     // RecoveryRequired (geri alma denendi, dogrulanamadi)
    std::vector<std::string> interrupted;  // yarim kalmis (Prepared/Applying/...)
    std::vector<std::string> conflict;     // RecoveryRequired + dis degisiklik
};
std::vector<UndoState> undo_states(const std::vector<Check>& checks, const FixTargets& targets);

}  // namespace ef::core::readiness
