#pragma once
// BENCH-01/BENCH-07: donanim D3D11 ekran disi renderer. WARP/Microsoft Basic Render/uzak/yazilim adaptoru
// fiziksel GPU benchmark'i olarak puanlanmaz (renderer_kind ayri yazilir). Hazirlik suresi skor penceresinden ayri.
// BENCH-07:
//  - Cihaz run_scene cagrisi basina BIR kez kurulur; isinma ve olcum ayni cihazda.
//  - Kare basi GPU beklemesi yok: en cok `frames_in_flight` kare kuyrukta (ID3D11Fence varsa cekirdek
//    beklemesi, yoksa olay sorgusu). GPU zaman damgalari halka ile birkac kare sonra bloklamadan okunur.
//  - frame_ms: GONDERIM ARALIGI (ardisik kare gonderimleri arasi CPU suresi; geri basinc nedeniyle GPU
//    dolu iken GPU verimini izler ama GPU'nun kareyi bitirdigi ani degildir).
//  - gpu_pacing_ms: GPU TAMAMLAMA ARALIGI (ardisik gecerli karelerin bitis zaman damgasi farki). Zaman
//    damgalari gecerli, dislanan kare yok ve kapsam >= %99,5 ise benchmark FPS / %1 dusuk bu seriden
//    gelir (frame_source, gpu_pacing_coverage); degilse frame_ms.
//  - gpu_elapsed_ms: kare basina GPU'da ilk ve son komut arasi gecen sure. Baska sureclerin araya giren
//    GPU isini da icerir; kullanim/mesguliyet DEGILDIR. GPU kullanimi yalniz PDH surec basi motor
//    sayaclarindan (GPU Engine pid_*_engtype_3D) okunur.
//  - cpu_submit_ms: yalniz komut kaydi + gonderim.
//  - Aydinlatma gecisi en cok kLightsPerDraw isiklik cizimlere bolunur (TDR guvenligi, toplamsal karisim).
//  - Son karenin yerine sabit dogrulama karesi (kare 0) cizilir ve 16x16 geri okunur: piksel ozeti
//    sure kipinde de deterministiktir.
// Denge testi "Arena v1" (spec 2026-11-12 A1; SceneConfig.arena, ayrinti arena.h):
//  - Ayni cihaz, kuyruk, cit ve zaman damgasi zinciri. Kare: [GPU'yu bekleme] -> simulasyon (isci havuzu) ->
//    ayiklama + siralama -> kayit + gonderim. Dort sure kare basina ayri yazilir (wait_ms, sim_ms, cull_ms,
//    submit_ms) ve GPU zaman damgasi serileriyle ayni sonucta doner.
//  - ArenaSession: tek cihazda adim adim kosu; cozunurluk degisince yalniz hedefler yeniden kurulur
//    (rebuild_targets) ve once video bellegi butcesi denetlenir.
// Canli onizleme (spec 2026-11-12 A4; SceneConfig.preview, iki sahnede de):
//  - En fazla 20 Hz: karenin sonunda LDR hedefi 960x540 kutusuna kucultulur (BGRA) ve uc dokuluk hazirlama
//    halkasinin bir yuvasina kopyalanir. Gecis karenin zaman damgasi parantezi ICINDEDIR: kare zincirden
//    cikarilmaz, GPU suresi onizleme isini de icerir.
//  - Okuma en az iki kare sonra D3D11_MAP_FLAG_DO_NOT_WAIT ile denenir; GPU bitirmediyse sonraki karede yeniden.
//    Olcum dongusu onizleme icin GPU'yu BEKLEMEZ. Okuma + geri cagri suresi cizim suresine yazilmaz (preview_ms).
//  - Adim sonunda (olcum bittikten sonra) son kare bir kez bekleyerek okunur (final_frame).
//  - Canli sayilar (FrameTap canli alanlari) isinmada da beslenir; yeni saat okumasi eklenmez.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "benchmark/scene/scene.h"

