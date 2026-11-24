#pragma once
// BENCH-01/BENCH-07: deterministik prosedurel sahne (saf; birim testlenir).
//  - Ayni SceneConfig -> ayni ornek/isik listesi ve ayni kamera yolu (zaman degil kare indeksi).
//  - Is yuku surumu (kSceneVersion) ve tier sonuca yazilir; farkli surum/tier esdeger sayilmaz.
//  - scene_v3 (= v2 + aydinlatma gecisi kLightsPerDraw isiklik cizimlere bolundu; High ciktisi ve maliyeti
//    degisti, v2 High sonuclariyla karsilastirilmaz).
//  - scene_v2: ertelenmis aydinlatma (G-buffer) + tier'a gore cok sayida nokta isik + prosedurel doku
//    ornekleme + tam ekran son islem (ayrik bulaniklik/bloom + tonemap). GPU gercekten yuklenir.
//  - Hazirlik (uretim, yukleme) skor penceresine girmez; renderer bunu ayri olcer.
//  - Olcum iki kipte: kare sayisi (measure_s == 0, eski davranis) veya sure (warmup_s/measure_s).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace ef::benchmark {

inline constexpr std::string_view kSceneVersion = "scene_v3";

enum class Tier { Low, Medium, High };
std::string_view to_string(Tier t) noexcept;

// EF-X15: Uygulama Testi onizleme paneli icin kilitsiz kare sayaci (yalniz olcum penceresi). Panel 2-4 Hz okur;
// sahne zamanlamasi ve skor serisi degismez. Gecersiz aralik sayilmaz. Deger onizlemedir, sonuc degildir.
// Canli test ekrani (spec 2026-11-12 A4): ayni yapi genisletildi. Canli alanlar ISINMADA da beslenir (frames /
// sum_us yalniz olcum penceresinde artar; eski panel davranisi degismez). Cizim is parcacigi yazar, UI okur;
// alanlar ayri ayri atomiktir (anlik goruntu kovalar arasi tutarsiz olabilir; deger onizlemedir, sonuc degildir).
enum class TapPhase : std::uint32_t { Idle = 0, Prepare = 1, Warmup = 2, Measure = 3 };
inline constexpr std::size_t kTapRingSize = 32;      // kisa FPS halkasi (kova basina bir deger)
inline constexpr double kTapBucketMs = 250.0;        // kova suresi: canli sayilar ~4 Hz yenilenir
inline constexpr std::size_t kTapWindowBuckets = 4;  // kayan pencere = 4 kova = 1 sn ("1 sn FPS")

struct FrameTapLive {  // FrameTap::snapshot() ciktisi (duz veri)
    std::uint32_t step_index = 0, step_total = 0;  // 1'den; 0 = adim yok
    std::uint32_t step_code = 0;                   // cagiranin adim kodu (denge kosucusu: StepId + 1; 0 = yok)
    std::uint32_t width = 0, height = 0;           // adimin cizim cozunurlugu
    TapPhase phase = TapPhase::Idle;
    bool has_window = false;       // en az bir kova kapandi (asagidaki sayilar olculdu; degilse hepsi 0)
    double fps = 0;                // son 1 sn: kare / sure
    double cpu_ms = 0;             // son 1 sn: kare basi CPU suresi (simulasyon + ayiklama + kayit / gonderim)
    std::optional<double> gpu_ms;  // son 1 sn: kare basi GPU gecen suresi (zaman damgasi yoksa bos)
    double wait_share = 0;         // son 1 sn: GPU'yu bekleme / kare araligi, [0,1]
    std::array<float, kTapRingSize> fps_ring{};  // eskiden yeniye; ilk `ring_count` oge gecerli
    std::size_t ring_count = 0;
    std::uint64_t updates = 0;     // adim basindan beri kapanan kova
};

