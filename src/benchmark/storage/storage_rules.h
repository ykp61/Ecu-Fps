#pragma once
// BENCH-05: depolama alt testinin saf kurallari (Win32 cagrisi yok, birim testlenir).
// - Hedef yalniz kullanici secimi bir surucu harfi kokudur ("X:\"); ham disk/volume tanitici yollari reddedilir.
// - Test dosyasi bizim olusturdugumuz benzersiz klasorde yeni dosyadir; mevcut kullanici dosyasi acilmaz.
// - Bos alan alt siniri: dosya boyu + max(%10 volume, 10 GiB).
// - Tamponlu kucuk dosya sonucu "onbellek" etiketlidir; surdurulebilir disk hizi diye sunulmaz.
// BENCH-09: HIZLI/TAM limit on ayarlari, seq QD8 ve rastgele 4 KiB yazma profilleri, profil basi
// gecikme persentilleri (p50/p95/p99) ve yazma butcesinin profiller arasinda adil paylasimi.

#include <cstdint>
#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::benchmark::storage {

inline constexpr std::uint64_t kKiB = 1024ull;
inline constexpr std::uint64_t kMiB = 1024ull * kKiB;
inline constexpr std::uint64_t kGiB = 1024ull * kMiB;

// Klasor/dosya adlari: yalniz bunlar bizim olusturdugumuz ve silebilecegimiz girdilerdir.
inline constexpr std::wstring_view kFolderPrefix = L"EcuFps-bench-";
inline constexpr std::wstring_view kMarkerName = L"itx-bench.marker";
inline constexpr std::wstring_view kTestFileName = L"test.bin";
inline constexpr std::wstring_view kCacheFileName = L"cache.bin";
inline constexpr std::string_view kMarkerMagic = "EcuFps-bench-v1";

// ---- Hedef dogrulama ----
enum class TargetVerdict {
    Ok,             // "X:\" veya "X:" bicimi
    Empty,
    RawDevice,      // \\.\PhysicalDriveN, \\.\C:, \\?\GLOBALROOT, \\?\Volume{...} vb. ham tanitici
    Network,        // \\sunucu\paylasim
    Relative,       // "C:foo", "foo"
    NotVolumeRoot,  // volume kokunden farkli yol (kullanici klasoru/dosyasi hedef olamaz)
    InvalidChars,
};
TargetVerdict validate_volume_root(std::wstring_view root);
const char* verdict_reason(TargetVerdict v);
// "c:" / "C:\" -> "C:\" (dogrulanmis kok icin); gecersizse bos.
std::wstring normalize_volume_root(std::wstring_view root);

// ---- Bos alan alt siniri ----
// Gerekli bos alan = dosya + max(toplam/20, 5 GiB) (uyumluluk bosluk 7: %86+ dolu 256-512 GB SSD'lerde de
// olculebilsin; once max(%10, 10 GiB) idi). Tasma doygun (UINT64_MAX).
std::uint64_t free_space_floor(std::uint64_t volume_total_bytes);
std::uint64_t required_free_bytes(std::uint64_t file_bytes, std::uint64_t volume_total_bytes);
// Dosya bos alanin permille/1000 payini asmiyor mu (0 = kural kapali).
bool within_free_share(std::uint64_t file_bytes, std::uint64_t free_bytes, std::uint32_t permille);
// within_free_share'i saglayan en kucuk bos alan (0 = kural kapali; tasma doygun UINT64_MAX).
std::uint64_t share_required_free(std::uint64_t file_bytes, std::uint32_t permille);
bool free_space_sufficient(std::uint64_t free_bytes, std::uint64_t volume_total_bytes, std::uint64_t file_bytes);

// ---- Klasor adi ----
std::wstring bench_folder_name(std::uint64_t random_id);  // EcuFps-bench-<16 hex>
bool is_bench_folder_name(std::wstring_view name);
// final_path, folder'in dogrudan veya derin alt girdisi mi (buyuk/kucuk harf duyarsiz, "\\?\" oneki yok sayilir).
bool path_is_under(std::wstring_view final_path, std::wstring_view folder);
std::string make_marker_content(std::uint32_t pid);
bool marker_content_valid(std::string_view content);