namespace ef::benchmark {

enum class RendererKind { Hardware, Software, None };
std::string_view to_string(RendererKind k) noexcept;

struct AdapterInfo {
    std::wstring name;
    std::uint32_t vendor_id = 0, device_id = 0;
    std::uint64_t dedicated_vram = 0;
    std::uint32_t feature_level = 0;  // 0xb000 = 11_0
    RendererKind kind = RendererKind::None;
    // Bagdastirici kimligi "hhhhhhhh:llllllll" (kucuk harf onaltilik; PDH GPU sayac orneklerindeki LUID ile ayni
    // bicim). Yalniz donanim bagdastiricisinda dolu; WARP'ta bos. Oturum ici kimliktir, kaydedilmez.
    std::string luid;
    // D3D11_FEATURE_THREADING (surucu es zamanli kaynak olusturma / komut listesi); okunamadiysa bos.
    std::optional<bool> driver_concurrent_creates, driver_command_lists;
};

struct SceneRunResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;                   // olcum penceresinde kare yoksa "no_measured_frames"
    AdapterInfo adapter;
    std::uint64_t workload_hash = 0;
    double prepare_ms = 0;               // cihaz + kaynak + shader (skor disi)
    std::vector<double> frame_ms;        // gonderim araligi (gonderimden gonderime, CPU saati)
    std::uint64_t pixel_checksum = 0;    // dogrulama karesinin kucuk geri okumasi (gercekten cizildi mi)
    // BENCH-03: GPU timestamp (yalniz gecerli kareler). Disjoint/frekans degisimi olan kare skora girmez.
    std::vector<double> gpu_elapsed_ms;  // kare basi ts1 - ts0 (baska sureclerin isi dahil; kullanim degil)
    std::uint32_t gpu_excluded = 0;      // gecersiz zaman damgali olculen kare
    std::vector<double> gpu_pacing_ms;   // GPU tamamlama araligi (ardisik gecerli ts1 farki)
    // BENCH-07
    Tier tier = Tier::Medium;
    std::uint32_t width = 0, height = 0, lights = 0, instances = 0;
    std::uint32_t light_draws = 0;       // aydinlatma gecisinin cizim sayisi (kare basina)
    std::uint32_t frames_in_flight = 0;
    bool gpu_timestamps = false;         // zaman damgasi sorgusu kurulabildi mi
    bool fence_wait = false;             // ID3D11Fence ile bekleme (yoksa olay sorgusu yoklamasi)
    std::uint32_t warmup_frames = 0;     // atilan kare
    std::uint32_t measured_frames = 0;   // olcum penceresinde gonderilen kare
    double warmup_s = 0, measure_s = 0;  // gerceklesen pencere sureleri (duvar saati)
    std::vector<double> cpu_submit_ms;   // olculen karelerin CPU kayit+gonderim suresi
    DurationStats gpu_elapsed;           // gpu_elapsed_ms ozeti
    DurationStats gpu_pacing;            // gpu_pacing_ms ozeti
    DurationStats cpu_submit;            // cpu_submit_ms ozeti
    std::optional<double> queue_saturation;  // [0,1]; kullanim degil (bkz. scene.h); bilinmiyorsa nullopt
    FrameSource frame_source = FrameSource::SubmitCadence;  // FPS / %1 dusuk kaynagi
    std::optional<double> gpu_pacing_coverage;  // gpu_pacing_ms / frame_ms sayisi; zaman damgasi yoksa nullopt
    std::chrono::steady_clock::time_point measure_begin{}, measure_end{};  // dis ornekleyici icin
    // Kare basi GPU'yu bekleme (olculen kareler; cpu_submit_ms ile ayni sira): kuyruk geri basinci
    // (frames_in_flight) + zaman damgasi okuma. CPU sinirliyken ~0; GPU sinirliyken kare araligina yaklasir.
    std::vector<double> wait_ms;
    DurationStats wait;
    // ---- Arena v1 (denge testi). `arena` false iken asagidaki alanlar bos / 0 kalir. ----
    bool arena = false;
    bool sim = false;                             // kare basi simulasyon + ayiklama calisti mi
    std::uint32_t sim_workers = 0;                // isci is parcacigi (sim kapaliysa 0)
    DrawMode draw_mode = DrawMode::Instanced;
    std::uint32_t object_draws_min = 0, object_draws_max = 0;  // olculen karelerde cizilen nesne (PerObject: cizim cagrisi)
    // Isci havuzuyla hesaplanan ilk kSimGoldenFrames karenin saglamasi beklenen degere (C profilinde altin
    // deger, degilse seri hesap) esit mi; sim kapaliysa nullopt. Esit degilse adim "sim_checksum" ile biter.
    std::optional<bool> sim_verified;
    std::uint64_t sim_checksum = 0;
    // Kare basi CPU sureleri (olculen kareler; ayni sira). submit_ms = cpu_submit_ms: Arena'da nesne basina
    // sabit tampon + cizim cagrilari + Flush (cizim is parcacigi).
    std::vector<double> sim_ms;   // isci havuzunda simulasyon adimi (fork-join duvar suresi); sim kapaliysa bos
    std::vector<double> cull_ms;  // cizim is parcacigi: ayiklama + derinlik siralama; sim kapaliysa bos
    const std::vector<double>& submit_ms() const noexcept { return cpu_submit_ms; }
    DurationStats sim_step, cull;  // sim_ms / cull_ms ozeti
    // ---- Canli onizleme (A4). Kosul olarak kaydedilir: onizleme acik ve kapali kosular karsilastirilmaz. ----
    bool preview = false;              // SceneConfig.preview verildi ve onizleme hedefleri kurulabildi
    std::uint32_t preview_frames = 0;  // iletilen onizleme karesi (adim sonu karesi dahil)
    double preview_ms = 0;             // olcum penceresinde onizleme okuma + geri cagri toplam suresi (cizim is parcacigi)
};

