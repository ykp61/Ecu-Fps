#pragma once
// Denge testi (spec 2026-11-12 A1): "Arena" sahnesinin kare basi CPU isi (saf; D3D yok; birim testlenir).
//  - Is kare numarasiyla tohumlanir (zamanla degil): kare n'in durumu yalniz tohum ve n'e baglidir. Kare
//    basina simulasyon TAM BIR sabit adimdir; kamera kare numarasiyla ilerler.
//  - Simulasyon (isci is parcaciklari): boids benzeri suru (uyum, hizalanma, ayrilma), eksen cevresinde
//    donus, tunel duvari ve kare tohumlu kucuk sapma. Yalniz tamsayi / sabit noktali (Q16.16) aritmetik; kayan nokta ve /fp bagimliligi
//    yok. Her nesne onceki karenin durumunu okur, kendi yeni durumunu yazar (cift tampon): sonuc is
//    parcacigi sayisina, bolumlemeye ve zamanlamaya bagli DEGILDIR.
//  - Her test adimi yeni bir FrameSim ile kare 0'dan baslar; boylece ayni ayarli adimlar (S1/T1) ayni is
//    dizisini isler.
//  - Isci bolumleme sabit indeks araliklaridir (worker_range); oncelik / cekirdek atamasi bu modulde yoktur.
//  - Cizim is parcacigi: gorunurluk ayiklama (sabit noktali kesik piramit) + derinlik siralama (onden
//    arkaya, esitlikte indeks) + cizim butcesine kirpma. Seri calisir.
//  - Ilk kSimGoldenFrames karenin saglama toplami (durum + gorunur liste) C profili icin altin degerle
//    dogrulanir. Algoritma, sabitler veya profil degisirse kFrameSimVersion ve altin deger birlikte degisir;
//    eski sonuclarla karsilastirilmaz.

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ef::benchmark {

inline constexpr std::string_view kFrameSimVersion = "frame_sim_v1";

// Sabit nokta: Q16.16 (1 birim = 65536).
inline constexpr int kSimFixShift = 16;
inline constexpr std::int32_t kSimOne = 1 << kSimFixShift;
inline float sim_to_float(std::int32_t fixed) noexcept { return static_cast<float>(fixed) * (1.0f / 65536.0f); }

// Tunel: z ekseni boyunca, uzunlugu kTunnelLength olan dongu (z [0, kTunnelLength) icinde sarilir).
inline constexpr std::int32_t kTunnelLength = 512 * kSimOne;     // 2^25 (maske ile sarilir)
inline constexpr std::int32_t kTunnelRadius = 26 * kSimOne;      // yumusak duvar (x ve y icin ayri ayri)
inline constexpr std::int32_t kTunnelHardRadius = 30 * kSimOne;  // konum bu sinirin disina cikamaz

// Kamera: tunel ekseninde, +z yonune bakar; kare basina kCameraStep ilerler.
inline constexpr std::int32_t kCameraStep = kSimOne / 4;
inline constexpr std::int32_t kCameraNear = 2 * kSimOne;
inline constexpr std::int32_t kCameraFar = 320 * kSimOne;
inline constexpr std::int32_t kCameraTanYQ8 = 148;  // tan(dusey yarim aci) * 256 (~60 derece dusey gorus)
inline constexpr std::int32_t kCameraTanXQ8 = 263;  // 16:9 icin yatay
inline constexpr std::int32_t kCullRadius = 2 * kSimOne;  // nesne sinir kuresi

// Suru: ardisik kSchoolSize indeks bir gruptur (ortak yon ve hiz hedefi; komsular yalniz grup icinden).
inline constexpr std::uint32_t kSchoolSize = 64;

struct SimConfig {
    std::uint64_t seed = 0x17A5C0DEull;
    std::uint32_t objects = 20000;     // simule nesne
    std::uint32_t draw_budget = 4000;  // kare basina en cok cizim cagrisi (en yakin nesneler)
    std::uint32_t neighbors = 16;      // nesne basina okunan grup ici komsu (kare tohumlu secim)
};
// A1 profil C: 20 000 simule nesne, 4 000 cizim cagrisi.
SimConfig sim_profile_c() noexcept;
// Isci sayisi = min(mantiksal cekirdek - 1, 8), en az 1.
std::uint32_t sim_worker_count(std::uint32_t logical_cores) noexcept;

struct SimObject {
    std::int32_t x, y, z;     // konum (Q16.16)
    std::int32_t vx, vy, vz;  // hiz (Q16.16 / kare)
};

// Sabit indeks araligi: `workers` parcadan `worker`inci. Araliklar [0, objects) uzerinde bosluksuz ve
// ortusmesizdir; workers == 0 -> 1 sayilir; worker >= workers -> bos aralik.
struct IndexRange {
    std::uint32_t begin = 0, end = 0;
    std::uint32_t size() const noexcept { return end - begin; }
};
IndexRange worker_range(std::uint32_t objects, std::uint32_t workers, std::uint32_t worker) noexcept;

// Gorunur nesne anahtari: ust 32 bit kamera derinligi (Q16.16), alt 32 bit nesne indeksi.
inline std::uint32_t visible_index(std::uint64_t key) noexcept { return static_cast<std::uint32_t>(key); }
inline std::uint32_t visible_depth(std::uint64_t key) noexcept { return static_cast<std::uint32_t>(key >> 32); }

struct VisibleSet {
    std::vector<std::uint64_t> keys;  // derinlige gore artan; ilk `draws` oge cizilir
    std::uint32_t in_frustum = 0;     // kesik piramit icindeki nesne
    std::uint32_t draws = 0;          // min(in_frustum, draw_budget)
    std::span<const std::uint64_t> drawn() const noexcept { return {keys.data(), draws}; }
};

std::int32_t sim_camera_z(std::uint64_t frame) noexcept;  // yalniz kare indeksine bagli, [0, kTunnelLength)
// Nesne kamera kesik piramidinde mi; icindeyse `depth` kamera onundeki uzakliktir.
bool sim_visible(const SimObject& o, std::int32_t camera_z, std::uint32_t& depth) noexcept;

class FrameSim {
public:
    explicit FrameSim(const SimConfig& config);

    const SimConfig& config() const noexcept { return config_; }
    std::uint64_t frame() const noexcept { return frame_; }  // tamamlanan adim sayisi
    std::span<const SimObject> objects() const noexcept { return cur_; }

    // Siradaki karenin adimini `range` icin hesaplar: guncel durumu okur, yalniz kendi araligina yazar.
    // Ayrik araliklarla ayni anda farkli is parcaciklarindan cagrilabilir.
    void step_range(IndexRange range) noexcept;
    // Tum araliklar bittikten sonra TEK is parcacigindan: yeni durum gecerli olur, kare sayaci artar.
    void commit() noexcept;
    // Seri kolaylik: `partitions` sabit araligi sirayla isler ve commit eder.
    void step(std::uint32_t partitions = 1) noexcept;

    // Guncel durumda (frame() icin) ayiklama + derinlik siralama. `out` yeniden kullanilir (kare basi tahsis yok).
    void cull_and_sort(VisibleSet& out) const;

    std::uint64_t state_checksum() const noexcept;
    // Kare saglamasi: kare indeksi + durum + cizilen liste (sirasiyla) + kesik piramit sayisi.
    std::uint64_t frame_checksum(const VisibleSet& visible) const noexcept;

private:
    SimConfig config_;
    std::uint64_t frame_ = 0;
    std::vector<SimObject> cur_, next_;
    std::vector<std::int32_t> target_vz_;    // nesne basina sabit ileri hiz hedefi (grup + kisisel)
    std::vector<std::int32_t> home_radius_;  // nesnenin baslangictaki eksen uzakligi (yorunge yaricapi)
};

// Nesnenin sabit ozet degeri (renk / olcek secimi gibi cizim niteliklerini turetmek icin).
std::uint64_t sim_object_hash(std::uint64_t seed, std::uint32_t index) noexcept;

// Kare saglamalarini sirayla katlar (sim_prefix_checksum ile ayni katlama; baslangic 0). Isci havuzuyla
// hesaplanan ilk kareler ayni yolla katlanip altin degerle karsilastirilir.
std::uint64_t sim_checksum_fold(std::uint64_t acc, std::uint64_t frame_checksum) noexcept;
// Ilk `frames` karenin katlanmis saglamasi. `partitions` sonucu DEGISTIRMEZ (birim testi bunu dogrular).
std::uint64_t sim_prefix_checksum(const SimConfig& config, std::uint32_t frames, std::uint32_t partitions = 1);

// Altin deger: yalniz C profili (sim_profile_c) icin, ilk kSimGoldenFrames kare.
inline constexpr std::uint32_t kSimGoldenFrames = 8;
bool sim_golden_applies(const SimConfig& config) noexcept;
std::uint64_t sim_golden_checksum() noexcept;

}  // namespace ef::benchmark