// ---- Yetim klasor karari ----
struct OrphanEntry {
    std::wstring name;
    bool is_dir = false;
    bool is_reparse = false;
    std::uint32_t link_count = 1;
};
enum class OrphanDecision {
    Delete,          // yalniz bizim dosyalarimiz + gecerli isaret
    NotOurs,         // ad deseni uymuyor
    FolderReparse,   // klasorun kendisi baglanti: asla inilmez
    NoMarker,        // isaret dosyasi yok/gecersiz: kullanici klasoru olabilir
    ForeignContent,  // bilinmeyen girdi, alt klasor, baglanti veya hardlink var
};
OrphanDecision decide_orphan(std::wstring_view folder_name, bool folder_is_reparse, bool marker_valid,
                             const std::vector<OrphanEntry>& entries);

// ---- Profil ve limitler ----
enum class Pattern { Sequential, Random };
enum class Op { Read, Write };
enum class CacheMode { Unbuffered, Buffered };
// Etiket: Unbuffered sonucu "disk" (NO_BUFFERING|WRITE_THROUGH), Buffered sonucu "cache" (OS onbellegi).
// Burst: tamponsuz ama yazma butcesi 1 sn dolmadan bitti -> cihaz ici onbellek karsilamis olabilir;
// surdurulebilir disk hizi diye sunulmaz.
enum class SpeedLabel { DiskUnbuffered, OsCache, Burst };
inline constexpr double kMinSustainedSeconds = 1.0;
SpeedLabel label_after_run(SpeedLabel l, bool byte_cap_hit, double seconds) noexcept;

struct Profile {
    const char* id = "";
    Pattern pattern = Pattern::Sequential;
    Op op = Op::Read;
    std::uint32_t block_bytes = 0;
    std::uint32_t queue_depth = 1;
    CacheMode cache = CacheMode::Unbuffered;
};
SpeedLabel label_of(const Profile& p);
const char* label_text(SpeedLabel l);  // "disk" / "cache" / "burst"
// BENCH-09 kayitli profil listesi (sira sabit):
//   seq 1 MiB QD1/QD8 okuma, rastgele 4 KiB QD1/QD4/QD32 okuma,
//   rastgele 4 KiB QD1/QD32 yazma, seq 1 MiB QD1/QD8 yazma, tamponlu 64 KiB cache okuma.
// Okumalar once: hazirlanan dosya uzerinde yazma profillerinden etkilenmeden olculur.
// Yavas rastgele yazmalar seq yazmalardan once: kullanmadiklari butce payi sonrakilere kalir.
std::vector<Profile> default_profiles();
std::uint32_t count_write_profiles(const std::vector<Profile>& ps);  // tamponsuz yazma profili sayisi