// Benchmark FPS / %1 dusuk icin kullanilacak seri: frame_source == GpuCompletion ise gpu_pacing_ms,
// degilse frame_ms (gonderim araligi; sonuc frame_source ile isaretli).
const std::vector<double>& scored_frame_ms(const SceneRunResult& r) noexcept;

// Birden cok sahne kosusunun skor serisi (birlesik yuk testi sahneyi tekrarlar). Kaynaklar karistirilmaz:
// tum kosular GpuCompletion ise pacing serileri birlestirilir; biri bile degilse TUM kosularin gonderim
// araligi kullanilir ve sonuc SubmitCadence olarak isaretlenir. Kosu yoksa seri bos.
class ScoredFrames {
public:
    void add(const SceneRunResult& r);
    const std::vector<double>& frame_ms() const noexcept { return all_gpu_ ? pacing_ : submit_; }
    FrameSource source() const noexcept { return all_gpu_ && runs_ ? FrameSource::GpuCompletion : FrameSource::SubmitCadence; }
    std::size_t runs() const noexcept { return runs_; }
    std::uint32_t gpu_excluded() const noexcept { return excluded_; }

private:
    std::vector<double> pacing_, submit_;
    bool all_gpu_ = true;
    std::size_t runs_ = 0;
    std::uint32_t excluded_ = 0;
};

// BENCH-03: kendi surecimizin VRAM butcesi altinda asamali doku yukleme. Butce kartin fiziksel kapasitesi
// degildir ve dis oyunun kullanimi sanilmaz (MEAS-03). Butce okunamazsa unsupported (sahte 0 yok).
struct VramStep {
    std::uint64_t allocated = 0, usage = 0, budget = 0;
    double upload_ms = 0;
};
struct VramResult {
    bool supported = false;
    bool stopped_at_budget = false;
    bool cancelled = false;
    std::string error;
    std::uint64_t budget_start = 0;
    std::vector<VramStep> steps;
};
// Plan: butcenin `fraction`i ve `cap` icinde kac `step` baytlik adim (saf, birim testlenir).
bool gpu_timestamp_valid(bool disjoint, std::uint64_t freq, bool read_ok, std::uint64_t a, std::uint64_t b) noexcept;
std::uint32_t vram_steps_allowed(std::uint64_t budget, std::uint64_t usage, std::uint64_t step, double fraction, std::uint64_t cap) noexcept;
VramResult run_vram(std::uint64_t step_bytes, double fraction, std::uint64_t cap, const std::atomic<bool>& cancel);

// Uyumluluk B9 (bosluk 24): create_device'in tek bagdastirici secim yolu. Yuksek performans sirasindaki listede
// `software[i]` = i. oge yazilim bagdastiricisi mi. n. (0 tabanli) donanim bagdastiricisinin liste konumu; yoksa -1
// (saf; create_device bunu "adapter_index" hatasina cevirir, WARP'a dusmez).
int nth_hardware_adapter(std::span<const bool> software, int n) noexcept;
// Bu bilgisayarin donanim bagdastiricilari (yuksek performans sirasi; salt okuma DXGI listesi). Yalniz deneme araci
// (ef_arena_probe --list-adapters) kullanir; urun bagdastirici secmez.
std::vector<std::wstring> hardware_adapter_names();

// prefer_hardware=false yalniz test icin WARP zorlar (sonuc Software olarak isaretlenir).
// `c.arena` aciksa tek adimlik bir ArenaSession acar (bkz. asagi).
SceneRunResult run_scene(const SceneConfig& c, const std::atomic<bool>& cancel, bool prefer_hardware = true);

// Uyumluluk B14: uzun test (isinma) icin scene_v3 oturumu. Cihaz + sahne kaynaklari open() ile BIR kez kurulur
// (hazirlik olcum disi); run_slice() ayni cihazda art arda cagrilir. Dilim basina cihaz kurma, bosta bekleme veya
// piksel geri okuma yoktur; yalniz dilim sonunda kuyruktaki son kare tamamlanir (zaman damgalari okunur).
//  - Her dilim kare 0'dan baslar: her saniye ayni is dizisi cizilir (dilimler arasi esdeger yuk).
//  - Cozunurluk ve tier open() ile ayni olmali ("bad_config"). Cihaz kaybinda error "device_removed".
class SceneSession {
public:
    SceneSession();
    ~SceneSession();
    SceneSession(const SceneSession&) = delete;
    SceneSession& operator=(const SceneSession&) = delete;

