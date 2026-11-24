// Denge testi "Arena v1" (spec 2026-11-12 A1 / D): sahne islev kontrolu. PENCERE ACILMAZ; ekran disi D3D11.
// Varsayilan (ctest windows.arena_warp): yazilim bagdastiricisi (WARP), 320x180, toplam <= 5 sn. WARP sonucu
//   puanlanmaz; amac sahnenin gercekten cizildigini, surelerin yazildigini ve hedef yeniden kurulumunu gormek.
//   1) C + Gmin 320x180: simulasyon (isci havuzu) + ayiklama + nesne basina cizim; dort sure serisi dolu.
//   2) Cmin + G 480x270: ayni cihazda hedefler yeniden kurulur; simulasyon yok, tek ornekli cizim, bloom acik.
//   3) Video bellegi butcesi yetmeyen cozunurluk -> "vram_budget"; oturum sonraki adimda calismaya devam eder.
//   4) Belirlenimcilik: ayni kare kipli adim iki kez -> ayni is yuku ozeti, ayni dogrulama pikseli, ayni son kare.
//      Cizim kipi (nesne basina / her kare yenilenen ornek tamponu) goruntuyu degistirmez.
//   5) Iptal, tohum uyusmazligi, run_scene tek adim yolu.
// --real: yuksek performansli gercek GPU'da kisa adimlar (S1, S2-S4, K1, K3, T1). K3 (2160p, C + G) birlikte adimin
//   GPU sinirli durumunu gosterir: kare hizi S4 ile, GPU suresi Cmin + G ile karsilastirilir. ctest'e KAYITLI DEGIL;
//   urun sahibi oyundayken kosulmaz. Sayilar bu test sahnesinin olcumudur; oyun FPS tahmini degildir.
// --out <klasor>: son karelerin PNG'leri (uygulayici / denetci goruntuye bakar).
// --balance [--quick] [--real] [--no-preview] [--timing <isinma>,<olcum>]: denge testi kosucusu (spec A2) + canli
//   onizleme okuma yolu (A4). Tum dizi (S1, S2-S4, K1-K3, T1, T2) kosulur; tablo ve "denge_v1" hukumleri yazilir;
//   her adimin son onizleme karesi (beklemesiz okuma yolundan gelen BGRA) PNG olarak yazilir.
//   Varsayilan (ctest windows.balance_warp): WARP, kucuk cozunurlukler ve cok kisa adimlar (yalniz test icin
//   sure / cozunurluk gecersiz kilma). Pencereler gecerlilik kuralindan kisa oldugundan hukum BEKLENMEZ: amac
//   adimlarin, iptalin, butce hatasinin, canli sayilarin ve onizlemenin calistigini gormektir. WARP puanlanmaz.
//   --real: gercek GPU'da gercek cozunurlukler (Tam 2 + 8 sn, --quick 1 + 4 sn). Varsayilan ctest'te KOSMAZ: yalniz
//   opt-in "real_gpu" etiketi (windows.balance_real_gpu, EF_REAL_GPU=1) her donanim bagdastiricisinda --adapter N ile kosar.
//   --adapter N: yuksek performans sirasindaki N. donanim bagdastiricisi (yalniz test). --list-adapters: salt okuma liste.
//   --real ayrica uygulamadaki salt okuma uyum denetimlerini kosar (spec C / D6): ilk adimdan once baska sureclerin
//   GPU yuku (arka plan GPU karistiricisi), CPU hiz sayaclari (C1), S3 sirasinda PCIe linki (C2), olay 37 sayisi.
// --cpu-cores N (YALNIZ TEST; spec 2026-11-12 D "zayif sistemde dogrulama"): bu deneme SURECININ islemci benzesimi ilk N
//   mantiksal cekirdege kisitlanir (SetProcessAffinityMask; yalniz islemci grubu 0). Sistem / guc ayari degismez, kisit
//   surecle birlikte biter; isci havuzu boyutu degismez (is parcaciklari N cekirdege sigdirilir). Amac: gercek GPU'da
//   "--balance --real --cpu-cores 8 / 2 / 1" kosup hukmun daha yuksek cozunurlukte "Islemci darbogazi"na dondugunu kanita
//   yazmak. Uygulamada boyle bir ayar YOKTUR. Gecersiz N (0, sayi degil, uygun cekirdekten fazla) -> cikis 2.
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <array>
#include <chrono>
#include <cwchar>
#include <memory>
#include <optional>

#include "benchmark/balance/balance_checks.h"
#include "benchmark/balance/balance_runner.h"
#include "measurement/telemetry/adapter_gpu.h"
#include "platform/windows/diagnostics/event_reader.h"
#include "platform/windows/metrics/balance_check_sampler.h"
#include "benchmark/scene/arena.h"
#include "benchmark/scene/d3d_renderer.h"
#include "core/diagnosis/balance.h"

using namespace ef::benchmark;
using Microsoft::WRL::ComPtr;

namespace {

int g_fail = 0;
void expect(bool ok, const char* what) {
    if (ok) return;
    ++g_fail;
    std::printf("arena_probe: FAIL %s\n", what);
}

bool save_png_bgra(const std::wstring& path, const std::vector<std::uint8_t>& bgra, std::uint32_t w, std::uint32_t h);

bool save_png(const std::wstring& path, const std::vector<std::uint8_t>& rgba, std::uint32_t w, std::uint32_t h) {
    std::vector<std::uint8_t> bgra(rgba.size());
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) bgra[i] = rgba[i + 2], bgra[i + 1] = rgba[i + 1], bgra[i + 2] = rgba[i], bgra[i + 3] = 255;
    return save_png_bgra(path, bgra, w, h);
}

bool save_png_bgra(const std::wstring& path, const std::vector<std::uint8_t>& bgra, std::uint32_t w, std::uint32_t h) {
    if (bgra.size() != std::size_t{w} * h * 4 || bgra.empty()) return false;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) || FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(enc->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(w, h)))
        return false;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&fmt)) || !IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA)) return false;
    return SUCCEEDED(frame->WritePixels(h, w * 4, static_cast<UINT>(bgra.size()), const_cast<BYTE*>(bgra.data()))) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
}

// Goruntu ozeti: sahne gercekten cizildi mi (tek renk degil; parlak ve doygun pikseller var).
struct ImageStats {
    double mean_luma = 0;       // 0..255
    double lit_fraction = 0;    // luma > 24
    double vivid_fraction = 0;  // doygun (max - min > 64) ve parlak (max > 120): marka renkli nesne / cizgi
    std::size_t distinct = 0;   // 4 bit / kanal nicemlenmis farkli renk
};
ImageStats image_stats(const std::vector<std::uint8_t>& rgba) {
    ImageStats s;
    const std::size_t n = rgba.size() / 4;
    if (!n) return s;
    std::vector<bool> seen(4096, false);
    double sum = 0;
    std::size_t lit = 0, vivid = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const int r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
        const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        sum += luma;
        if (luma > 24) ++lit;
        const int hi = r > g ? (r > b ? r : b) : (g > b ? g : b), lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
        if (hi - lo > 64 && hi > 120) ++vivid;
        const std::size_t key = (static_cast<std::size_t>(r >> 4) << 8) | (static_cast<std::size_t>(g >> 4) << 4) | static_cast<std::size_t>(b >> 4);
        if (!seen[key]) seen[key] = true, ++s.distinct;
    }
    s.mean_luma = sum / static_cast<double>(n);
    s.lit_fraction = static_cast<double>(lit) / static_cast<double>(n);
    s.vivid_fraction = static_cast<double>(vivid) / static_cast<double>(n);
    return s;
}

double mean(const std::vector<double>& v) { return v.empty() ? 0.0 : std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size()); }