struct Limits {
    // Handoff v1 04_BENCHMARK: dosya en fazla 512 MiB; kisa koşuda toplam yazma <= 1 GiB/birim. Bos alan payi
    // uyumluluk bosluk 7 ile %1'den %10'a cikti (%1 kurali TAM testte ~205 GiB bos alan istiyordu).
    // Varsayilan == quick_limits().
    std::uint64_t file_bytes = 512 * kMiB;        // ana test dosyasi (onceden rastgele veriyle doldurulur)
    std::uint64_t cache_file_bytes = 32 * kMiB;   // tamponlu kucuk dosya
    std::uint64_t write_budget_bytes = 1 * kGiB;  // hazirlik dahil toplam yazma
    std::uint32_t max_free_permille = 100;        // dosya bos alanin binde 100'unu (%10) asarsa test ATLANIR (kirpilmaz: is yuku sabit)
    std::uint32_t profile_ms = 3000;              // profil basi sure
    std::uint32_t total_ms = 60000;               // tum test icin ust sinir
    // EF-X22: test dosyasinin ilk verify_bytes'i yaz-oku-karsilastir ile dogrulanir (MiB kati, <= file_bytes,
    // 0 = kapali). Yazma butcesine dahildir; ek bos alan istemez.
    std::uint64_t verify_bytes = 64 * kMiB;
};
// HIZLI: 512 MiB dosya, 1 GiB yazma butcesi, profil basi 3 s, toplam <= 60 s (Limits{} ile ayni).
Limits quick_limits();
// TAM: 2 GiB dosya, 4 GiB yazma butcesi, profil basi 8 s, toplam <= 150 s. Ayni %10 bos alan kurali:
// ~20 GiB bos alan yoksa test "free_share_limit" ile ATLANIR. Yazma miktari onceden gosterilmelidir
// (max_host_write_bytes).
Limits full_limits();
// Testin en fazla yazacagi byte (hazirlik dahil); kullaniciya onceden gosterilecek deger.
std::uint64_t max_host_write_bytes(const Limits& l);
// Planlanan olcum suresi (hazirlik haric): min(total_ms, profile_ms * profil sayisi).
std::uint64_t planned_measure_ms(const Limits& l, std::size_t profile_count);
enum class LimitsVerdict { Ok, FileTooSmall, FileNotAligned, CacheFileInvalid, BudgetBelowPreparation, TimeInvalid, VerifyInvalid };
LimitsVerdict validate_limits(const Limits& l);
const char* limits_reason(LimitsVerdict v);
// Hazirlik yazimi (dosya + cache dosyasi doldurma) byte'i.
std::uint64_t preparation_write_bytes(const Limits& l);
// Testin atlanmamasi icin gereken bos alan: iki kuralin (alt sinir + dosya payi) buyugu. Atlama metni bunu
// mevcut bos alanla birlikte gosterir (bosluk 7).
std::uint64_t required_free_for(const Limits& l, std::uint64_t volume_total_bytes);
// Butceden kalan; istenen yazma kalan butceyi asarsa kirpilir (blok hizali, asagi yuvarlanir).
std::uint64_t clamp_write(std::uint64_t budget, std::uint64_t used, std::uint64_t request, std::uint32_t block);
// Yazma profili payi: (butce - kullanilan - ayrilan) / kalan yazma profili; blok hizali, asagi yuvarlanir.
// Onceki profilin kullanmadigi pay sonrakilere kalir; toplam asla butceyi asmaz. remaining_writers 0 -> 0.
std::uint64_t write_share(std::uint64_t budget, std::uint64_t used, std::uint64_t reserved,
                          std::uint32_t remaining_writers, std::uint32_t block);

// ---- Gecikme histogrami ----
// Log-dogrusal kova (HDR benzeri): 0..63 ns tam, sonrasi her ikinin kuvveti 64 alt kova (goreli hata <= 1/64).
// En yuksek tam kova buyuklugu 2^36 (2^36..2^37-1 ns); 2^37 ns (~137.4 s) ve ustu son kovaya kirpilir
// (max_us yine gercek degeri tutar). Bellek sabit (~16 KiB), ornek sayisindan bagimsiz.
class LatencyHistogram {
public:
    static constexpr std::uint32_t kSubBits = 6;
    static constexpr std::uint32_t kSub = 1u << kSubBits;  // 64
    static constexpr std::uint32_t kMaxMagnitude = 36;     // son tam buyukluk: [2^36, 2^37) ns = [68.7 s, 137.4 s)
    static constexpr std::uint32_t kBuckets = (kMaxMagnitude - kSubBits + 2) * kSub;
    void add_ns(std::uint64_t ns) noexcept;
    void merge(const LatencyHistogram& o) noexcept;
    std::uint64_t count() const noexcept { return count_; }
    // Yakin-sira (nearest-rank) persentil, p in [0,100]; kovanin orta noktasi (us). Bos -> 0.
    // Sira tamsayi ile: p 0.01 adima yuvarlanir, rank = ceil(p_bp * count / 10000), en az 1
    // (kayan nokta hatasi ile p99/n=100 -> 100 gibi bir sira kaymasi olmaz).
    double percentile_us(double p) const noexcept;
    double mean_us() const noexcept { return count_ ? static_cast<double>(sum_ns_) / count_ / 1000.0 : 0.0; }
    double max_us() const noexcept { return static_cast<double>(max_ns_) / 1000.0; }
    static std::uint32_t bucket_of(std::uint64_t ns) noexcept;
    static std::uint64_t bucket_low_ns(std::uint32_t idx) noexcept;
    static std::uint64_t bucket_width_ns(std::uint32_t idx) noexcept;

private:
    std::uint64_t counts_[kBuckets] = {};
    std::uint64_t count_ = 0;
    std::uint64_t sum_ns_ = 0;
    std::uint64_t max_ns_ = 0;
};