    bool open(const SceneConfig& c, bool prefer_hardware = true);
    const std::string& error() const noexcept;
    const AdapterInfo& adapter() const noexcept;
    // Sure kipi dilim (c.measure_s > 0, isinmasiz): measure_begin/measure_end gercek cizim penceresidir.
    SceneRunResult run_slice(const SceneConfig& c, const std::atomic<bool>& cancel);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Denge testi "Arena v1": tek cihaz uzerinde adim adim kosu. Cihaz, ag, golgelendiriciler, isik listesi ve
// isci havuzu adimlar arasinda yasar; cozunurluk degisirse yalniz hedef kumesi yeniden kurulur.
//  - Her adim yeni bir simulasyonla kare 0'dan baslar (ayni ayarli adimlar ayni is dizisini isler).
//  - Olcumden once adimin tum gecisleri bir kez cizilir (kare 0; pixel_checksum bu karenin ozetidir).
//  - Olcum ekran disidir (Flush, Present yok); zaman damgasi zinciri run_scene ile aynidir.
//  - WARP ile acilan oturum Software olarak isaretlenir ve puanlanmaz.
class ArenaSession {
public:
    ArenaSession();
    ~ArenaSession();
    ArenaSession(const ArenaSession&) = delete;
    ArenaSession& operator=(const ArenaSession&) = delete;

    // Cihaz + cozunurlukten bagimsiz kaynaklar. `base.seed` oturum boyunca sabittir. false -> error().
    // hardware_index >= 0 (denge kosucusu create_device secimi): yuksek performans sirasindaki o donanim bagdastiricisi; yoksa false ve error
    // "adapter_index" (WARP'a dusulmez). -1 = urun secimi (ilk donanim bagdastiricisi, yoksa prefer_hardware'a gore WARP).
    bool open(const SceneConfig& base, bool prefer_hardware = true, int hardware_index = -1);
    const std::string& error() const noexcept;
    const AdapterInfo& adapter() const noexcept;

    // Bir adim (isinma + olcum). `c.arena` acik ve tohum oturumla ayni olmali ("bad_config").
    // Hedefler kurulamazsa ok=false ve error "vram_budget" (butce yetmiyor) ya da "target_create";
    // oturum sonraki adim icin kullanilabilir kalir (butce asimi GPU siniri hukmu degildir). Hedefler cihaz
    // kaybi yuzunden kurulamadiysa error "device_removed" olur (bu cozunurluge ozgu degil; oturum biter).
    SceneRunResult run_step(const SceneConfig& c, const std::atomic<bool>& cancel);

    // Son cizilen karenin tamami (LDR hedefi), RGBA8, satir satir, width * height * 4 bayt. GPU'yu bekleyerek
    // okur: yalniz dogrulama / deneme icindir, olcum dongusunde cagrilmaz.
    bool read_frame(std::vector<std::uint8_t>& rgba, std::uint32_t& width, std::uint32_t& height);

    // Butce ust siniri (bayt; 0 = yok): denetimde butce = min(okunan, sinir). Okunan butce yoksa sinir kullanilir.
    void limit_vram_budget(std::uint64_t bytes) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// BENCH-07: gercek agir boru hattinda kisa kalibrasyon. Tek cihaz; Low -> Medium -> High sirayla kosar,
// butceyi asan ilk tier'dan sonra durur. Secilen tier A/B icin kilitlenir (cagiran SceneConfig.tier'a yazar).
struct CalibrationOptions {
    double warmup_s = 0.3;       // tier basina isinma
    double measure_s = 0.6;      // tier basina olcum
    double max_frame_ms = 33.0;  // >= ~30 FPS: olcum penceresi yeterli kare toplar
};
struct SceneCalibration {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    AdapterInfo adapter;
    std::array<double, 3> avg_frame_ms{};        // Low/Medium/High gonderim araligi; 0 = olculmedi
    std::array<double, 3> avg_gpu_pacing_ms{};   // GPU tamamlama araligi ortalamasi; 0 = yok/yetersiz
    std::array<double, 3> avg_gpu_elapsed_ms{};  // gecerli zaman damgasi gecen sure ortalamasi; 0 = yok
    // Tier secimi: GPU tamamlama araligi gecerliyse (choose_frame_source; dislanan kare yok, kapsam
    // >= %99,5) o, degilse gonderim araligi.
    Tier tier = Tier::Low;
    double seconds = 0;
};
SceneCalibration calibrate_scene(const SceneConfig& base, const CalibrationOptions& o, const std::atomic<bool>& cancel,
                                 bool prefer_hardware = true);

}  // namespace ef::benchmark