void print_step(const char* tag, const SceneConfig& c, const SceneRunResult& r) {
    const double frame = mean(r.frame_ms);
    std::printf("arena_probe: %s %ux%u sim=%d objects=%u workers=%u mode=%s draws=%u..%u lights=%u light_draws=%u bloom=%d | ok=%d err=%s "
                "cancelled=%d prepare_ms=%.1f warmup=%.2fs/%u measure=%.2fs/%u\n",
                tag, r.width, r.height, r.sim, r.instances, r.sim_workers, std::string(to_string(r.draw_mode)).c_str(), r.object_draws_min,
                r.object_draws_max, r.lights, r.light_draws, c.arena_bloom, r.ok, r.error.c_str(), r.cancelled, r.prepare_ms, r.warmup_s,
                r.warmup_frames, r.measure_s, r.measured_frames);
    if (!r.ok) return;
    char busy[32] = "unknown";
    if (r.gpu_elapsed.n && frame > 0) std::snprintf(busy, sizeof busy, "%.3f", r.gpu_elapsed.avg_ms / frame);
    std::printf("arena_probe: %s frame_ms avg=%.3f (fps=%.1f) | sim_ms avg=%.3f p99=%.3f | cull_ms avg=%.3f p99=%.3f | submit_ms avg=%.3f "
                "p99=%.3f | wait_ms avg=%.3f p99=%.3f | gpu_elapsed_ms avg=%.3f p99=%.3f n=%zu excluded=%u | gpu_elapsed/frame=%s source=%s\n",
                tag, frame, frame > 0 ? 1000.0 / frame : 0.0, r.sim_step.avg_ms, r.sim_step.p99_ms, r.cull.avg_ms, r.cull.p99_ms,
                r.cpu_submit.avg_ms, r.cpu_submit.p99_ms, r.wait.avg_ms, r.wait.p99_ms, r.gpu_elapsed.avg_ms, r.gpu_elapsed.p99_ms,
                r.gpu_elapsed.n, r.gpu_excluded, busy, std::string(to_string(r.frame_source)).c_str());
    std::printf("arena_probe: %s hash=%016llx pix=%016llx sim_verified=%s sim_checksum=%016llx ts=%d fence=%d\n", tag,
                static_cast<unsigned long long>(r.workload_hash), static_cast<unsigned long long>(r.pixel_checksum),
                r.sim_verified ? (*r.sim_verified ? "yes" : "NO") : "n/a", static_cast<unsigned long long>(r.sim_checksum), r.gpu_timestamps,
                r.fence_wait);
}

// Sure serileri: her olculen kare icin bir deger, sonlu ve negatif degil.
void check_timers(const SceneRunResult& r, bool sim) {
    const std::size_t n = r.measured_frames;
    expect(r.submit_ms().size() == n && r.wait_ms.size() == n, "submit_ms / wait_ms: olculen kare basina bir deger");
    expect(sim ? (r.sim_ms.size() == n && r.cull_ms.size() == n) : (r.sim_ms.empty() && r.cull_ms.empty()),
           "sim_ms / cull_ms: sim aciksa kare basina bir deger, kapaliysa bos");
    bool finite = true;
    for (const auto* v : {&r.sim_ms, &r.cull_ms, &r.cpu_submit_ms, &r.wait_ms, &r.frame_ms})
        for (const double x : *v) finite = finite && std::isfinite(x) && x >= 0;
    expect(finite, "sureler sonlu ve >= 0");
    if (sim) expect(r.sim_step.avg_ms > 0 && r.cull.avg_ms > 0 && r.cpu_submit.avg_ms > 0, "sim / cull / submit sureleri > 0");
}

struct Shot {
    std::vector<std::uint8_t> rgba;
    std::uint32_t w = 0, h = 0;
};

// Son kareyi okur, ozetler, istenirse PNG yazar.
bool capture(ArenaSession& s, const std::wstring& out_dir, const wchar_t* name, const char* tag, Shot& shot) {
    if (!s.read_frame(shot.rgba, shot.w, shot.h)) {
        std::printf("arena_probe: %s read_frame failed\n", tag);
        return false;
    }
    const ImageStats st = image_stats(shot.rgba);
    std::printf("arena_probe: %s image %ux%u mean_luma=%.1f lit=%.3f vivid=%.4f distinct=%zu\n", tag, shot.w, shot.h, st.mean_luma,
                st.lit_fraction, st.vivid_fraction, st.distinct);
    expect(st.distinct >= 64, "goruntu tek renk degil (>= 64 farkli renk)");
    expect(st.lit_fraction > 0.05, "goruntude aydinlatilmis alan var");
    expect(st.vivid_fraction > 0.002, "goruntude marka renkli parlak pikseller var");
    if (!out_dir.empty()) {
        const std::wstring path = out_dir + L"\\" + name;
        const bool ok = save_png(path, shot.rgba, shot.w, shot.h);
        std::wprintf(L"arena_probe: png %ls %hs\n", path.c_str(), ok ? "written" : "WRITE FAILED");
        expect(ok, "PNG yazildi");
    }
    return true;
}

std::uint32_t logical_cores() {
    const unsigned n = std::thread::hardware_concurrency();
    return n ? n : 1u;
}

int run_warp(const std::wstring& out_dir) {
    std::atomic<bool> cancel{false};
    const std::uint32_t cores = logical_cores();
    ArenaSession s;
    SceneConfig base = arena_config(kArenaCpuC, kArenaGpuGmin, 320, 180, cores);
    if (!s.open(base, false)) {
        std::printf("arena_probe: skipped (WARP cihazi acilamadi: %s)\n", s.error().c_str());
        return 77;
    }
    std::wprintf(L"arena_probe: adapter=\"%ls\" kind=%hs fl=0x%x cores=%u\n", s.adapter().name.c_str(),
                 std::string(to_string(s.adapter().kind)).c_str(), s.adapter().feature_level, cores);
    expect(s.adapter().kind == RendererKind::Software, "WARP Software olarak isaretli (puanlanmaz)");

    // 1) C + Gmin, 320x180 (S1 ayari): simulasyon + ayiklama + nesne basina cizim
    SceneConfig c1 = base;
    c1.warmup_s = 0.3;
    c1.measure_s = 1.0;
    const auto r1 = s.run_step(c1, cancel);
    print_step("c_gmin", c1, r1);
    expect(r1.ok, "C + Gmin adimi tamamlandi");
    if (r1.ok) {
        check_timers(r1, true);
        expect(r1.sim_verified && *r1.sim_verified && r1.sim_checksum == sim_golden_checksum(), "simulasyon altin saglamasi (isci havuzuyla)");
        expect(r1.object_draws_min == kArenaCpuC.object_draws && r1.object_draws_max == kArenaCpuC.object_draws, "kare basina 4 000 cizim");
        expect(r1.sim_workers == sim_worker_count(cores) && r1.draw_mode == DrawMode::PerObject, "C profili: isci sayisi ve PerObject");
        expect(r1.adapter.kind == RendererKind::Software && r1.arena, "sonuc Arena + Software");
        Shot shot;
        capture(s, out_dir, L"arena_warp_c_gmin_320x180.png", "c_gmin", shot);
    }

    // 2) Cmin + G, 480x270: ayni cihazda hedefler yeniden kurulur; simulasyon yok, tek ornekli cizim, bloom
    SceneConfig c2 = arena_config(kArenaCpuCmin, kArenaGpuG, 480, 270, cores);
    c2.warmup_s = 0.2;
    c2.measure_s = 0.6;
    const auto r2 = s.run_step(c2, cancel);
    print_step("cmin_g", c2, r2);
    expect(r2.ok, "Cmin + G adimi tamamlandi (hedefler yeniden kuruldu)");
    if (r2.ok) {
        check_timers(r2, false);
        expect(!r2.sim_verified.has_value() && r2.sim_workers == 0 && r2.draw_mode == DrawMode::Instanced, "Cmin: simulasyon yok, ornekli cizim");
        expect(r2.object_draws_min == kArenaCpuCmin.object_draws && r2.object_draws_max == kArenaCpuCmin.object_draws,
               "Cmin: ayni 4 000 nesne tek cizimde");
        expect(r2.width == 480 && r2.height == 270, "yeni cozunurluk");
        Shot shot;
        capture(s, out_dir, L"arena_warp_cmin_g_480x270.png", "cmin_g", shot);
    }

    // 3) Butce yetmeyen cozunurluk: adim "vram_budget" ile biter, oturum yasamaya devam eder
    s.limit_vram_budget(1ull << 20);
    SceneConfig c3 = arena_config(kArenaCpuCmin, kArenaGpuGmin, 640, 360, cores);
    c3.frames = 4;
    const auto r3 = s.run_step(c3, cancel);
    std::printf("arena_probe: budget 640x360 need=%llu limit=%llu -> ok=%d err=%s\n",
                static_cast<unsigned long long>(target_set_bytes(640, 360)), 1ull << 20, r3.ok, r3.error.c_str());
    expect(!r3.ok && r3.error == "vram_budget" && !r3.cancelled, "butce yetmeyince vram_budget");
    s.limit_vram_budget(0);

    // 4) Belirlenimcilik: kare kipi, ayni ayar iki kez (ayni cihaz; butce hatasindan sonra hedefler yeniden kurulur)
    SceneConfig c4 = base;
    c4.warmup_frames = 2;
    c4.frames = 10;
    const auto a = s.run_step(c4, cancel);
    Shot sa, sb;
    const bool ga = a.ok && s.read_frame(sa.rgba, sa.w, sa.h);
    const auto b = s.run_step(c4, cancel);
    const bool gb = b.ok && s.read_frame(sb.rgba, sb.w, sb.h);
    std::printf("arena_probe: determinism ok=%d/%d hash=%016llx/%016llx pix=%016llx/%016llx frames=%u/%u last_frame_equal=%d\n", a.ok, b.ok,
                static_cast<unsigned long long>(a.workload_hash), static_cast<unsigned long long>(b.workload_hash),
                static_cast<unsigned long long>(a.pixel_checksum), static_cast<unsigned long long>(b.pixel_checksum), a.measured_frames,
                b.measured_frames, ga && gb && sa.rgba == sb.rgba);
    expect(a.ok && b.ok, "butce hatasindan sonra oturum calisiyor");
    expect(a.workload_hash == b.workload_hash && a.workload_hash == workload_hash(c4, {}), "ayni ayar -> ayni is yuku ozeti");
    expect(a.pixel_checksum == b.pixel_checksum && a.pixel_checksum != 0, "ayni ayar -> ayni dogrulama pikseli");
    expect(a.measured_frames == c4.frames && a.warmup_frames == c4.warmup_frames, "kare kipi: istenen kare sayisi");
    expect(ga && gb && sa.rgba == sb.rgba, "ayni kare indeksi -> ayni son kare (is zamanla degil kare numarasiyla tohumlanir)");
    expect(a.workload_hash != r1.workload_hash, "olcum plani kimlige girer");

    // 4b) Cizim kipi resmi degistirmez: ayni simulasyon kareleri nesne basina cizimle ve her kare yenilenen ornek
    //     tamponuyla (tek cizim) ayni goruntuyu verir.
    SceneConfig c5 = c4;
    c5.draw_mode = DrawMode::Instanced;
    const auto d = s.run_step(c5, cancel);
    Shot sd;
    const bool gd = d.ok && s.read_frame(sd.rgba, sd.w, sd.h);
    std::size_t differing = 0;
    const bool comparable = ga && gd && sa.rgba.size() == sd.rgba.size() && !sa.rgba.empty();
    if (comparable)
        for (std::size_t i = 0; i + 3 < sa.rgba.size(); i += 4)
            if (std::memcmp(&sa.rgba[i], &sd.rgba[i], 4) != 0) ++differing;
    std::printf("arena_probe: draw_mode instanced+sim ok=%d err=%s draws=%u..%u pix=%016llx (per_object %016llx) last_frame_diff_pixels=%zu/%zu\n",
                d.ok, d.error.c_str(), d.object_draws_min, d.object_draws_max, static_cast<unsigned long long>(d.pixel_checksum),
                static_cast<unsigned long long>(a.pixel_checksum), differing, sa.rgba.size() / 4);
    expect(d.ok && d.sim && d.draw_mode == DrawMode::Instanced, "simulasyon + ornekli cizim (her kare yenilenen ornek tamponu)");
    expect(d.object_draws_min == kArenaCpuC.object_draws && d.object_draws_max == kArenaCpuC.object_draws, "ornekli cizimde de 4 000 nesne");
    expect(d.pixel_checksum == a.pixel_checksum, "cizim kipi dogrulama pikselini degistirmez");
    expect(comparable && differing * 100 <= sa.rgba.size() / 4, "cizim kipi son kareyi degistirmez (en cok %1 piksel fark)");
    expect(d.workload_hash != a.workload_hash, "cizim kipi kimlige girer");

    // 5) Iptal, tohum uyusmazligi, tek adim yolu
    std::atomic<bool> stopped{true};
    const auto x = s.run_step(c4, stopped);
    expect(x.cancelled && !x.ok, "iptal: kismi sonuc yok");
    SceneConfig bad = c4;
    bad.seed ^= 1;
    const auto y = s.run_step(bad, cancel);
    expect(!y.ok && y.error == "bad_config", "tohum oturumla ayni olmali");
    SceneConfig one = arena_config(kArenaCpuCmin, kArenaGpuGmin, 320, 180, cores);
    one.frames = 6;
    const auto z = run_scene(one, cancel, false);
    std::printf("arena_probe: run_scene(arena) ok=%d err=%s kind=%s frames=%u\n", z.ok, z.error.c_str(),
                std::string(to_string(z.adapter.kind)).c_str(), z.measured_frames);
    expect(z.ok && z.arena && z.adapter.kind == RendererKind::Software && z.measured_frames == 6, "run_scene Arena tek adim");
    return g_fail ? 1 : 0;
}