// ---- Slot adaleti (QD>1 tamamlanma acligi denetimi) ----
// Gecikme gonderimden (ReadFile/WriteFile oncesi) porttan alinmaya kadar olculur ve slot alinir alinmaz yeniden
// gonderilir; bu yuzden ortalama gecikme ~ QD/IOPS (Little) neredeyse tanim geregi tutar ve aclik yakalamaz: ac
// kalan slotun beklemesi kendi gecikmesine dahildir. Acligi gosteren, slot basina tamamlanan G/C sayisidir:
// adil hizmette her slot ~ops/QD tamamlar; en dusuk isaretli slotu kayiran bekleme (eski WaitForMultipleObjects)
// yuksek slotlari ~0'a indirir. Oran = en az / ortalama.
//
// Esigin turetilmesi (sabit 0.5 saglikli ama yuksek degiskenlikli cihazlari yanlis isaretliyordu):
// Her slot kapali bir dongudur: tamamlanir tamamlanmaz yeniden gonderilir, yani slotun T suredeki tamamlanma
// sayisi N bir yenileme (renewal) surecidir; dongu suresi ~ G/C gecikmesi (ortalama mu, degisim katsayisi c).
// Yenileme MLT'si: N ~ Normal(m, c^2 * m), m = T/mu. Adil hizmette k slot ayni dagilimdan bagimsiz cekilir; en
// dusuk slotun m - z*c*sqrt(m) altina dusme olasiligi Bonferroni ile <= k * Phi(-z) <= k * exp(-z^2/2) / 2.
// Hedef yanlis alarm alfa = kSlotFalseAlarm icin z = sqrt(2 * ln(k / (2 * alfa))) (k = 32: z ~ 4.9).
// Ornek: m = 100, c = 3 (GC duraklamali SSD yazmasi): beklenen en dusuk ~ 100 - 2.07*30 ~ 38 (oran ~0.38,
// eski 0.5 esigi "ac" derdi); yeni taban 100 - 4.9*30 < 0 -> isaretlenmez. Gercek aclik (slotlar ~0 tamamlar)
// m buyudukce tabanin cok altinda kalir.
// c icin slot bazinda gecikme ortalama/varyansindan EN BUYUK c kullanilir, ama yalniz ornek sayisi (= tamamlanan
// G/C) ust medyanin (cift k'da k/2+1. en kucuk deger) ve kMinCvSamples'in ustundeki slotlardan: tek tek slotlar
// kendi dongulerini olcer; en buyugu almak tabani asagi ceker (muhafazakar, yanlis alarmi azaltir). Ac kalan slot c'ye GIRMEZ: gec kuyruktan alinan
// bir slotun gecikmeleri duzensiz ve cok uzundur, kendi c'si yuksektir; c'ye girseydi tabani kendisi 0'in altina
// ceker ve kendi acligini gizlerdi (ornek: 32 slot ort ~200, tek slot 50 G/C ve c = 3 -> 200 - 4.9*3*14 < 0).
// Aclik, slotu tanim geregi medyanin altina iter; bu yuzden medyan kesimi acligi c'den disarida tutar. Havuzlanmis
// c de ayni nedenle kullanilmaz. Sinir: slotlarin yarisindan FAZLASI (ust medyan: k = 32'de en az 17; tam yarisi
// yetmez) kMinCvSamples'tan cok ornekle kismen ac kalirsa medyan da ac seviyeye iner ve c sisebilir (o durumda "aclik yok" iddiasi muhafazakar tarafta kalir;
// eski WaitForMultipleObjects aclik deseninde ac slotlar ~0 tamamlar ve zaten kMinCvSamples altindadir).
// Iki kosul birlikte gerekir: min < taban (istatistiksel) VE oran < kMinSlotFairness (pratik buyukluk: cok buyuk
// m'de kucuk ama anlamli sapmalar "aclik" sayilmaz). Ortalama kMinOpsPerSlot altindaysa (kisa/burst profil, MLT
// yaklasimi zayif) veya c bilinmiyorsa known = false (aclik iddiasi da, adalet iddiasi da yok).
inline constexpr double kMinOpsPerSlot = 100.0;
inline constexpr double kMinSlotFairness = 0.5;
inline constexpr double kSlotFalseAlarm = 1e-4;
inline constexpr std::uint64_t kMinCvSamples = 10;

