#pragma once
// Denge testi (spec 2026-11-12 A1): "Arena v1" sahnesinin saf kismi (D3D yok; birim testlenir).
//  - Nesne: urunun cift ok ambleminden kodla uretilen ekstruzyon prizmasi (harici model / doku yok).
//  - Tunel icinde ucan nesneler; renkler marka paletinden (design/tokens.json).
//  - GPU yuku piksel sayisiyla olceklenir: piksel basi isik dongusu, parlama gecisleri ve her pikseli
//    kaplayan tunel (tam ekran arka plan). Ucgen / nesne sayisi cozunurlukler arasinda sabittir.
//  - CPU yuku iki parcadir ve ayri raporlanir: isci havuzunda simulasyon (frame_sim) + cizim is
//    parcaciginda ayiklama, derinlik siralama, nesne basina sabit tampon ve orneklemesiz cizim (PerObject).
//  - Profiller sabittir ve kimlige (arena_workload_hash) girer. Degerler degisirse kArenaVersion artar;
//    eski sonuclarla karsilastirilmaz.
//  - Sim kapaliyken (Cmin) CPU isi yoktur: kare 0 cizim listesi bir kez kurulur, nesnelerin ilerlemesi
//    kose golgelendiricisinde kare indeksinden tamsayi aritmetigiyle hesaplanir (ayni nesne sayisi, tek cizim).

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "benchmark/scene/frame_sim.h"
#include "benchmark/scene/scene.h"