int run_real(const std::wstring& out_dir) {
    std::atomic<bool> cancel{false};
    const std::uint32_t cores = logical_cores();
    ArenaSession s;
    SceneConfig base = arena_config(kArenaCpuC, kArenaGpuGmin, 320, 180, cores);
    if (!s.open(base, true)) {
        std::printf("arena_probe: real skipped (cihaz acilamadi: %s)\n", s.error().c_str());
        return 77;
    }
    std::wprintf(L"arena_probe: adapter=\"%ls\" vendor=0x%04x kind=%hs fl=0x%x vram=%llu MiB cores=%u\n", s.adapter().name.c_str(),
                 s.adapter().vendor_id, std::string(to_string(s.adapter().kind)).c_str(), s.adapter().feature_level,
                 static_cast<unsigned long long>(s.adapter().dedicated_vram >> 20), cores);
    if (s.adapter().kind != RendererKind::Hardware) {
        std::printf("arena_probe: real skipped (donanim bagdastiricisi yok)\n");
        return 77;
    }
    struct Step {
        const char* tag;
        const ArenaCpuProfile* cpu;
        const ArenaGpuProfile* gpu;
        std::uint32_t w, h;
        double warm, measure;
        const wchar_t* png;  // nullptr: yazilmaz
    };
    const Step steps[] = {
        {"S1 C+Gmin", &kArenaCpuC, &kArenaGpuGmin, 320, 180, 1.0, 3.0, L"arena_real_s1_c_gmin_320x180.png"},
        {"S2 Cmin+G", &kArenaCpuCmin, &kArenaGpuG, 1920, 1080, 1.0, 3.0, L"arena_real_s2_cmin_g_1080p.png"},
        {"S3 Cmin+G", &kArenaCpuCmin, &kArenaGpuG, 2560, 1440, 0.5, 1.5, nullptr},
        {"S4 Cmin+G", &kArenaCpuCmin, &kArenaGpuG, 3840, 2160, 0.5, 1.5, nullptr},
        {"K1 C+G", &kArenaCpuC, &kArenaGpuG, 1920, 1080, 1.0, 3.0, L"arena_real_k1_c_g_1080p.png"},
        {"K3 C+G", &kArenaCpuC, &kArenaGpuG, 3840, 2160, 0.5, 1.5, nullptr},
        {"T1 C+Gmin", &kArenaCpuC, &kArenaGpuGmin, 320, 180, 0.5, 1.5, nullptr},
    };
    for (const Step& st : steps) {
        SceneConfig c = arena_config(*st.cpu, *st.gpu, st.w, st.h, cores);
        c.warmup_s = st.warm;
        c.measure_s = st.measure;
        const auto r = s.run_step(c, cancel);
        print_step(st.tag, c, r);
        if (!r.ok && (r.error == "vram_budget" || r.error == "target_create")) {
            std::printf("arena_probe: %s olculemedi (%s); diger adimlar surer\n", st.tag, r.error.c_str());
            continue;
        }
        expect(r.ok, "gercek GPU adimi tamamlandi");
        if (!r.ok) continue;
        check_timers(r, st.cpu->sim);
        if (st.cpu->sim) expect(r.sim_verified && *r.sim_verified, "simulasyon altin saglamasi");
        if (st.png) {
            Shot shot;
            capture(s, out_dir, st.png, st.tag, shot);
        }
    }
    return g_fail ? 1 : 0;
}

// ---------------- Denge testi kosucusu (--balance) ----------------
namespace bal = ef::benchmark::balance;
namespace dg = ef::core::diagnosis;

// Bir adimin onizleme kareleri (kosucu is parcacigindan yazilir; okuma kosu bittikten sonra).
struct StepShot {
    std::vector<std::uint8_t> bgra;  // son iletilen kare (satir dolgusuz)
    std::uint32_t w = 0, h = 0, src_w = 0, src_h = 0;
    std::uint32_t frames = 0, in_measure = 0, in_warmup = 0;
    bool final_seen = false;
    bool bad = false;  // bos / tutarsiz kare iletildi
};

// Canli sayaclarin (FrameTap) baska bir is parcacigindan gorunumu: UI'nin yapacagi gibi yoklanir.
struct TapWatch {
    bool seen = false, warm_live = false, measure_live = false;
    std::uint32_t width = 0, height = 0, total = 0, code = 0;
    FrameTapLive last;  // olcum fazindaki son goruntu
};

struct BalanceRun {
    bal::BalanceReport report;
    std::array<StepShot, bal::kStepCount> shots;
    std::array<TapWatch, bal::kStepCount> taps;
    std::uint32_t progress_begin = 0, progress_end = 0;
};

// Salt okuma uyum denetimleri (yalniz --real): uygulamanin kullandigi ornekleyici + arka plan GPU ornegi.
struct ProbeChecks {
    ef::platform::metrics::BalanceCheckSampler sampler;
    std::optional<double> other_gpu_pct;
    bool before_steps_called = false;
};

