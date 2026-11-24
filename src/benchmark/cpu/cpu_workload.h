#pragma once
// BENCH-02 / BENCH-08: Uygulama Testi CPU alt testi - deterministik is yukleri (cpu_v2).
//
// ONEMLI: Bu skor "CPU is birimi / saniye" (cpu work units per second) olcer.
// Gercek bir oyundaki FPS tahmini DEGILDIR ve FPS'e cevrilmez. Sonuc yapisindaki
// is_fps_prediction alani sabit olarak false'tur; UI/rapor bu skoru FPS diye gosteremez.
//
// Is yuku surumu: "cpu_v2". Bes cekirdek (kernel) ayri fazlarda, her biri varyant suresinin
// esit payi kadar calisir. Bir "is birimi" = bir cekirdegin bir kez, sabit tohumdan
// (seed, cekirdek, birim indeksi) turetilen girdiyle calistirilmasi:
//   0) sim     : cpu_v1 parcasi (tamsayi parcacik fizigi + izgara BFS + kucuk siralama).
//   1) nbody   : cift duyarlikli (double) N-cisim kutle cekimi, O(N^2) kuvvet + sqrt/bolme.
//   2) matmul  : cift duyarlikli bloklu yogun matris carpimi (k sirasi sabit -> bit-ayni sonuc).
//   3) ray     : cift duyarlikli isin-kure kesisimi; birincil + golge + bir yansima (dallanmali FP).
//   4) bigdata : buyuk calisma kumesi tamsayi: LSD radix siralama + acik adresli hash-join.
//                Varsayilan birim basina ~8 MiB (L2'yi asar; cok is parcacigi L3/DRAM'i zorlar).
// FP cekirdekleri yalniz + - * / sqrt kullanir (IEEE dogru yuvarlanan; CRT surumune bagli degil).
// Derleme: ef_benchmark hedefi /fp:precise ile derlenir ve cpu_workload.cpp "#pragma fp_contract(off)"
// kullanir; MSVC'de "/fp:contract-" bayragi yoktur, FMA birlestirmesi bu pragma ile kapatilir. Boylece
// a*b+c iki ayri yuvarlamayla hesaplanir ve sonuc /arch veya derleyici surumunden bagimsiz bit-aynidir.
//
// bigdata bellegi: isci basina bigdata_bytes_per_worker(big_elements) (~8 MiB varsayilan). Cok is
// parcacikli fazda isci sayisi RAM butcesiyle (memory::compute_budget, varsayilan politika) sinirlanir;
// azaltildiysa KernelResult::threads ve CpuVariantResult::memory_limited bunu gosterir. Tek bir isci bile
// butceye sigmiyorsa (tek is parcacikli varyant dahil) big_elements, her iki varyant icin ayni olacak sekilde,
// bigdata_elements_for_budget ile yarilanarak kucultulur (en az kMinBigElements); bu durumda bigdata is yuku
// varsayilan degildir (bigdata icin altin denetim yapilmaz, bigdata skoru varsayilan kosularla karsilastirilamaz),
// memory_limited her iki varyantta isaretlenir ve etkin eleman sayisi CpuVariantResult::bigdata_elements'te
// raporlanir. Diger cekirdeklerin altin denetimi etkilenmez (golden_applies cekirdek bazindadir).
// Butce bilinmiyorsa (snapshot yok / politika atladi): kucultme YAPILMAZ (ST varsayilan boyutla kosar; ST zaten
// tek isciyle ~8 MiB kullanir), yalniz MT bigdata fazi guvenli tarafta tek isciyle kosar; MT memory_limited
// isaretlenir ve CpuVariantResult::bigdata_budget_known = false bunu "butce olculemedi" diye ayirir.
// big_elements yapilandirma dogrulamasinda (validated_config) kMaxBigElements'e (< 2^31) kirpilir.
//
// Cekisme: her isci kendi CPU suresini (GetThreadTimes cekirdek + kullanici, ~15,6 ms tanecik) ve duvar
// suresini olcer. Pay = CPU / duvar. Isci duvar suresi kMinShareWallSeconds altindaysa pay bilinmez (bos).
// En dusuk pay < kUncontendedThreadCpuShare ise varyant "contended" (rakip yuk) isaretlenir. Pay bilinmiyorsa
// (kisa faz) contended = false kalir ama bu "cekismesiz" demek DEGILDIR: contention() uc durumlu sonucu verir
// (Unknown / Uncontended / Contended); cagiranlar bool yerine onu kullanmalidir. Tek ve cok is parcacikli
// varyantlarin ikisi de ayni kurala tabidir.
//
// Varyant skoru: cekirdek birim/sn degerlerinin geometrik ortalamasi (orana dayali karsilastirma
// icin olcek bagimsiz bilesik endeks). Cekirdek bazinda birim/sn ayrica dondurulur.
//
// Dogruluk: varsayilan yapilandirmada her cekirdegin ilk birimleri gomulu altin degerlerle olcumden
// once karsilastirilir (altin degerler birim testte bagimsiz referansla dogrulanir). Tek ve cok is
// parcacikli varyantlarin her cekirdek icin ortak birim onekinin checksum'u esit olmak zorundadir.
// Uyusmazlik -> sonuc gecersiz, puanlanmaz.
//
// Is parcaciklari: cok is parcacikli varyant tum mantiksal islemcileri std::thread ile kullanir;
// oncelik/affinity degistirilmez (baska sureclere dokunulmaz).
//
// Iptal: std::atomic<bool>& her birim arasinda okunur. Iptal edilen kosu "cancelled" isaretlenir,
// kismi sonuc puanlanmaz.