namespace ef::benchmark {

inline constexpr std::string_view kArenaVersion = "arena_v1";

// ---- Amblem geometrisi ----
// assets/benchmark/arena_v1-chevrons.svg yol verisi (SVG birimi, y asagi; marka 1.0.0 cift ok amblemi). Marka
// 2.0.0'a (EF) gecti; arena_v1 olcum sahnesi karsilastirilabilir kalsin diye bu geometri dondurulmustur. Birim testi
// bu tabloyu dosyadaki `d` degerleriyle karsilastirir.
struct MarkPoint {
    float x, y;
};
inline constexpr std::array<MarkPoint, 9> kMarkChevronA{
    {{52, 22}, {170, 22}, {270, 124}, {120, 298}, {0, 298}, {126, 164}, {217, 154}, {234, 134}, {139, 134}}};
inline constexpr std::array<MarkPoint, 6> kMarkChevronB{{{240, 0}, {369, 0}, {460, 124}, {341, 261}, {187, 261}, {321, 111}}};
inline constexpr float kMarkWidth = 460.f, kMarkHeight = 298.f;  // iki yolun ortak sinir kutusu

// Basit cokgeni (kendini kesmeyen; yonu fark etmez) ucgenlere boler: kulak kesme. Donen indeksler `pts`
// icindir ve her ucgen saat yonunun tersinedir (x saga, y yukari). n < 3 -> bos.
std::vector<std::uint16_t> triangulate_polygon(std::span<const MarkPoint> pts);

struct ArenaVertex {
    std::array<float, 3> pos, normal;
};
struct ArenaMesh {
    std::vector<ArenaVertex> vertices;
    std::vector<std::uint16_t> indices;  // ucgen listesi; on yuz: cross(v1 - v0, v2 - v0) normal yonunde
};
inline constexpr float kChevronHalfDepth = 0.16f;  // ekstruzyon yari kalinligi (ag birimi)
// Cift ok prizmasi: amblem merkezi orijinde, en uzun yari kenar 1 birim (x), y yukari, ekstruzyon z ekseninde.
// Ok +x yonunu gosterir. Duz golgeleme icin her yuz kendi koselerini tasir.
ArenaMesh arena_chevron_mesh();
// Agin sinir kuresi yaricapi (olcek 1 icin); nesne olcegi bununla kCullRadius icinde kalir.
float arena_mesh_radius(const ArenaMesh& m) noexcept;

// ---- Palet (design/tokens.json `colors` + `benchmark_scene_colors`; 0xRRGGBB sRGB). Birim testi dosyayla
// karsilastirir. Sis / duvar / beyaz isik sahneye sabitlenmistir (benchmark_scene_colors): arayuz paleti degisince
// (02 // BOOST) olcum sahnesi ve kosullar arasi karsilastirilabilirlik degismez (kArenaVersion ayni kalir).
struct ArenaColor {
    std::string_view token;
    std::uint32_t rgb;
};
inline constexpr std::array<ArenaColor, 4> kArenaObjectColors{
    {{"accent", 0x3DFF7A}, {"cyan", 0x00D4FF}, {"violet", 0xA64DFF}, {"orange", 0xFF8A1F}}};
inline constexpr std::array<ArenaColor, 5> kArenaLightColors{
    {{"accent", 0x3DFF7A}, {"cyan", 0x00D4FF}, {"violet", 0xA64DFF}, {"scene_light_white", 0xEDEDED}, {"orange", 0xFF8A1F}}};
inline constexpr ArenaColor kArenaWallColor{"scene_wall", 0x6B6B6B};
inline constexpr ArenaColor kArenaLineColor{"accent", 0x3DFF7A};
inline constexpr ArenaColor kArenaFogColor{"scene_fog", 0x050505};
std::array<float, 3> srgb_to_linear(std::uint32_t rgb) noexcept;

// ---- Nesne stili (sabit; yalniz tohum + indeks) ----
struct ArenaObjectStyle {
    std::array<float, 3> color;  // dogrusal RGB; ayni surunun (kSchoolSize) uyeleri ayni palet rengini tasir
    float scale;                 // [kArenaScaleMin, kArenaScaleMax]
    float roll_phase;            // rad
    float roll_rate;             // rad / kare (isaretli)
};
inline constexpr float kArenaScaleMin = 0.35f, kArenaScaleMax = 0.7f;  // amblem genisligi 0,7 .. 1,4 birim
ArenaObjectStyle arena_object_style(std::uint64_t seed, std::uint32_t index) noexcept;

// ---- GPU'ya giden nesne kaydi (HLSL `Object` sabit tamponu ve ornek akisi ile ayni yerlesim) ----
struct ArenaObjectGpu {
    float pos_scale[4];   // x, y, -, olcek
    float dir_roll[4];    // yon xyz (normalize edilmemis), donus fazi
    float color_rate[4];  // dogrusal rgb, donus hizi
    std::uint32_t zq[4];  // x: kameraya gore z (Q16.16), y: kare basi akis hizi (Q16.16, ikiye tumleyen), z, w: 0
};
static_assert(sizeof(ArenaObjectGpu) == 64, "HLSL Object cbuffer ile ayni yerlesim");

// Cizim listesi: `visible.drawn()` sirasiyla (onden arkaya). flow == false: z = kameraya gore derinlik, akis yok
// (sim acik; her kare yeniden kurulur). flow == true: akis hizi = nesne ileri hizi - kamera adimi (sim kapali;
// bir kez kurulur, golgelendirici kare indeksiyle ilerletir ve arena_flow_mask ile sarar).
void arena_fill_objects(const FrameSim& sim, const VisibleSet& visible, std::span<const ArenaObjectStyle> styles, bool flow,
                        std::vector<ArenaObjectGpu>& out);
// Akis sarma maskesi (Q16.16): en buyuk derinligi kapsayan ikinin kuvveti - 1; en az 16 birim.
std::uint32_t arena_flow_mask(std::uint32_t max_depth_q16) noexcept;
inline constexpr std::uint32_t kArenaNoFlowMask = 0xFFFFFFFFu;

// ---- Isiklar ----
// Dunya z'sinde kArenaLightPeriod periyotlu desen (tunel uzunlugunu tam boler); golgelendirici isigi kameranin
// kArenaLightBack gerisi ile periyot - kArenaLightBack ilerisi arasina sarar. Liste on ek ozelliklidir: N isik,
// daha uzun listenin ilk N ogesidir (Gmin, G'nin ilk isiklarini kullanir) ve her on ek z boyunca dengeli yayilir.
inline constexpr std::uint32_t kArenaMaxLights = 1024;
inline constexpr float kArenaLightPeriod = 256.f;
inline constexpr float kArenaLightBack = 32.f;
inline constexpr float kArenaLightEnergy = 10.f;  // toplam isik enerjisi isik sayisindan bagimsiz (siddet = bu / N)
std::vector<PointLight> arena_lights(std::uint64_t seed, std::uint32_t count);

// TDR guvenligi: aydinlatma cizimi basina isik sayisi piksel sayisiyla kuculur (1080p'ye kadar kLightsPerDraw;
// 2160p'de dortte biri). En az kArenaMinLightChunk.
inline constexpr std::uint64_t kArenaChunkBasePixels = 1920ull * 1080ull;
inline constexpr std::uint32_t kArenaMinLightChunk = 16;
std::uint32_t arena_light_chunk(std::uint64_t pixels) noexcept;
std::uint32_t arena_light_draws(std::uint32_t lights, std::uint64_t pixels) noexcept;  // en az 1 (taban gecisi)

// ---- Hedef kumesi ve video bellegi butcesi ----
// G-buffer (RGBA8 + RGBA16F + RGBA32F) + derinlik + HDR + bloom x2 + LDR = 60 bayt / piksel.
inline constexpr std::uint64_t kTargetBytesPerPixel = 60;
inline constexpr double kTargetBudgetFraction = 0.9;  // kullanim + hedefler butcenin bu payini asamaz
std::uint64_t target_set_bytes(std::uint32_t width, std::uint32_t height) noexcept;
enum class BudgetCheck { Fits, Exceeds, Unknown };
// budget == 0 -> Unknown (okunamadi; sahte karar yok, olusturma denemesi sonucu belirler).
BudgetCheck targets_fit_budget(std::uint64_t budget, std::uint64_t usage, std::uint64_t bytes) noexcept;

// ---- Profiller (A1; sabit) ----
struct ArenaCpuProfile {
    std::string_view name;
    bool sim;
    std::uint32_t objects;       // sahnedeki nesne
    std::uint32_t object_draws;  // kare basina cizilen nesne
    DrawMode draw_mode;
    std::uint32_t max_workers;   // isci = min(mantiksal cekirdek - 1, max_workers); 0 -> isci yok
};
struct ArenaGpuProfile {
    std::string_view name;
    std::uint32_t lights;
    bool bloom;
};
// C: 20 000 simule nesne, 4 000 cizim cagrisi, isci = min(mantiksal cekirdek - 1, 8).
inline constexpr ArenaCpuProfile kArenaCpuC{"C", true, 20000, 4000, DrawMode::PerObject, 8};
// Cmin: simulasyon yok, 1 ornekli cizim (ayni 4 000 nesne; GPU geometrisi C ile ayni kalir).
inline constexpr ArenaCpuProfile kArenaCpuCmin{"Cmin", false, 20000, 4000, DrawMode::Instanced, 0};
// G: 64 isik, bloom acik. Gmin: 8 isik, bloom kapali.
inline constexpr ArenaGpuProfile kArenaGpuG{"G", 64, true};
inline constexpr ArenaGpuProfile kArenaGpuGmin{"Gmin", 8, false};
inline constexpr std::uint32_t kArenaBlurRadius = 12;  // bloom acikken ayrik Gauss yaricapi (piksel)

// Profil cifti + cozunurluk -> SceneConfig (arena = true; olcum penceresi alanlari cagirana kalir).
SceneConfig arena_config(const ArenaCpuProfile& cpu, const ArenaGpuProfile& gpu, std::uint32_t width, std::uint32_t height,
                         std::uint32_t logical_cores) noexcept;

inline constexpr std::uint32_t kArenaMaxObjects = 200000;
// Arena alanlari tutarli mi (nesne / isik ust sinirlari, cizim sayisi <= nesne, sim icin nesne > 0).
bool arena_config_valid(const SceneConfig& c) noexcept;
// Arena config'inin simulasyon ayari (tohum, nesne, cizim butcesi).
SimConfig arena_sim_config(const SceneConfig& c) noexcept;
// Arena is yukunun kimligi: surumler (arena + frame_sim) + tum Arena alanlari + cozunurluk + olcum plani.
std::uint64_t arena_workload_hash(const SceneConfig& c) noexcept;

}  // namespace ef::benchmark