struct FrameTap {
    std::atomic<std::uint64_t> frames{0};
    std::atomic<std::uint64_t> sum_us{0};
    void add(double ms) noexcept {
        if (!(ms > 0.0) || !(ms < 60000.0)) return;
        sum_us.fetch_add(static_cast<std::uint64_t>(ms * 1000.0 + 0.5), std::memory_order_relaxed);
        frames.fetch_add(1, std::memory_order_relaxed);
    }
    // ---- Canli alanlar (A4) ----
    std::atomic<std::uint32_t> step_index{0}, step_total{0}, step_code{0}, width{0}, height{0};
    std::atomic<std::uint32_t> phase{0};  // TapPhase
    std::atomic<std::uint32_t> fps_milli{0}, cpu_us{0}, gpu_us{0}, gpu_valid{0}, wait_permille{0};
    std::array<std::atomic<std::uint32_t>, kTapRingSize> ring_milli{};  // kova FPS'i x 1000
    std::atomic<std::uint64_t> ring_pushes{0};  // adim basindan beri kova; yazma indeksi = pushes % kTapRingSize
    // Yeni adim: kimlik yazilir, canli sayilar ve halka sifirlanir, faz Prepare olur.
    void begin_step(std::uint32_t index, std::uint32_t total, std::uint32_t code, std::uint32_t w, std::uint32_t h) noexcept;
    // Canli sayilari ve halkayi sifirlar (adim kimligi kalir). Her olcum dongusu basinda cagrilir.
    void reset_live() noexcept;
    void set_resolution(std::uint32_t w, std::uint32_t h) noexcept;
    void set_phase(TapPhase p) noexcept { phase.store(static_cast<std::uint32_t>(p), std::memory_order_relaxed); }
    // Bir kova kapandi: 1 sn'lik pencere degerleri + kovanin FPS'i (halkaya).
    void publish(double fps, double cpu_ms, std::optional<double> gpu_ms, double wait_share, double bucket_fps) noexcept;
    FrameTapLive snapshot() const noexcept;
};

// Canli alanlarin kare basi beslemesi (cizim is parcacigi; saf). Saat okumaz: sure, kare araliklarinin toplamidir.
// Kova >= kTapBucketMs dolunca kapanir; pencere son kTapWindowBuckets kovadir. tap == nullptr -> hicbir sey yapmaz.
class TapFeeder {
public:
    explicit TapFeeder(FrameTap* tap) noexcept : tap_(tap) {}
    // interval_ms: onceki gonderimden bu gonderime (ilk karede <= 0 verilir; kare sayilmaz).
    void frame(double interval_ms, double cpu_ms, double wait_ms) noexcept;
    // Cozulen GPU zaman damgalari (toplam gecen sure, kare sayisi); isinma kareleri dahil.
    void gpu(double sum_ms, std::uint32_t n) noexcept;

private:
    struct Bucket {
        double span_ms = 0, cpu_ms = 0, wait_ms = 0, gpu_ms = 0;
        std::uint32_t frames = 0, gpu_n = 0;
    };
    FrameTap* tap_;
    Bucket cur_;
    std::array<Bucket, kTapWindowBuckets> window_{};
    std::size_t closed_ = 0;
};

// ---- Canli onizleme (A4): olcum hedefinden kucultulmus goruntu, GPU'dan bellege BEKLEMESIZ okunur ----
inline constexpr std::uint32_t kPreviewMaxWidth = 960, kPreviewMaxHeight = 540;
inline constexpr double kPreviewMinIntervalMs = 50.0;    // en fazla 20 Hz
inline constexpr std::uint32_t kPreviewSlots = 3;        // hazirlama (staging) halkasi
inline constexpr std::uint64_t kPreviewDelayFrames = 2;  // kopya en erken bu kadar kare sonra okunmaya calisilir
struct PreviewSize {
    std::uint32_t width = 0, height = 0;
};
// Hedef cozunurluk -> onizleme boyutu: en-boy orani korunur, 960x540 kutusuna sigdirilir, BUYUTULMEZ
// (320x180 adimi 320x180 iletilir; UI olcekler). 0 boyut -> {0, 0}.
PreviewSize preview_size(std::uint32_t width, std::uint32_t height) noexcept;

struct PreviewFrame {
    const std::uint8_t* pixels = nullptr;  // onceden carpilmis BGRA8 (alfa 255), ustten alta; YALNIZ geri cagri suresince gecerli
    std::uint32_t width = 0, height = 0;   // onizleme boyutu
    std::uint32_t stride = 0;              // satir basi bayt (>= width * 4)
    std::uint32_t source_width = 0, source_height = 0;  // olcum hedefi
    std::uint64_t frame = 0;               // adim icindeki kare indeksi (isinma dahil)
    bool measuring = false;                // kare olcum penceresinde mi cizildi
    bool final_frame = false;              // adim sonu karesi (olcum bittikten sonra okunur)
};
// Cizim is parcacigindan cagrilir; hizli donmeli (pikselleri kopyalayip cikmali), D3D cagirmamali, firlatmamali.
using PreviewCallback = std::function<void(const PreviewFrame&)>;
// Pikselleri satir dolgusu olmadan `out` icine kopyalar (width * height * 4 bayt). Bos / tutarsiz kare -> false.
bool copy_preview(const PreviewFrame& f, std::vector<std::uint8_t>& out);