#include <atomic>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ef::benchmark::cpu {

inline constexpr std::string_view kWorkloadVersion = "cpu_v2";
inline constexpr std::string_view kScoreUnit = "cpu_work_units_per_second";
// EF-X22.02 CPU FP tolerans politikasi (docs/05_BENCHMARK_SPEC.md "CPU FP tolerans politikasi"): FP cekirdekleri
// yalniz dogru yuvarlanan + - * / sqrt kullanir, /fp:precise + fp_contract(off) ile ISA/derleyiciden bagimsiz bit-aynidir.
// Bu yuzden tolerans 0 ULP'dir: altin ya da ST/MT checksum uyusmazligi her zaman hata sayilir, "yakin" sonuc kabul edilmez.
// CRT'ye bagli fonksiyon (sin/exp/pow...) eklenirse cekirdek checksum disinda kalir ya da surum + belgelenmis tolerans ister.
inline constexpr std::uint32_t kCpuFpToleranceUlps = 0;
inline constexpr std::uint64_t kDefaultSeed = 0x17A5'C0DE'B3C4'0002ULL;

enum class Kernel : std::uint32_t { Sim = 0, NBody = 1, MatMul = 2, Ray = 3, BigData = 4 };
inline constexpr std::uint32_t kKernelCount = 5;
inline constexpr std::uint32_t kAllKernels = (1u << kKernelCount) - 1u;
inline constexpr std::uint32_t kernel_bit(Kernel k) { return 1u << static_cast<std::uint32_t>(k); }
// Kararli ASCII ad (metrik anahtari icin): "sim", "nbody", "matmul", "ray", "bigdata".
std::string_view kernel_name(Kernel k) noexcept;

struct WorkloadSizes {
    // sim (cpu_v1)
    std::uint32_t particles = 512;      // parcacik sayisi
    std::uint32_t physics_steps = 24;   // fizik adimi
    std::uint32_t grid_side = 64;       // yol bulma izgarasi kenari
    std::uint32_t sort_elements = 4096; // siralanacak eleman sayisi
    // nbody
    std::uint32_t nbody_bodies = 192;
    std::uint32_t nbody_steps = 8;
    // matmul (n x n double; 3 matris)
    std::uint32_t matrix_n = 192;
    // ray (genislik x yukseklik piksel, kure sayisi)
    std::uint32_t ray_width = 96;
    std::uint32_t ray_height = 64;
    std::uint32_t ray_spheres = 24;
    // bigdata: radix siralanan anahtar sayisi; build tarafi = yarisi, tablo = 2^k >= 2*build.
    // Gecerli aralik: <= kMaxBigElements (validated_config kirpar).
    std::uint32_t big_elements = 1u << 19;

    bool operator==(const WorkloadSizes&) const = default;
};

struct CpuConfig {
    std::uint64_t seed = kDefaultSeed;
    WorkloadSizes sizes{};
    double time_limit_s = 2.0;    // eski alan: st_seconds/mt_seconds 0 ise varyant basina sure
    double st_seconds = 0.0;      // tek is parcacikli varyant toplam suresi; 0 = time_limit_s
    double mt_seconds = 0.0;      // cok is parcacikli varyant toplam suresi; 0 = time_limit_s
    std::uint64_t max_chunks = 0; // 0 = yalniz sure siniri; >0 = cekirdek basina en fazla bu kadar birim
    std::uint32_t threads = 0;    // cok is parcacikli varyant icin; 0 = tum islemci gruplarindaki mantiksal CPU
    std::uint32_t kernel_mask = kAllKernels;  // calistirilacak cekirdekler (kernel_bit)
    // bigdata MT isci butcesi (bayt). 0 = OS bellek durumundan memory::compute_budget (varsayilan politika).
    // Test kancasi; gercek kullanimda 0 kalir.
    std::uint64_t bigdata_budget_bytes = 0;
    // Test kancasi: true ise butce "bilinmiyor" sayilir (compute_budget atladi/okunamadi durumunun benzetimi).
    bool bigdata_budget_unknown = false;
};