// Kosuyu canli sayac + (istenirse) onizleme tuketicisiyle yurutur. `cancel_after_s` > 0: o kadar sonra iptal.
void run_balance_once(bal::BalanceOptions o, bool preview_on, double cancel_after_s, BalanceRun& out, ProbeChecks* checks = nullptr) {
    std::atomic<bool> cancel{false}, stop{false};
    FrameTap tap;
    int current = -1;  // kosucu is parcacigi: suren adim
    const PreviewCallback on_preview = [&](const PreviewFrame& f) {
        if (current < 0) return;
        StepShot& s = out.shots[static_cast<std::size_t>(current)];
        if (!copy_preview(f, s.bgra)) {  // UI tuketicisinin yapacagi tek kopya
            s.bad = true;
            return;
        }
        s.w = f.width, s.h = f.height, s.src_w = f.source_width, s.src_h = f.source_height;
        ++s.frames;
        if (f.final_frame) s.final_seen = true;
        else if (f.measuring) ++s.in_measure;
        else ++s.in_warmup;
    };
    o.frame_tap = &tap;
    o.preview = preview_on ? &on_preview : nullptr;
    if (checks)
        o.before_steps = [checks](const AdapterInfo& a, const std::atomic<bool>& c) {
            checks->before_steps_called = true;
            checks->sampler.set_adapter(a.vendor_id, a.device_id);
            checks->other_gpu_pct = ef::measurement::sample_other_process_gpu_pct(a.luid, GetCurrentProcessId(),
                                                                                   ef::measurement::kOtherGpuSampleSeconds, c);
        };
    o.progress = [&, timing = bal::effective_timing(o)](const bal::StepProgress& p) {
        if (checks) bal::notify_check_sampler(checks->sampler, p, timing);
        if (p.finished) {
            ++out.progress_end;
            current = -1;
        } else {
            ++out.progress_begin;
            current = static_cast<int>(p.id);
        }
    };
    std::thread sampler([&] {
        const auto t0 = std::chrono::steady_clock::now();
        while (!stop.load()) {
            const FrameTapLive s = tap.snapshot();
            if (s.step_index >= 1 && s.step_index <= bal::kStepCount) {
                TapWatch& w = out.taps[s.step_index - 1];
                w.seen = true;
                w.width = s.width, w.height = s.height, w.total = s.step_total, w.code = s.step_code;
                if (s.has_window && s.phase == TapPhase::Warmup) w.warm_live = true;
                if (s.has_window && s.phase == TapPhase::Measure) {
                    w.measure_live = true;
                    w.last = s;
                }
            }
            if (cancel_after_s > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= cancel_after_s) cancel = true;
            Sleep(5);
        }
    });
    if (checks) checks->sampler.start();
    out.report = bal::run_balance(o, cancel);
    if (checks) checks->sampler.stop();
    stop = true;
    sampler.join();
}

const char* yes_no(bool v) { return v ? "yes" : "no"; }

void print_balance_table(const char* tag, const BalanceRun& run) {
    const bal::BalanceReport& r = run.report;
    std::wprintf(L"balance: %hs adapter=\"%ls\" kind=%hs vendor=0x%04x vram=%llu MiB\n", tag, r.adapter.name.c_str(),
                 std::string(to_string(r.adapter.kind)).c_str(), r.adapter.vendor_id, static_cast<unsigned long long>(r.adapter.dedicated_vram >> 20));
    std::printf("balance: %s ok=%d cancelled=%d error=%s software=%d quick=%d timing=%.2f+%.2f s cores=%u preview=%d retries=%u seconds=%.1f\n", tag,
                r.ok, r.cancelled, r.error.empty() ? "-" : r.error.c_str(), r.software, r.quick, r.timing.warmup_s, r.timing.measure_s,
                r.logical_cores, r.preview, r.retries, r.seconds);
    std::printf("balance: %s fingerprint=%s\n", tag, r.fingerprint.c_str());
    std::printf("balance: %s step res        cpu  gpu  status       state        reason             fps      cv    sec frames  gpu_busy bound    "
                "wait   frame_ms gpu_ms  sim_ms cull_ms submit_ms src            prev(n/ms) att\n",
                tag);
    for (const bal::StepResult& s : r.steps) {
        char res[24], busy[16] = "-", wait[16] = "-", bound[16] = "-";
        std::snprintf(res, sizeof res, "%ux%u", s.plan.config.width, s.plan.config.height);
        if (s.stats.gpu_busy_ratio) std::snprintf(busy, sizeof busy, "%.3f", *s.stats.gpu_busy_ratio);
        if (s.wait_share) std::snprintf(wait, sizeof wait, "%.3f", *s.wait_share);
        if (s.bound_confirmed) std::snprintf(bound, sizeof bound, "%s", *s.bound_confirmed ? "held" : "NOT-HELD");
        std::printf("balance: %s %-4s %-10s %-4s %-4s %-12s %-12s %-18s %8.1f %5.3f %3zu %6zu  %-8s %-8s %-6s %8.3f %6.3f %6.3f %7.3f %9.3f %-14s %u/%.1f %u\n",
                    tag, std::string(bal::to_string(s.plan.id)).c_str(), res, std::string(s.plan.cpu->name).c_str(),
                    std::string(s.plan.gpu->name).c_str(), std::string(bal::to_string(s.status)).c_str(),
                    std::string(dg::to_string(s.stats.state)).c_str(),
                    s.stats.reason.empty() ? (s.error.empty() ? "-" : s.error.c_str()) : std::string(s.stats.reason).c_str(), s.stats.fps,
                    s.stats.cv, s.stats.seconds, s.stats.frames, busy, bound, wait, s.frame_ms, s.gpu_ms, s.sim_ms, s.cull_ms, s.submit_ms,
                    std::string(to_string(s.frame_source)).c_str(), s.preview_frames, s.preview_ms, s.attempts);
    }
}

// Kural ciktisi. Pil, guc plani ve arka plan CPU'su bu denemede OKUNMAZ (uygulama doldurur); --real kosusunda arka
// plan GPU'su ile CPU hiz notu okunur (checks_read).
dg::BalanceResult print_balance_verdicts(const char* tag, const bal::BalanceReport& r, bool checks_read = false) {
    const dg::BalanceResult v = dg::evaluate_balance(r.inputs);
    std::printf("balance: %s rule=%s (bu test sahnesinde; oyun FPS tahmini degildir; %s)\n", tag, std::string(v.rule).c_str(),
                checks_read ? "arka plan GPU'su ve CPU hiz notu okundu; pil / guc plani / arka plan CPU'su okunmadi"
                            : "karistirici girdileri bu denemede okunmadi");
    if (v.cpu_fps) {
        std::printf("balance: %s cpu_fps=%s%.1f", tag, v.cpu_lower_bound ? ">=" : "", *v.cpu_fps);
        if (v.cpu_fps_lo && v.cpu_fps_hi) std::printf(" drift_range=%.1f..%.1f", *v.cpu_fps_lo, *v.cpu_fps_hi);
        if (v.cpu_sim_share && v.cpu_draw_share) std::printf(" cpu_split sim=%.0f%% draw=%.0f%%", *v.cpu_sim_share * 100, *v.cpu_draw_share * 100);
        std::printf("\n");
    } else {
        std::printf("balance: %s cpu_fps=- (S1 gecersiz / olculmedi)\n", tag);
    }
    for (std::size_t i = 0; i < dg::kBalanceRows; ++i) {
        const dg::BalanceRow& row = v.rows[i];
        char gpu[32] = "-", coupled[32] = "-", ratio[16] = "-", cr[16] = "-", cb[16] = "-";
        if (row.gpu_fps) std::snprintf(gpu, sizeof gpu, "%s%.1f", row.gpu_lower_bound ? ">=" : "", *row.gpu_fps);
        if (row.coupled_fps) std::snprintf(coupled, sizeof coupled, "%.1f", *row.coupled_fps);
        if (row.ratio) std::snprintf(ratio, sizeof ratio, "%.3f", *row.ratio);
        if (row.coupled_ratio) std::snprintf(cr, sizeof cr, "%.3f", *row.coupled_ratio);
        if (row.coupled_gpu_busy) std::snprintf(cb, sizeof cb, "%.3f", *row.coupled_gpu_busy);
        std::printf("balance: %s row %sp gpu_fps=%s coupled_fps=%s ratio=%s coupled_ratio=%s coupled_gpu_busy=%s -> verdict=%s reason=%s\n", tag,
                    std::string(dg::kBalanceRowKeys[i]).c_str(), gpu, coupled, ratio, cr, cb, std::string(dg::to_string(row.verdict)).c_str(),
                    row.reason.empty() ? "-" : std::string(row.reason).c_str());
    }
    std::string notes, globals;
    for (const auto n : v.notes) notes += (notes.empty() ? "" : ",") + std::string(n);
    for (const auto g : v.global_reasons) globals += (globals.empty() ? "" : ",") + std::string(g);
    std::printf("balance: %s global_reasons=%s notes=%s cpu_drift=%d gpu_drift=%d\n", tag, globals.empty() ? "-" : globals.c_str(),
                notes.empty() ? "-" : notes.c_str(), v.cpu_drift, v.gpu_drift);
    return v;
}