// Onizleme halkasi defteri (saf): hangi karede kopya istenir, hangi yuva ne zaman okunmaya calisilir.
//  - Kopya: son kopyadan >= kPreviewMinIntervalMs gecmisse ve bos yuva varsa.
//  - Okuma: en eski bekleyen kopya, istendiginden en az kPreviewDelayFrames kare sonra denenir; GPU henuz
//    bitirmediyse (WAS_STILL_DRAWING) yuva bekler ve sonraki karede yeniden denenir. Olcum dongusu beklemez.
class PreviewRing {
public:
    struct Pending {
        std::uint32_t slot = 0;
        std::uint64_t frame = 0;
        bool measuring = false;
    };
    void reset() noexcept { *this = PreviewRing{}; }
    // Bu karede kopya istenmeli mi (now_ms: adim basindan beri gecen sure).
    bool should_issue(double now_ms) const noexcept;
    // Kopya istendi: kullanilacak yuva. Bos yuva yoksa (should_issue false iken cagrildiysa) nullopt.
    std::optional<std::uint32_t> issue(double now_ms, std::uint64_t frame, bool measuring) noexcept;
    // Okunmaya hazir en eski kopya (yoksa nullopt).
    std::optional<Pending> ready(std::uint64_t frame) const noexcept;
    void pop() noexcept;  // en eski kopya okundu (ya da dusuruldu)
    std::uint32_t pending() const noexcept { return count_; }

private:
    std::array<Pending, kPreviewSlots> q_{};
    std::uint32_t head_ = 0, count_ = 0, next_slot_ = 0;
    bool issued_ = false;
    double last_ms_ = 0;
};

// Arena v1 cizim kipi. Instanced: tek ornekli cizim. PerObject: nesne basina sabit tampon guncellemesi
// (Map WRITE_DISCARD) + orneklemesiz DrawIndexed (cizim is parcacigi yuku).
enum class DrawMode { Instanced, PerObject };
std::string_view to_string(DrawMode m) noexcept;

struct SceneConfig {
    std::uint64_t seed = 0x17A5C0DEull;
    Tier tier = Tier::Medium;
    std::uint32_t width = 1920, height = 1080;
    std::uint32_t frames = 600;          // kare kipi: olculen kare sayisi (measure_s == 0 iken)
    std::uint32_t warmup_frames = 0;     // kare kipi: ayni cihazda, olcumden once atilan kare
    double warmup_s = 0;                 // sure kipi: ayni cihazda isinma (saniye)
    double measure_s = 0;                // > 0: sure kipi; olcum penceresi (saniye)
    std::uint32_t frames_in_flight = 3;  // GPU kuyrugunda en cok kare (1..6); kare basi bekleme yok
    std::uint32_t max_frames = 1000000;  // sure kipinde toplam kare ust siniri (bellek korumasi)
    FrameTap* frame_tap = nullptr;  // EF-X15: nullptr = panel yok (varsayilan); plan/parmak izine girmez
    // A4: canli onizleme geri cagrisi. nullptr = onizleme uretilmez (varsayilan); plan/parmak izine girmez.
    // Acik/kapali durumu sonuca yazilir (SceneRunResult.preview); farkli durumlar karsilastirilmaz.
    const PreviewCallback* preview = nullptr;
    // ---- Arena v1 (denge testi, spec 2026-11-12 A1; ayrinti arena.h). Hepsi varsayilan KAPALI: `arena` false
    // iken asagidaki alanlar okunmaz; scene_v3 is yuku, hash'i ve sonucu degismez. ----
    bool arena = false;                         // Arena sahnesi (cift ok prizmasi, tunel); tier kullanilmaz
    bool sim = false;                           // kare basi nesne simulasyonu (isci havuzu) + ayiklama + derinlik siralama
    std::uint32_t sim_objects = 0;              // sahnedeki nesne (sim aciksa her kare simule edilir)
    std::uint32_t sim_workers = 0;              // simulasyon isci is parcacigi (0 -> 1)
    DrawMode draw_mode = DrawMode::Instanced;   // PerObject: nesne basina sabit tampon + cizim cagrisi
    std::uint32_t object_draws = 0;             // kare basina en cok cizilen nesne (PerObject: cizim cagrisi)
    std::uint32_t arena_lights = 0;             // piksel basi isik sayisi
    bool arena_bloom = false;                   // parlama gecisleri (scene_v3'te her zaman vardir; bu alan yalniz Arena)
};

struct Instance {
    std::array<float, 3> pos;
    float scale;
    std::array<float, 4> color;
    float spin;  // rad/kare
};