// bigdata anahtar alani 2*E ve 32 bit anahtar/etiket kullanir: E < 2^31 olmak zorunda.
inline constexpr std::uint32_t kMaxBigElements = 0x7FFFFFFFu;
// RAM butcesi yetmezse kucultmenin alt siniri (isci basina 64 KiB).
inline constexpr std::uint32_t kMinBigElements = 1u << 12;
// Yapilandirma dogrulamasi: big_elements kMaxBigElements'e kirpilir (diger alanlar aynen).
CpuConfig validated_config(const CpuConfig& config);
// bigdata isci basina calisma alani (bayt): anahtar + gecici (2 * 4 * E) + hash tablosu (8 * 2^k).
std::uint64_t bigdata_bytes_per_worker(std::uint32_t elements) noexcept;
// threads isciden butceye sigan kadari: min(threads, budget / isci basina bayt), en az 1.
std::uint32_t bigdata_thread_cap(std::uint32_t threads, std::uint32_t elements, std::uint64_t budget_bytes) noexcept;
// Tek iscinin calisma alani butceyi asarsa elements yarilanir (kMinBigElements altina inmez; zaten altindaysa
// degismez). Sigiyorsa aynen doner.
std::uint32_t bigdata_elements_for_budget(std::uint32_t elements, std::uint64_t budget_bytes) noexcept;

// Altin checksum'lar bu cekirdek icin gecerli mi: tohum varsayilan ve cekirdegin kullandigi boyut alanlari
// varsayilan (ornegin bigdata kucultulduyse yalniz bigdata icin false; diger cekirdekler denetlenmeye devam eder).
bool golden_applies(Kernel k, const CpuConfig& config) noexcept;

// Cekisme esigi ve pay olcumu icin en kisa isci duvar suresi.
inline constexpr double kUncontendedThreadCpuShare = 0.90;
inline constexpr double kMinShareWallSeconds = 0.2;

// Hazir sureler (duz yapilandirma degerleri). Hizli: ST 5 sn + MT 10 sn; Tam: ST 20 sn + MT 60 sn.
inline constexpr double kQuickStSeconds = 5.0;
inline constexpr double kQuickMtSeconds = 10.0;
inline constexpr double kFullStSeconds = 20.0;
inline constexpr double kFullMtSeconds = 60.0;
CpuConfig quick_cpu_config();
CpuConfig full_cpu_config();
// Varyant icin gecerli toplam sure (st/mt 0 ise time_limit_s).
double variant_seconds(const CpuConfig& config, bool multi_thread) noexcept;

// --- Tek tek is yukleri (testler bagimsiz referansla karsilastirir) ---
std::uint64_t particle_workload(std::uint64_t seed, std::uint32_t particles, std::uint32_t steps);
std::uint64_t path_workload(std::uint64_t seed, std::uint32_t grid_side);
std::uint64_t sort_workload(std::uint64_t seed, std::uint32_t elements);
std::uint64_t nbody_workload(std::uint64_t seed, std::uint32_t bodies, std::uint32_t steps);
std::uint64_t matmul_workload(std::uint64_t seed, std::uint32_t n);
std::uint64_t ray_workload(std::uint64_t seed, std::uint32_t width, std::uint32_t height, std::uint32_t spheres);
std::uint64_t bigdata_workload(std::uint64_t seed, std::uint32_t elements);

// Parca tohumunun ve alt tohumlarin turetilmesi (testteki referans da ayni tanimi kullanir).
std::uint64_t chunk_seed(std::uint64_t seed, std::uint64_t index);
// sim cekirdeginin bir parcasinin checksum'u: chunk_seed + uc alt is yukunun katlanmasi.
std::uint64_t chunk_checksum(std::uint64_t seed, std::uint64_t index, const WorkloadSizes& sizes);
// Herhangi bir cekirdegin bir biriminin checksum'u. Sim icin chunk_checksum ile aynidir; digerleri
// icin birim tohumu = chunk_seed(seed ^ cekirdek tuzu, index), checksum = fold(fold(0, tohum), yuk).
std::uint64_t kernel_salt(Kernel k) noexcept;
std::uint64_t unit_checksum(Kernel k, std::uint64_t seed, std::uint64_t index, const WorkloadSizes& sizes);
// Iki 64 bitlik degeri sira duyarli katlar (parca ve kosu checksum'lari icin).
std::uint64_t fold(std::uint64_t acc, std::uint64_t value);