// Onizleme kareleri: boyut, alfa, icerik; her adimin son karesi PNG.
void check_previews(const char* tag, const BalanceRun& run, const std::wstring& out_dir, const wchar_t* prefix) {
    for (std::size_t i = 0; i < bal::kStepCount; ++i) {
        const bal::StepResult& sr = run.report.steps[i];
        const StepShot& shot = run.shots[i];
        if (sr.status != bal::StepStatus::Measured) {
            expect(shot.frames == 0 || sr.status == bal::StepStatus::Cancelled, "kosulmayan adimda onizleme karesi yok");
            continue;
        }
        if (sr.error == "no_measured_frames") continue;  // olcum araligi olusmadi: sonuc (ve onizleme sayaci) yok
        const std::string id(bal::to_string(sr.plan.id));
        const PreviewSize want = preview_size(sr.plan.config.width, sr.plan.config.height);
        std::size_t opaque = 0;
        for (std::size_t k = 3; k < shot.bgra.size(); k += 4) opaque += shot.bgra[k] == 255;
        std::vector<std::uint8_t> rgba(shot.bgra.size());
        for (std::size_t k = 0; k + 3 < shot.bgra.size(); k += 4) rgba[k] = shot.bgra[k + 2], rgba[k + 1] = shot.bgra[k + 1], rgba[k + 2] = shot.bgra[k], rgba[k + 3] = 255;
        const ImageStats st = image_stats(rgba);
        std::printf("balance: %s %s preview %ux%u (hedef %ux%u) frames=%u warmup=%u measure=%u final=%s runner_count=%u readback_ms=%.2f | "
                    "mean_luma=%.1f lit=%.3f vivid=%.4f distinct=%zu\n",
                    tag, id.c_str(), shot.w, shot.h, shot.src_w, shot.src_h, shot.frames, shot.in_warmup, shot.in_measure, yes_no(shot.final_seen),
                    sr.preview_frames, sr.preview_ms, st.mean_luma, st.lit_fraction, st.vivid_fraction, st.distinct);
        expect(sr.preview && !shot.bad && shot.frames >= 1 && shot.final_seen, "adimda onizleme uretildi ve adim sonu karesi geldi");
        // Tekrarlanan adimda (D6) tuketici atilan denemenin onizleme karelerini de gormustur; kosucunun sayaci SON
        // denemeye aittir. Tek denemede sayilar esittir.
        if (sr.attempts <= 1) expect(shot.frames == sr.preview_frames, "kosucunun onizleme sayaci tuketicinin gordugu kare sayisina esit");
        else expect(shot.frames > sr.preview_frames, "tekrarlanan adim: tuketici atilan denemenin karelerini de gordu; kosucu sayaci son denemenin");
        expect(shot.w == want.width && shot.h == want.height && shot.w <= kPreviewMaxWidth && shot.h <= kPreviewMaxHeight,
               "onizleme boyutu: 960x540 kutusuna sigdirilmis, buyutulmemis");
        expect(shot.src_w == sr.plan.config.width && shot.src_h == sr.plan.config.height, "onizleme kaynak cozunurlugu adiminki");
        expect(!shot.bgra.empty() && opaque == shot.bgra.size() / 4, "onizleme alfa = 255 (onceden carpilmis BGRA)");
        expect(st.distinct >= 64 && st.lit_fraction > 0.05 && st.vivid_fraction > 0.002, "onizleme karesi sahneyi gosteriyor (tek renk degil)");
        if (!out_dir.empty() && !shot.bgra.empty()) {
            wchar_t name[96];
            std::swprintf(name, 96, L"%ls_%hs_%ux%u.png", prefix, id.c_str(), sr.plan.config.width, sr.plan.config.height);
            const std::wstring path = out_dir + L"\\" + name;
            const bool ok = save_png_bgra(path, shot.bgra, shot.w, shot.h);
            std::wprintf(L"balance: png %ls %hs\n", path.c_str(), ok ? "written" : "WRITE FAILED");
            expect(ok, "onizleme PNG yazildi");
        }
    }
}

void print_taps(const char* tag, const BalanceRun& run) {
    for (std::size_t i = 0; i < bal::kStepCount; ++i) {
        const TapWatch& w = run.taps[i];
        if (!w.seen) continue;
        char gpu[16] = "-";
        if (w.last.gpu_ms) std::snprintf(gpu, sizeof gpu, "%.3f", *w.last.gpu_ms);
        std::printf("balance: %s tap %s %ux%u step %zu/%u warmup_live=%s measure_live=%s | last: fps=%.1f cpu_ms=%.3f gpu_ms=%s wait=%.3f ring=%zu "
                    "updates=%llu\n",
                    tag, std::string(bal::to_string(run.report.steps[i].plan.id)).c_str(), w.width, w.height, i + 1, w.total, yes_no(w.warm_live),
                    yes_no(w.measure_live), w.last.fps, w.last.cpu_ms, gpu, w.last.wait_share, w.last.ring_count,
                    static_cast<unsigned long long>(w.last.updates));
    }
}