// Nokta isik: xyz konum, w = 1/yaricap^2 (zayiflama); renk rgb, a kullanilmaz.
struct PointLight {
    std::array<float, 4> pos_inv_r2;
    std::array<float, 4> color;
};

// Tier maliyet parametreleri: ornek sayisi (geometri), isik sayisi (piksel basi dongu), bulaniklik yaricapi.
struct TierParams {
    std::uint32_t instances;
    std::uint32_t lights;
    std::uint32_t blur_radius;
};
TierParams tier_params(Tier t) noexcept;
std::uint32_t instance_count(Tier t) noexcept;  // Low 2k, Medium 20k, High 80k
std::uint32_t light_count(Tier t) noexcept;
std::vector<Instance> generate_instances(const SceneConfig& c);
// Ayni tohumla daha yuksek tier listesinin ilk N ogesi alt tier listesine esittir (kalibrasyon tek cihazla).
std::vector<PointLight> generate_lights(const SceneConfig& c);

struct Camera {
    std::array<float, 3> eye, target;
};
inline constexpr std::uint32_t kOrbitFrames = 1200;  // sure kipinde kamera turu (kare)
// Tur periyodu: sure kipinde kOrbitFrames, kare kipinde max(1, frames).
std::uint32_t orbit_period(const SceneConfig& c) noexcept;
Camera camera_at(const SceneConfig& c, std::uint32_t frame) noexcept;  // yalniz kare indeksine bagli

// TDR guvenligi: aydinlatma gecisi en cok kLightsPerDraw isiklik parcalara bolunur (toplamsal karisim);
// tek cizim komutu hicbir tier'da asiri uzun surmez. Isiksiz sahne de taban gecisi icin 1 cizim yapar.
inline constexpr std::uint32_t kLightsPerDraw = 512;
std::uint32_t light_draws(std::uint32_t lights) noexcept;

// Uretilen is yukunun ozeti (ayni config -> ayni deger). FNV-1a 64 ham bitler uzerinden.
// `arena` aciksa Arena v1 kimligi (arena_workload_hash; `v` kullanilmaz); kapaliyken scene_v3 degeri degismez.
std::uint64_t workload_hash(const SceneConfig& c, const std::vector<Instance>& v) noexcept;

// ---- BENCH-07 olcum yardimcilari (saf) ----

// Sure kipi mi (measure_s > 0)?
bool duration_mode(const SceneConfig& c) noexcept;

enum class ScenePhase { Warmup, Measure, Done };
// `frame` (0'dan, isinma dahil) gonderilmeden once, dongu basindan bu yana gecen `elapsed_s` ile karar.
// Kare kipi: [0, warmup_frames) isinma, sonra `frames` kare olcum. Sure kipi: [0, warmup_s) isinma,
// [warmup_s, warmup_s + measure_s) olcum; max_frames asilirsa Done.
ScenePhase scene_phase(const SceneConfig& c, std::uint64_t frame, double elapsed_s) noexcept;

// Kalibrasyon: her tier icin olculen ortalama kare suresi (<= 0: olculmedi/olcum yok). Hedef butceyi
// (max_frame_ms) asmayan en yuksek tier secilir; hicbiri sigmazsa Low (zayif iGPU yine kosar).
Tier pick_scene_tier(const std::array<double, 3>& avg_frame_ms, double max_frame_ms) noexcept;