// Varsayilan tohum + boyutlar icin ilk parcalarin gomulu altin checksum'lari.
inline constexpr std::uint32_t kGoldenCount = 4;
std::uint64_t golden_chunk_checksum(std::uint32_t index);  // sim; index < kGoldenCount
inline constexpr std::uint32_t kGoldenUnits = 2;
std::uint64_t golden_unit_checksum(Kernel k, std::uint32_t index);  // index < kGoldenUnits (sim: kGoldenCount)

enum class CpuRunStatus {
    Ok,               // tamamlandi, checksum dogru, puanlanabilir
    Cancelled,        // kullanici iptali; kismi sonuc puanlanmaz
    ChecksumMismatch, // dogruluk kontrolu basarisiz; puanlanmaz
    NoWork,           // hic parca tamamlanmadi (sure siniri cok kisa vb.)
};

struct KernelResult {
    Kernel kernel = Kernel::Sim;
    std::uint32_t threads = 0;         // bu fazda calisan isci (bigdata butce siniriyla varyanttan az olabilir)
    std::uint64_t units = 0;           // tamamlanan birim
    double elapsed_s = 0.0;            // bu cekirdek fazinin duvar saati suresi
    double units_per_second = 0.0;     // units / elapsed_s ; FPS degil
    std::uint64_t checksum = 0;        // birim checksum'larinin indeks sirasinda katlanmasi
    // Isci basina CPU suresi / isci duvar suresi (isci sirasinda). Herhangi bir isci icin okunamazsa
    // veya duvar suresi kMinShareWallSeconds altindaysa vektor bos ve min bos kalir (tahmin yok).
    std::vector<double> thread_cpu_share;
    std::optional<double> min_thread_cpu_share;
};

struct CpuVariantResult {
    std::uint32_t threads = 0;
    std::uint64_t chunks = 0;          // tum cekirdeklerde tamamlanan is birimi toplami
    double elapsed_s = 0.0;            // fazlarin toplam duvar saati suresi
    double work_units_per_second = 0;  // cekirdek birim/sn geometrik ortalamasi ; FPS degil
    std::uint64_t checksum = 0;        // cekirdek checksum'larinin cekirdek sirasinda katlanmasi
    bool cancelled = false;
    std::vector<KernelResult> kernels; // calisan cekirdekler, cekirdek sirasinda
    // Fazlarin en dusuk isci CPU payi; bir faz bile bilinmiyorsa bos.
    std::optional<double> min_thread_cpu_share;
    bool contended = false;            // min_thread_cpu_share < kUncontendedThreadCpuShare
    bool memory_limited = false;       // bigdata isci sayisi veya eleman sayisi RAM butcesiyle azaltildi
    std::uint32_t bigdata_elements = 0;  // etkin bigdata eleman sayisi (bigdata kosmadiysa 0)
    bool bigdata_elements_reduced = false;  // eleman sayisi butce yuzunden kucultuldu (is yuku varsayilan degil)
    bool bigdata_budget_known = false;      // bigdata kostu ve RAM butcesi olculebildi (kosmadiysa false)
};

// Cekisme uc durumludur: pay olculemediyse (faz < kMinShareWallSeconds, GetThreadTimes yok) Unknown.
enum class Contention { Unknown, Uncontended, Contended };
Contention contention(const CpuVariantResult& v) noexcept;
const char* to_string(Contention c) noexcept;  // "unknown" / "uncontended" / "contended"

struct CpuBenchmarkResult {
    std::string_view workload_version = kWorkloadVersion;
    std::string_view score_unit = kScoreUnit;
    // Bu skor oyun FPS tahmini degildir; FPS'e cevrilmez, FPS diye sunulmaz.
    bool is_fps_prediction = false;

    CpuVariantResult single_thread;
    CpuVariantResult multi_thread;
    std::uint32_t hardware_threads = 0;  // tum islemci gruplarindaki mantiksal CPU (cpu_groups.h logical_processor_count)

    bool checksum_ok = false;  // altin degerler + ST/MT ortak onek tutarliligi
    bool cancelled = false;
    bool scored = false;       // yalniz Ok durumunda true
    CpuRunStatus status = CpuRunStatus::NoWork;
};

// Tek bir varyanti (threads is parcacigi) calistirir: her cekirdek sirayla, sure payi
// variant_seconds(config, threads > 1) / etkin cekirdek sayisi. Birim checksum'lari indeks sirasinda katlanir.
CpuVariantResult run_variant(const CpuConfig& config, std::uint32_t threads, std::atomic<bool>& cancel);

// Once tek is parcacikli, sonra cok is parcacikli varyanti ayri ayri olcer ve dogrular.
CpuBenchmarkResult run_cpu_benchmark(const CpuConfig& config, std::atomic<bool>& cancel);

// Checksum havuzunun (optimizer olu kod silemesin diye yazilan atomik deger) son degeri.
std::uint64_t checksum_sink_value() noexcept;

}  // namespace ef::benchmark::cpu