// WARP: kisa adimlarla islev kontrolu (puanlanmaz). Pencereler gecerlilik kuralindan kisa -> hukum beklenmez.
int run_balance_warp(const std::wstring& out_dir, bool preview_on, std::optional<bal::StepTiming> timing) {
    bal::BalanceOptions o;
    o.prefer_hardware = false;
    o.allow_software = true;
    o.timing_override = timing ? *timing : bal::StepTiming{0.45, 0.25};
    o.cpu_resolution_override = bal::Resolution{320, 180};
    o.row_resolution_override = std::array<bal::Resolution, dg::kBalanceRows>{{{320, 180}, {384, 216}, {480, 270}}};
    const std::uint32_t cores = logical_cores();

    // 1) Tum dizi
    auto run = std::make_unique<BalanceRun>();
    run_balance_once(o, preview_on, 0, *run);
    const bal::BalanceReport& r = run->report;
    if (!r.error.empty() && r.adapter.kind == RendererKind::None) {
        std::printf("balance: skipped (WARP cihazi acilamadi: %s)\n", r.error.c_str());
        return 77;
    }
    print_balance_table("warp", *run);
    expect(r.ok && !r.cancelled && r.error.empty(), "WARP: dokuz adim da denendi");
    expect(r.software && r.adapter.kind == RendererKind::Software, "WARP Software olarak isaretli (puanlanmaz)");
    expect(r.fingerprint == bal::plan_fingerprint(o, cores) && r.logical_cores == cores, "plan kimligi seceneklerden turetilir");
    expect(run->progress_begin == bal::kStepCount && run->progress_end == bal::kStepCount, "her adim icin baslangic + bitis bildirimi");
    for (const bal::StepResult& s : r.steps) {
        expect(s.status == bal::StepStatus::Measured, "WARP: adim kosuldu");
        expect(s.stats.state == dg::StepState::Invalid, "kisa pencere gecerlilik kuralini SAGLAMAZ (hukum uretilmez)");
        expect(s.error == "no_measured_frames" || (s.measured_frames >= 2 && s.frame_ms > 0), "olculen kareler ve kare araligi");
        expect(s.bound_confirmed.has_value() == (s.plan.kind != bal::StepKind::Coupled), "sinir dogrulamasi yalniz S ve T adimlarinda");
        expect(s.workload_hash == arena_workload_hash(s.plan.config), "adim is yuku ozeti planla ayni");
        expect(s.attempts == 1, "kisa pencere 'unstable' degildir: adim tekrar edilmez");
    }
    expect(r.retries == 0, "tekrar yok: kosul 0");
    expect(r.step(bal::StepId::S1).workload_hash == r.step(bal::StepId::T1).workload_hash &&
               r.step(bal::StepId::S2).workload_hash == r.step(bal::StepId::T2).workload_hash,
           "tekrar adimlari ilk olcumle ayni is yuku");
    expect(r.step(bal::StepId::S1).pixel_checksum == r.step(bal::StepId::T1).pixel_checksum && r.step(bal::StepId::S1).pixel_checksum != 0 &&
               r.step(bal::StepId::S2).pixel_checksum == r.step(bal::StepId::T2).pixel_checksum,
           "tekrar adimlari ayni dogrulama pikselini cizer");
    expect(r.step(bal::StepId::S1).sim_workers == sim_worker_count(cores) && r.step(bal::StepId::S2).sim_workers == 0 &&
               r.step(bal::StepId::K1).sim_workers == sim_worker_count(cores),
           "C adimlari isci havuzuyla, Cmin adimlari simulasyonsuz");
    const dg::BalanceResult v = print_balance_verdicts("warp", r);
    for (const dg::BalanceRow& row : v.rows)
        expect(row.verdict == dg::BalanceVerdict::Undetermined && row.reason == dg::balance_reason::kCpuStepInvalid,
               "gecersiz S1 -> tum satirlar Belirlenemedi (cpu_step_invalid)");
    expect(!v.cpu_fps.has_value(), "gecersiz adimin degeri gosterilmez");
    print_taps("warp", *run);
    bool any_warm = false, any_measure = false;
    for (std::size_t i = 0; i < bal::kStepCount; ++i) {
        const TapWatch& w = run->taps[i];
        expect(w.seen && w.total == bal::kStepCount && w.code == i + 1, "canli sayac: adim indeksi / toplam / kod");
        expect(w.width == r.steps[i].plan.config.width && w.height == r.steps[i].plan.config.height, "canli sayac: adim cozunurlugu");
        any_warm = any_warm || w.warm_live;
        any_measure = any_measure || w.measure_live;
    }
    expect(any_warm, "canli sayilar isinmada da beslenir (en az bir adimda goruldu)");
    expect(any_measure, "canli sayilar olcumde beslenir");
    if (preview_on) {
        expect(r.preview, "onizleme kosulu rapora yazildi");
        check_previews("warp", *run, out_dir, L"balance_warp");
    } else {
        expect(!r.preview, "onizleme kapali: kosul kapali");
        for (const StepShot& s : run->shots) expect(s.frames == 0, "onizleme kapaliyken kare iletilmez");
    }

    // 2) Bir cozunurlugun hedefleri butceye sigmiyor: o satirin adimlari "olculemedi", digerleri surer.
    bal::BalanceOptions ob = o;
    ob.timing_override = bal::StepTiming{0.05, 0.15};
    ob.vram_budget_limit = 1ull << 20;
    ob.vram_limit_row = 1;
    auto rb = std::make_unique<BalanceRun>();
    run_balance_once(ob, false, 0, *rb);
    print_balance_table("warp-budget", *rb);
    expect(rb->report.ok && rb->report.error.empty(), "butce hatasi kosuyu bitirmez");
    for (const bal::StepResult& s : rb->report.steps) {
        if (s.plan.row == 1)
            expect(s.status == bal::StepStatus::Unmeasurable && s.error == "vram_budget" && s.stats.state == dg::StepState::Unmeasurable &&
                       s.stats.reason == dg::balance_reason::kVramBudget,
                   "butce yetmeyen cozunurluk: adim olculemedi (vram_budget)");
        else
            expect(s.status == bal::StepStatus::Measured, "diger adimlar surer");
    }
    {
        // Gecerli S1/T1 varsayilirsa (kisa pencere yerine) bu satir "olculemedi" olur: kural girdisi dogru tasiniyor mu.
        dg::BalanceInputs in = rb->report.inputs;
        dg::StepStats ok;
        ok.state = dg::StepState::Valid, ok.fps = 100, ok.frames = 400, ok.seconds = 4, ok.gpu_busy_ratio = 0.1;
        in.cpu = in.cpu_repeat = ok;
        const dg::BalanceResult vb = dg::evaluate_balance(in);
        expect(vb.rows[1].verdict == dg::BalanceVerdict::Unmeasurable && vb.rows[1].reason == dg::balance_reason::kVramBudget,
               "kural: butce yetmeyen satir Olculemedi (GPU siniri hukmu degil)");
    }

    // 2a) Uyumluluk B3: olcum penceresinde pencere simge durumundaydi -> olculen her adim Invalid(window_minimized),
    //     tekrar yok; olcum penceresi kancaya gecerli aralikla verilir. (WARP, ekran disi; pencere acilmaz.)
    bal::BalanceOptions om = o;
    om.timing_override = bal::StepTiming{0.05, 0.15};
    std::atomic<int> minimized_calls{0};
    std::atomic<bool> bad_window{false};
    om.window_minimized_during = [&](std::chrono::steady_clock::time_point b, std::chrono::steady_clock::time_point e) {
        ++minimized_calls;
        if (!(e > b)) bad_window = true;
        return true;
    };
    auto rm = std::make_unique<BalanceRun>();
    run_balance_once(om, false, 0, *rm);
    print_balance_table("warp-minimized", *rm);
    expect(rm->report.ok && rm->report.error.empty(), "simge durumu kosuyu bitirmez");
    int measured_ok = 0;
    for (const bal::StepResult& s : rm->report.steps) {
        expect(s.status == bal::StepStatus::Measured && s.attempts == 1, "simge durumundaki adim kosulur, tekrar edilmez");
        if (s.error.empty()) {
            ++measured_ok;
            expect(s.stats.state == dg::StepState::Invalid && s.stats.reason == dg::balance_reason::kWindowMinimized,
                   "simge durumunda olculen adim: gecersiz (window_minimized)");
        }
    }
    expect(minimized_calls.load() == measured_ok && !bad_window.load(), "kanca yalniz olculen denemede, gecerli olcum penceresiyle cagrilir");

    // 2b) Adim zaman butcesi: bir cozunurlugun adimi butcesini asarsa o adim "olculemedi (timeout)" olur, ayni
    //     cozunurlugun sonraki adimlari kosulmaz, diger adimlar surer; kullanici iptali sayilmaz.
    bal::BalanceOptions ot = o;
    ot.timing_override = bal::StepTiming{0.05, 0.15};
    ot.step_budget_override_s = 0.0;
    ot.step_budget_row = 0;
    auto rt = std::make_unique<BalanceRun>();
    run_balance_once(ot, false, 0, *rt);
    print_balance_table("warp-timeout", *rt);
    expect(rt->report.ok && !rt->report.cancelled && rt->report.error.empty(), "bir cozunurlugun zaman asimi kosuyu bitirmez, iptal sayilmaz");
    for (const bal::StepResult& s : rt->report.steps) {
        if (s.plan.row == 0) {
            expect(s.status == (s.plan.id == bal::StepId::S2 ? bal::StepStatus::TimedOut : bal::StepStatus::NotRun) && s.error == "timeout",
                   "butceyi asan adim TimedOut; ayni cozunurlugun sonraki adimlari kosulmaz");
            expect(s.stats.state == dg::StepState::Unmeasurable && s.stats.reason == dg::balance_reason::kTimeout && s.stats.fps == 0 &&
                       s.measured_frames == 0 && !s.bound_confirmed,
                   "zaman asimina ugrayan cozunurluk: olculemedi (timeout), deger yok");
        } else {
            expect(s.status == bal::StepStatus::Measured, "zaman asimi baska cozunurluklerin adimlarini etkilemez");
        }
    }
    expect(rt->progress_begin == bal::kStepCount - 2 && rt->progress_end == bal::kStepCount - 2, "kosulmayan iki adim icin bildirim yok");
    {
        dg::BalanceInputs in = rt->report.inputs;
        dg::StepStats ok;
        ok.state = dg::StepState::Valid, ok.fps = 100, ok.frames = 400, ok.seconds = 4, ok.gpu_busy_ratio = 0.1;
        in.cpu = in.cpu_repeat = ok;
        const dg::BalanceResult vt = dg::evaluate_balance(in);
        expect(vt.rows[0].verdict == dg::BalanceVerdict::Unmeasurable && vt.rows[0].reason == dg::balance_reason::kTimeout,
               "kural: zaman asimina ugrayan satir Olculemedi (timeout)");
    }
    // 2c) S1 butcesini asarsa kosu "timeout" ile biter (hicbir satir hukum alamaz; kalan adimlar kosulmaz).
    ot.step_budget_row = -1;
    auto rs = std::make_unique<BalanceRun>();
    run_balance_once(ot, false, 0, *rs);
    std::printf("balance: warp-timeout-s1 ok=%d cancelled=%d error=%s begin=%u end=%u seconds=%.2f\n", rs->report.ok, rs->report.cancelled,
                rs->report.error.c_str(), rs->progress_begin, rs->progress_end, rs->report.seconds);
    expect(!rs->report.ok && !rs->report.cancelled && rs->report.error == "timeout", "S1 zaman asimi: kosu timeout ile biter, iptal sayilmaz");
    expect(rs->report.step(bal::StepId::S1).status == bal::StepStatus::TimedOut && rs->progress_begin == 1 && rs->progress_end == 1,
           "S1 TimedOut; baska adim baslamadi");
    for (std::size_t i = 1; i < bal::kStepCount; ++i)
        expect(rs->report.steps[i].status == bal::StepStatus::NotRun && rs->report.steps[i].stats.state == dg::StepState::NotRun,
               "S1 zaman asimindan sonra kalan adimlar kosulmadi");
    for (const dg::BalanceRow& row : dg::evaluate_balance(rs->report.inputs).rows)
        expect(row.verdict == dg::BalanceVerdict::Undetermined, "S1 olculemedi -> hukum yok");

    // 3) Kosu ortasinda iptal: kismi adim atilir, kalanlar kosulmaz.
    //    Iptal 2,5 sn sonra gelir: ilk adim(lar) tamamlanmis olur (adim ~1 sn), dizi bitmemistir (dokuz adim >= 8,5 sn).
    const double cancel_after_s = 2.5;
    auto rc = std::make_unique<BalanceRun>();
    run_balance_once(o, preview_on, cancel_after_s, *rc);
    std::size_t measured = 0, cancelled = 0, not_run = 0;
    for (const bal::StepResult& s : rc->report.steps) {
        measured += s.status == bal::StepStatus::Measured;
        cancelled += s.status == bal::StepStatus::Cancelled;
        not_run += s.status == bal::StepStatus::NotRun;
    }
    std::printf("balance: warp-cancel ok=%d cancelled=%d measured=%zu cancelled_steps=%zu not_run=%zu seconds=%.2f\n", rc->report.ok,
                rc->report.cancelled, measured, cancelled, not_run, rc->report.seconds);
    expect(!rc->report.ok && rc->report.cancelled && rc->report.error.empty(), "iptal: kosu tamamlanmadi sayilir");
    expect(cancelled <= 1 && measured + cancelled + not_run == bal::kStepCount && not_run >= 1, "iptal: kalan adimlar kosulmadi");
    expect(measured >= 1, "iptalden once tamamlanan adimlar raporda kalir");
    expect(rc->report.seconds < cancel_after_s + 2.0, "iptal hizli doner");
    bool tail = false;  // ilk olculmeyen adimdan sonra olculmus adim olamaz (sira korunur)
    for (const bal::StepResult& s : rc->report.steps) {
        if (s.status == bal::StepStatus::Measured) {
            expect(!tail && s.measured_frames >= 2 && s.frame_ms > 0, "iptalden once tamamlanan adimin olcumu duruyor");
        } else {
            tail = true;
            expect(s.stats.state == dg::StepState::NotRun && s.stats.fps == 0 && s.measured_frames == 0 && !s.bound_confirmed,
                   "iptal edilen / kosulmayan adimda deger yok");
        }
    }

    // 3b) Kararsiz adim tekrari (islev denetimi): test secenegiyle her Invalid adim tekrar edilebilir sayilir. Kosu
    //     basina en cok kBalanceRunMaxRetries tekrar, adim basina en cok kBalanceStepMaxAttempts deneme; bildirim
    //     sayisi degismez (tekrar adimin ICINDEDIR), son deneme oldugu gibi kullanilir.
    {
        bal::BalanceOptions orr = o;
        orr.timing_override = bal::StepTiming{0.05, 0.15};
        orr.retry_any_invalid = true;
        auto rr = std::make_unique<BalanceRun>();
        run_balance_once(orr, false, 0, *rr);
        print_balance_table("warp-retry", *rr);
        std::uint32_t extra = 0;
        bool bounded = true;
        for (const bal::StepResult& s : rr->report.steps) {
            bounded = bounded && s.attempts >= 1 && s.attempts <= dg::kBalanceStepMaxAttempts;
            extra += s.attempts > 0 ? s.attempts - 1 : 0;
            expect(s.status == bal::StepStatus::Measured && s.stats.state == dg::StepState::Invalid, "tekrar edilen adim da kuralla degerlendirilir (esik degismez)");
        }
        expect(rr->report.ok && rr->report.error.empty() && !rr->report.cancelled, "tekrar kosuyu bitirmez");
        expect(bounded, "adim basina en cok iki deneme");
        expect(rr->report.retries == dg::kBalanceRunMaxRetries && extra == rr->report.retries, "kosu basina en cok iki tekrar; sayac denemelerle ayni");
        expect(rr->report.step(bal::StepId::T2).attempts == 1, "tekrar hakki bittikten sonra adimlar bir kez kosulur");
        expect(rr->progress_begin == bal::kStepCount && rr->progress_end == bal::kStepCount, "tekrar ek bildirim uretmez");
    }

    // 4) Yazilim bagdastiricisi izinsiz: kosu baslamaz (WARP puanlanmaz).
    bal::BalanceOptions on = o;
    on.allow_software = false;
    auto rn = std::make_unique<BalanceRun>();
    run_balance_once(on, false, 0, *rn);
    std::printf("balance: warp-refused ok=%d error=%s begin=%u\n", rn->report.ok, rn->report.error.c_str(), rn->progress_begin);
    expect(!rn->report.ok && rn->report.error == "no_hardware_adapter" && rn->progress_begin == 0, "yazilim bagdastiricisi: denge kosusu reddedilir");

    // 5) Genel testin sahnesi (scene_v3) ayni onizleme yolunu kullanir; onizleme sahnenin ciktisini ve kimligini
    //    degistirmez (ayni is yuku ozeti, ayni dogrulama pikseli).
    {
        std::atomic<bool> cancel{false};
        SceneConfig v3;
        v3.tier = Tier::Low;
        v3.width = 320, v3.height = 180;
        v3.warmup_frames = 2;
        v3.frames = 10;
        const auto plain = run_scene(v3, cancel, false);
        std::uint32_t got = 0, finals = 0;
        std::vector<std::uint8_t> last;
        const PreviewCallback cb = [&](const PreviewFrame& f) {
            if (copy_preview(f, last)) ++got;
            if (f.final_frame) ++finals;
        };
        FrameTap tap;
        SceneConfig with = v3;
        with.preview = &cb;
        with.frame_tap = &tap;
        const auto prev = run_scene(with, cancel, false);
        std::printf("balance: scene_v3 preview ok=%d/%d hash=%016llx/%016llx pix=%016llx/%016llx preview=%d/%d frames=%u (callback %u, final %u) "
                    "tap_frames=%llu\n",
                    plain.ok, prev.ok, static_cast<unsigned long long>(plain.workload_hash), static_cast<unsigned long long>(prev.workload_hash),
                    static_cast<unsigned long long>(plain.pixel_checksum), static_cast<unsigned long long>(prev.pixel_checksum), plain.preview,
                    prev.preview, prev.preview_frames, got, finals, static_cast<unsigned long long>(tap.frames.load()));
        expect(plain.ok && prev.ok && !plain.preview && plain.preview_frames == 0, "scene_v3: onizleme varsayilan kapali");
        expect(prev.preview && prev.preview_frames == got && got >= 1 && finals == 1, "scene_v3: onizleme kareleri ve adim sonu karesi");
        expect(plain.workload_hash == prev.workload_hash && plain.pixel_checksum == prev.pixel_checksum && plain.pixel_checksum != 0,
               "scene_v3: onizleme is yuku ozetini ve dogrulama pikselini degistirmez");
        expect(prev.measured_frames == v3.frames && prev.gpu_excluded == plain.gpu_excluded, "scene_v3: onizleme kare saymayi / zaman damgasi zincirini bozmaz");
        expect(last.size() == std::size_t{320} * 180 * 4, "scene_v3: onizleme boyutu");
        expect(tap.frames.load() == prev.frame_ms.size(), "scene_v3: eski panel sayaci yalniz olcum araliklarini sayar");
    }
    return g_fail ? 1 : 0;
}