// Slot basina gecikme (dongu suresi) birikimi: ortalama ve varyans icin (birim fark etmez, oran kullanilir).
struct SlotLatency {
    std::uint64_t n = 0;
    double sum = 0.0;
    double sum_sq = 0.0;
    void add(double x) noexcept {
        ++n;
        sum += x;
        sum_sq += x * x;
    }
};
// Adalet tabani icin dongu CV'si: ornek sayisi >= alt medyan (tum slotlar uzerinden) ve >= kMinCvSamples olan
// slotlarin en buyuk degisim katsayisi (sd / ortalama); uygun slot yoksa bos.
std::optional<double> fairness_cycle_cv(const std::vector<SlotLatency>& per_slot);
// k slot icin Bonferroni z degeri (yukaridaki turetme); k < 1 icin 1 alinir.
double slot_starvation_z(std::uint32_t slots) noexcept;

struct SlotFairness {
    bool known = false;          // yeterli ornek ve c biliniyor mu
    std::uint32_t slots = 0;
    std::uint64_t min_ops = 0;
    std::uint64_t max_ops = 0;
    double mean_ops = 0.0;
    double ratio = 0.0;          // min_ops / mean_ops (known degilse yine hesaplanir, bilgi amacli)
    std::optional<double> cycle_cv;  // kullanilan dongu degisim katsayisi
    double z = 0.0;                  // slot_starvation_z(slots)
    double floor_ops = 0.0;          // mean - z * c * sqrt(mean) (known ise)
    bool starved = false;            // known && min_ops < floor_ops && ratio < kMinSlotFairness
};
SlotFairness slot_fairness(const std::vector<std::uint64_t>& per_slot_ops, std::optional<double> cycle_cv);

// ---- Sonuclar ----
enum class Status { Ok, Partial, Skipped, Cancelled, Refused, Failed };
const char* status_text(Status s);

struct ProfileResult {
    Profile profile;
    SpeedLabel label = SpeedLabel::DiskUnbuffered;
    Status status = Status::Skipped;
    std::uint64_t bytes = 0;
    std::uint64_t ops = 0;
    double seconds = 0.0;
    std::string reason;  // atlandi/kismi nedeni (ASCII anahtar)
    std::uint64_t byte_cap = 0;  // yazma profili icin ayrilan butce payi (0 = okuma, sinirsiz)
    // Gonderim/tamamlama is parcacigi sayisi (toplam bekleyen G/C = queue_depth; 0 = kosulmadi).
    std::uint32_t io_threads = 0;
    // Tamamlanan her G/C icin gozlenen gecikme (gonderim -> tamamlanmanin uygulamaca alinmasi), mikrosaniye.
    std::uint64_t lat_samples = 0;
    double lat_mean_us = 0.0;
    double lat_p50_us = 0.0;
    double lat_p95_us = 0.0;
    double lat_p99_us = 0.0;
    double lat_max_us = 0.0;
    // Tamponsuz motor (QD slotlari): slot basina tamamlanan G/C adaleti. Tamponlu/kosulmamis profilde slots = 0.
    SlotFairness slot;
    // Gonderim cagrisi (ReadFile/WriteFile) icinde gecen toplam sure / (is parcacigi * profil suresi). 1'e yakin
    // deger gonderim CPU darbogazini gosterir (cihaz QD'yi gormez); gecikmeye dahil oldugundan Little bunu gostermez.
    // Bilgi amaclidir; olculmediyse bos (< 0).
    double submit_busy = -1.0;
    void set_latency(const LatencyHistogram& h);
    double mb_per_s() const { return seconds > 0 ? static_cast<double>(bytes) / (1024.0 * 1024.0) / seconds : 0.0; }
    double iops() const { return seconds > 0 ? static_cast<double>(ops) / seconds : 0.0; }
};