// GPU zaman damgasi halkasi (saf defter). D3D11 sorgulari kare sonunda okunmaz; birkac kare sonra
// bloklamadan okunur. Yalniz olcum penceresindeki karelerin gecerli (disjoint olmayan) araliklari sayilir.
// Iki seri uretir:
//  - elapsed_ms: kare basina ts1 - ts0 (GPU'nun karenin ilk ve son komutu arasindaki gecen sure). Bu
//    sure baska sureclerin GPU'da araya giren isini da icerir; KULLANIM DEGILDIR. Kullanim yalniz PDH
//    surec basi motor sayaclarindan (GPU Engine pid_*_engtype_3D) okunur.
//  - pacing_ms: ardisik gecerli karelerin bitis zaman damgalari (ts1) arasi fark = GPU tamamlama
//    araligi. Onceki kare gecersiz/atlanmis ya da frekans farkliysa zincir kopar (o aralik yazilmaz).
//    Olcumdeki ilk karenin araligi son isinma karesine gore hesaplanir (gecerliyse).
class GpuQueryRing {
public:
    explicit GpuQueryRing(std::uint32_t slots)
        : slots_(slots ? slots : 1), measured_(slots_, false), gap_before_(slots_, false) {}
    std::uint32_t slots() const noexcept { return slots_; }
    std::uint32_t pending() const noexcept { return static_cast<std::uint32_t>(tail_ - head_); }
    bool full() const noexcept { return pending() >= slots_; }
    // Yeni kare icin yuva; halka doluysa false (once en eski yuva cozulmeli).
    bool issue(bool measured, std::uint32_t& slot) noexcept;
    // Yuva alamadan gonderilen kare: olculuyorsa dislanir, tamamlama zinciri bir sonraki karede kopar.
    void note_unissued(bool measured) noexcept;
    std::uint32_t oldest_slot() const noexcept { return static_cast<std::uint32_t>(head_ % slots_); }
    // En eski bekleyen yuvanin sonucu. Gecersiz (disjoint/frekans yok/okunamadi/ters sira) -> dislanir.
    void resolve_oldest(bool disjoint, std::uint64_t freq, bool read_ok, std::uint64_t a, std::uint64_t b);
    const std::vector<double>& elapsed_ms() const noexcept { return elapsed_ms_; }
    const std::vector<double>& pacing_ms() const noexcept { return pacing_ms_; }
    std::uint32_t excluded() const noexcept { return excluded_; }  // yalniz olculen kareler
    // Canli gosterge (A4): son cagridan beri cozulen GECERLI karelerin (isinma dahil) toplam gecen suresi ve
    // sayisi; okununca sifirlanir. Skor serilerini etkilemez.
    void take_live(double& sum_ms, std::uint32_t& n) noexcept {
        sum_ms = live_sum_ms_;
        n = live_n_;
        live_sum_ms_ = 0;
        live_n_ = 0;
    }

private:
    std::uint32_t slots_;
    std::uint64_t head_ = 0, tail_ = 0;
    std::vector<bool> measured_, gap_before_;
    bool gap_pending_ = false;
    bool have_prev_end_ = false;
    std::uint64_t prev_end_ = 0, prev_freq_ = 0;
    std::vector<double> elapsed_ms_, pacing_ms_;
    std::uint32_t excluded_ = 0;
    double live_sum_ms_ = 0;
    std::uint32_t live_n_ = 0;
};

// Sure dizisi ozeti (en yakin sira yuzdelik). Bos dizi -> n = 0 (sahte deger yok).
struct DurationStats {
    std::size_t n = 0;
    double avg_ms = 0, p50_ms = 0, p99_ms = 0, max_ms = 0, sum_ms = 0;
};
DurationStats summarize_ms(const std::vector<double>& ms);

// Benchmark sonucu (FPS / %1 dusuk) hangi kare serisinden?
//  GpuCompletion: GPU tamamlama araliklari (pacing_ms). Yalniz zaman damgasi var, olculen hicbir kare
//                 dislanmamis (gpu_excluded == 0) VE pacing gonderim araliklarinin en az %99,5'ini
//                 kapsiyorsa. Gecersiz kare iki araligi birden dusurur ve gecersizlik cogu zaman takilmayla
//                 ayni anda olur; %1 dusuk metriginden buyuk bir bosluk yavas kareleri gizleyebilir.
//  SubmitCadence: gonderimden gonderime sure (yedek; sonucta bu bayrakla isaretlenir).
// Not: her karenin kendi TIMESTAMP_DISJOINT parantezi vardir; zincir yalniz ayni frekans ve ileri giden
// bitis damgasiyla kurulur. Iki parantez arasindaki ileri yonlu bir saat sicramasi tespit edilemez (nadir).
enum class FrameSource { GpuCompletion, SubmitCadence };
std::string_view to_string(FrameSource s) noexcept;
// Kapsam = pacing araligi sayisi / gonderim araligi sayisi, [0,1]. submit_n == 0 -> nullopt (bilinmiyor).
std::optional<double> pacing_coverage(std::size_t pacing_n, std::size_t submit_n) noexcept;
FrameSource choose_frame_source(bool timestamps, std::size_t pacing_n, std::size_t submit_n, std::uint32_t excluded) noexcept;

// Kuyruk doygunlugu: ort. GPU gecen sure / ort. GPU tamamlama araligi, [0,1]'e kirpilir. ~1: kendi
// karelerimiz GPU'da arka arkaya (bizden kaynakli bosluk yok). Kullanim degildir (gecen sure baska
// sureclerin isini da icerir). Iki seriden biri bossa veya aralik 0 ise bilinmiyor (nullopt).
std::optional<double> queue_saturation(const DurationStats& elapsed, const DurationStats& pacing) noexcept;

}  // namespace ef::benchmark