// Gercek GPU: gercek cozunurlukler ve pencereler. ctest'e kayitli degil.
int run_balance_real(const std::wstring& out_dir, bool quick, bool preview_on, std::optional<bal::StepTiming> timing, int adapter) {
    bal::BalanceOptions o;
    o.quick = quick;
    o.timing_override = timing;
    o.hardware_adapter_index = adapter;  // -1 = urun secimi
    auto run = std::make_unique<BalanceRun>();
    ProbeChecks checks;
    run_balance_once(o, preview_on, 0, *run, &checks);
    const bal::BalanceReport& r = run->report;
    if (adapter >= 0 && r.error == "adapter_index") {
        std::printf("balance: real --adapter %d yok\n", adapter);
        return 2;
    }
    if (r.error == "no_hardware_adapter" || r.adapter.kind == RendererKind::None) {
        std::printf("balance: real skipped (%s)\n", r.error.c_str());
        return 77;
    }
    print_balance_table("real", *run);
    {
        // Spec C1 / C2 / A3 (salt okuma): uygulamadaki ayni yol. Degerler BU bilgisayarin olcumudur; okunamayan "-".
        const auto fw = ef::platform::diagnostics::count_processor_speed_limit_events_since_boot();
        const auto rc = bal::collect_run_checks<ef::platform::metrics::SteadyWindow>(
            r, checks.sampler, checks.other_gpu_pct, fw.ok ? std::optional<std::uint32_t>(fw.count) : std::nullopt);
        bal::apply_run_checks(run->report.inputs, rc);
        const auto num = [](const std::optional<double>& v, const char* fmt) {
            char b[32] = "-";
            if (v) std::snprintf(b, sizeof b, fmt, *v);
            return std::string(b);
        };
        const auto u32 = [](const std::optional<std::uint32_t>& v) { return v ? std::to_string(*v) : std::string("-"); };
        std::printf("balance: real checks adapter_luid=%s before_steps=%s other_gpu_pct=%s (esik %.0f) retries=%u\n",
                    r.adapter.luid.empty() ? "-" : r.adapter.luid.c_str(), yes_no(checks.before_steps_called), num(rc.other_gpu_pct, "%.1f").c_str(),
                    dg::kBalanceOtherGpuMaxPct, rc.retries);
        std::printf("balance: real checks cpu_speed samples=%zu measured=%s perf_first=%s%% perf_last=%s%% drop_ratio=%s (esik > %.2f) limit_min=%s%% "
                    "drop=%s limited=%s fw_limit_events_since_boot=%s%s\n",
                    checks.sampler.cpu_sample_count(), yes_no(rc.cpu_speed.measured), num(rc.cpu_speed.first_pct, "%.1f").c_str(),
                    num(rc.cpu_speed.last_pct, "%.1f").c_str(), num(rc.cpu_speed.drop_ratio, "%.3f").c_str(), dg::kCpuSpeedDropRatio,
                    num(rc.cpu_speed.min_limit_pct, "%.0f").c_str(), yes_no(rc.cpu_speed.drop), yes_no(rc.cpu_speed.limited),
                    fw.ok ? std::to_string(fw.count).c_str() : "-", fw.capped ? "+" : "");
        if (rc.pcie) {
            const dg::PcieLoadFinding pf = dg::evaluate_pcie_under_load(*rc.pcie);
            std::printf("balance: real checks pcie reads=%zu link=\"%s\" slot_max=x%s/Gen%s gpu_load=%s%% measured_under_load=%s below_max=%s class=%s\n",
                        checks.sampler.pcie_read_count(), dg::pcie_link_text(*rc.pcie).c_str(), u32(rc.pcie->slot_max_width).c_str(),
                        u32(rc.pcie->slot_max_gen).c_str(), num(rc.pcie->gpu_load_pct, "%.1f").c_str(), yes_no(pf.measured_under_load),
                        yes_no(pf.below_max()), std::string(dg::to_string(pf.platform.cls)).c_str());
        } else {
            std::printf("balance: real checks pcie reads=%zu link=- (S3 olcum penceresinde okuma yok ya da aygit bulunamadi)\n",
                        checks.sampler.pcie_read_count());
        }
        expect(checks.before_steps_called, "ilk adimdan once arka plan GPU ornegi alindi");
        expect(rc.retries == r.retries, "tekrar sayisi kosu kosuluna tasindi");
    }
    expect(r.ok && !r.cancelled && r.error.empty() && !r.software, "gercek GPU: dokuz adim da denendi");
    for (std::size_t i = 0; i < bal::kStepCount; ++i) {
        const bal::StepResult& s = r.steps[i];
        if (s.status == bal::StepStatus::Unmeasurable) {
            std::printf("balance: real %s olculemedi (%s); diger adimlar surdu\n", std::string(bal::to_string(s.plan.id)).c_str(), s.error.c_str());
            continue;
        }
        expect(s.status == bal::StepStatus::Measured, "gercek GPU adimi kosuldu");
        if (s.status != bal::StepStatus::Measured) continue;
        {
            std::string ps;
            for (const double f : s.second_fps) {
                char b[24];
                std::snprintf(b, sizeof b, " %.0f", f);
                ps += b;
            }
            std::printf("balance: real %s persec%s\n", std::string(bal::to_string(s.plan.id)).c_str(), ps.c_str());
        }
        if (s.stats.state != dg::StepState::Valid)
            std::printf("balance: real %s GECERSIZ (%s)\n", std::string(bal::to_string(s.plan.id)).c_str(), std::string(s.stats.reason).c_str());
        if (s.bound_confirmed && !*s.bound_confirmed)
            std::printf("balance: real %s SINIR DOGRULANMADI (gpu_busy=%.3f): deger \"en az\" sayilir\n",
                        std::string(bal::to_string(s.plan.id)).c_str(), s.stats.gpu_busy_ratio.value_or(-1));
        expect(run->taps[i].seen && run->taps[i].measure_live, "canli sayilar olcumde beslendi");
        if (!timing) expect(run->taps[i].warm_live, "canli sayilar isinmada da beslendi");
    }
    print_balance_verdicts("real", r, true);
    print_taps("real", *run);
    if (preview_on) check_previews("real", *run, out_dir, quick ? L"balance_real_quick" : L"balance_real_full");
    else expect(!r.preview, "onizleme kapali: kosul kapali");
    return g_fail ? 1 : 0;
}