// xorshift64*: sikistirilamaz test verisi (sifir degil). Kriptografik degildir; yalniz veri desenidir.
void fill_random(void* data, std::size_t bytes, std::uint64_t& state);
// Ayni tampon tekrar yazilirken her 4 KiB sektorun ilk 8 byte'ina sayac damgasi: tekrar eden blok olmaz.
void stamp_sectors(void* data, std::size_t bytes, std::uint64_t counter);

// ---- EF-X22: sinirli yaz-oku-karsilastir (veri dogrulama) ----
// Yalniz uygulamanin kendi test dosyasi. Her blok adrese bagli desenle (core/measure/address_pattern.h, tohum +
// blok*kelime) yazilir; tum bloklar yazildiktan sonra her blok okunup karsilastirilir. Okuma oncesi tampon
// sifirlanir: tamponu doldurmayan "basarili" okuma esleme sayilmaz. Uyusmazlik veri koruma uyarisidir (puan yok),
// kesin ariza karari degildir; cihaz ici onbellek okumayi karsilamis olabilir (ortam saklama testi degildir).
// Iptal/sure blok aralarinda denetlenir; o ana kadar uyusmazlik bulunduysa durum Mismatch kalir.
inline constexpr std::uint32_t kVerifyBlockBytes = 1u << 20;  // 1 MiB: 512 B / 4 KiB sektor kati
class BlockDevice {
public:
    virtual ~BlockDevice() = default;
    virtual bool write_block(std::uint64_t offset, const void* data, std::uint32_t bytes) = 0;
    virtual bool read_block(std::uint64_t offset, void* data, std::uint32_t bytes) = 0;
    virtual std::uint32_t last_error() const = 0;
};
enum class VerifyStatus { NotRun, Ok, Mismatch, IoError, Cancelled, TimeLimit, Invalid };
struct VerifyResult {
    VerifyStatus status = VerifyStatus::NotRun;
    std::uint64_t blocks_planned = 0;
    std::uint64_t blocks_written = 0;
    std::uint64_t blocks_verified = 0;    // okundu ve eslesti
    std::uint64_t mismatched_blocks = 0;
    std::optional<std::uint64_t> first_mismatch_offset;  // ilk hatali 8 byte kelimenin dosya ofseti
    std::uint64_t bytes_written = 0;
    std::uint32_t io_error = 0;
};
// Uyumluluk bosluk 19: tek G/C icin sinirli bekleme. wait(ms) G/C o sure icinde bittiyse true doner.
// Ilk bekleme bitmezse cancel() bir kez cagrilir ve en fazla cancel_ms daha beklenir (INFINITE yok). Stuck:
// G/C iptalden sonra da bitmedi; cagiran OVERLAPPED/olay/tamponu serbest birakmamali (bilerek sizdirir).
enum class IoWaitOutcome { Completed, CancelledDone, Stuck };
IoWaitOutcome bounded_io_wait(const std::function<bool(std::uint32_t)>& wait, const std::function<void()>& cancel,
                              std::uint32_t first_ms, std::uint32_t cancel_ms);

// bytes % block == 0, block % 8 == 0, tamponlar block byte; degilse Invalid ve hic G/C yok.
VerifyResult run_verify(BlockDevice& dev, std::uint64_t bytes, std::uint32_t block, std::uint64_t seed,
                        std::uint64_t* write_buf, std::uint64_t* read_buf, const std::atomic<bool>& cancel,
                        const std::function<bool()>& time_up);

}  // namespace ef::benchmark::storage