// --cpu-cores: surecin benzesim maskesini, izinli mantiksal cekirdeklerin ilk n tanesine indirir.
bool limit_cpu_cores(unsigned n) {
    DWORD_PTR proc = 0, sys = 0;
    if (n == 0 || !GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys)) return false;
    DWORD_PTR mask = 0;
    unsigned taken = 0;
    for (unsigned b = 0; b < sizeof(DWORD_PTR) * 8 && taken < n; ++b)
        if (proc & (DWORD_PTR{1} << b)) {
            mask |= DWORD_PTR{1} << b;
            ++taken;
        }
    if (taken < n) {
        std::printf("cpu_cores: %u istendi, surecin izinli cekirdegi %u\n", n, taken);
        return false;
    }
    if (!SetProcessAffinityMask(GetCurrentProcess(), mask)) return false;
    std::printf("cpu_cores: %u (benzesim maskesi 0x%llx; yalniz bu surec)\n", taken, static_cast<unsigned long long>(mask));
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    bool real = false, balance = false, quick = false, preview_on = true;
    std::optional<bal::StepTiming> timing;
    std::wstring out_dir;
    unsigned cpu_cores = 0;  // 0 = kisit yok
    int adapter = -1;        // --adapter N (yalniz --balance --real): yuksek performans sirasindaki N. donanim bagdastiricisi
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--list-adapters") == 0) {
            // Uyumluluk B9 (real_gpu ctest): salt okuma DXGI listesi; ilk satir sayi, sonra dizin + ad.
            const auto names = ef::benchmark::hardware_adapter_names();
            std::printf("adapters=%zu\n", names.size());
            for (std::size_t k = 0; k < names.size(); ++k) std::wprintf(L"adapter %zu \"%ls\"\n", k, names[k].c_str());
            return 0;
        }
        if (std::wcscmp(argv[i], L"--adapter") == 0 && i + 1 < argc) {
            wchar_t* end = nullptr;
            const unsigned long n = std::wcstoul(argv[++i], &end, 10);
            if (!end || *end != L'\0' || n > 16) {
                std::printf("arena_probe: --adapter gecersiz\n");
                return 2;
            }
            adapter = static_cast<int>(n);
            continue;
        }
        if (std::wcscmp(argv[i], L"--real") == 0) real = true;
        else if (std::wcscmp(argv[i], L"--balance") == 0) balance = true;
        else if (std::wcscmp(argv[i], L"--quick") == 0) quick = true;
        else if (std::wcscmp(argv[i], L"--no-preview") == 0) preview_on = false;
        else if (std::wcscmp(argv[i], L"--timing") == 0 && i + 1 < argc) {
            wchar_t* end = nullptr;
            const double w = std::wcstod(argv[++i], &end);
            const double m = end && *end == L',' ? std::wcstod(end + 1, nullptr) : 0.0;
            if (w >= 0 && m > 0) timing = bal::StepTiming{w, m};
        } else if (std::wcscmp(argv[i], L"--out") == 0 && i + 1 < argc) out_dir = argv[++i];
        else if (std::wcscmp(argv[i], L"--cpu-cores") == 0 && i + 1 < argc) {
            wchar_t* end = nullptr;
            const unsigned long n = std::wcstoul(argv[++i], &end, 10);
            if (!end || *end != L'\0' || n == 0 || n > 64) {
                std::printf("arena_probe: --cpu-cores gecersiz\n");
                return 2;
            }
            cpu_cores = static_cast<unsigned>(n);
        }
    }
    if (cpu_cores > 0 && !limit_cpu_cores(cpu_cores)) return 2;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    int rc = 0;
    if (adapter >= 0 && !(balance && real)) {
        std::printf("arena_probe: --adapter yalniz --balance --real ile\n");
        CoUninitialize();
        return 2;
    }
    if (balance) rc = real ? run_balance_real(out_dir, quick, preview_on, timing, adapter) : run_balance_warp(out_dir, preview_on, timing);
    else rc = real ? run_real(out_dir) : run_warp(out_dir);
    CoUninitialize();
    std::printf("arena_probe: %s\n", rc == 0 ? "ok" : (rc == 77 ? "skipped" : "FAILED"));
    return rc;
}
