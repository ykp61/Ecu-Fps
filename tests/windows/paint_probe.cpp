// Gercek pencere + ID2D1DCRenderTarget yolunun olcumu (WM_PAINT sayaci; WIC snapshot suresi kanit sayilmaz).
// Kontroller: bos pencere yok, yeniden boyutlandirma/DPI, simge durumu ve gizliyken cizim yok, GDI/USER sizintisi yok.
// Pencere etkinlestirilmez ve z-sirasinin en altina konur. Cikis: 0 gecti, 1 hata, 2 altyapi.
#include <windows.h>
#include <psapi.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "ui/shell/main_window.h"
#include "ui/theme/theme.h"

using namespace ef::ui;

namespace {

void pump(int ms) {
    const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(ms);
    MSG msg;
    while (GetTickCount64() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(2);
    }
}

// Gercek pencere iceriginden farkli renk sayisi: bos/tek renk kare regresyonunu yakalar.
int distinct_samples(HWND h) {
    RECT rc{};
    GetClientRect(h, &rc);
    const int w = rc.right, ht = rc.bottom;
    if (w <= 0 || ht <= 0) return 0;
    HDC wdc = GetDC(h);
    HDC mem = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, w, ht);
    HGDIOBJ old = SelectObject(mem, bmp);
    PrintWindow(h, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
    COLORREF seen[32];
    int n = 0;
    for (int y = 8; y < ht; y += ht / 24 + 1)
        for (int x = 8; x < w; x += w / 24 + 1) {
            const COLORREF c = GetPixel(mem, x, y);
            bool dup = false;
            for (int i = 0; i < n; ++i) dup = dup || seen[i] == c;
            if (!dup && n < 32) seen[n++] = c;
        }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(h, wdc);
    return n;
}

double cpu_ms() {
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    const auto v = [](FILETIME f) { return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return static_cast<double>(v(k) + v(u)) / 10000.0;
}

std::size_t private_kib() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
    return pmc.PrivateUsage / 1024;
}

// Istemci alaninin pikselleri (PrintWindow; pencere arkada olsa da kendi icerigi). Bos -> okunamadi.
std::vector<std::uint32_t> client_pixels(HWND h) {
    RECT rc{};
    GetClientRect(h, &rc);
    const int w = rc.right, ht = rc.bottom;
    std::vector<std::uint32_t> px;
    if (w <= 0 || ht <= 0) return px;
    HDC wdc = GetDC(h);
    HDC mem = CreateCompatibleDC(wdc);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -ht;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        if (PrintWindow(h, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT)) {
            GdiFlush();
            px.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(ht));
            std::memcpy(px.data(), bits, px.size() * 4);
            for (auto& v : px) v &= 0x00FFFFFFu;  // alfa bayti tanimsiz
        }
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(h, wdc);
    return px;
}

// Kodla uretilmis onizleme karesi (canli test gorunumu): kayan seritler. Gercek sahne degildir.
ef::core::games::ImageRef live_image(std::uint32_t w, std::uint32_t ht, int phase) {
    auto img = std::make_shared<ef::core::games::ImagePixels>();
    img->w = w;
    img->h = ht;
    img->bgra.resize(static_cast<std::size_t>(w) * ht * 4);
    for (std::uint32_t y = 0; y < ht; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            std::uint8_t* p = img->bgra.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            const float v = 0.5f + 0.5f * std::sin(static_cast<float>(x + y) * 0.05f + static_cast<float>(phase) * 0.9f);
            p[0] = static_cast<std::uint8_t>(40.f + 160.f * v);
            p[1] = static_cast<std::uint8_t>(20.f + 200.f * (1.f - v));
            p[2] = static_cast<std::uint8_t>((x * 255u) / w);
            p[3] = 255;
        }
    return img;
}

ef::core::measure::LiveSample live_sample(int phase) {
    ef::core::measure::LiveSample s;
    s.preview = live_image(960, 540, phase);
    s.phase = ef::core::measure::LivePhase::Measure;
    s.step_index = 5, s.step_total = 9, s.step_code = 5, s.width = 1920, s.height = 1080;
    s.has_numbers = true;
    s.fps = 120.0 + phase;
    s.cpu_ms = 4.0 + 0.3 * (phase % 8);
    s.gpu_ms = 7.5;
    s.ring_count = 24;
    for (std::size_t i = 0; i < s.ring_count; ++i) s.fps_ring[i] = static_cast<float>(118.0 + static_cast<double>((i * 7 + static_cast<std::size_t>(phase)) % 9));
    return s;
}

void size_to(HWND h, int w, int ht) {
    SetWindowPos(h, HWND_BOTTOM, 40, 40, w, ht, SWP_NOACTIVATE);
    UpdateWindow(h);
    pump(120);
}

}  // namespace

int wmain() {
    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    if (!cat.ok()) return 2;
    MainWindow::Options o;
    o.window_class = L"EcuFps.PaintProbe";
    o.title = L"Ecu Fps paint probe";
    o.quit_on_destroy = false;
    auto w = MainWindow::create(GetModuleHandleW(nullptr), default_theme(), cat.value(), o);
    if (!w) return 2;
    const HWND h = w->hwnd();
    int failures = 0;
    ViewModel vm;
    vm.version = "0";
    w->set_view(vm);
    ShowWindow(h, SW_SHOWNOACTIVATE);
    size_to(h, 1280, 720);
    pump(400);
    const DWORD gdi0 = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), user0 = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    const std::size_t mem0 = private_kib();

    if (distinct_samples(h) < 3) {
        std::printf("blank window at open\n");
        ++failures;
    }
    auto measure = [&](const char* label, int rounds) {
        w->reset_paint_stats();
        for (int i = 0; i < rounds; ++i) {
            InvalidateRect(h, nullptr, FALSE);
            UpdateWindow(h);
            pump(4);
        }
        const auto s = w->paint_stats();
        std::printf("paint %s: n=%llu avg_ms=%.2f max_ms=%.2f\n", label, s.count, s.count ? s.total_ms / static_cast<double>(s.count) : 0.0, s.max_ms);
        if (s.count == 0) {
            std::printf("%s: no WM_PAINT reached the DC path\n", label);
            ++failures;
        }
        return s;
    };
    measure("1280x720", 60);
    size_to(h, 2560, 1440);
    measure("2560x1440", 30);
    for (const auto [cw, ch] : {std::pair{1000, 720}, {960, 540}, {1800, 1100}, {1280, 720}}) {
        size_to(h, cw, ch);
        if (distinct_samples(h) < 3) {
            std::printf("blank window after resize %dx%d\n", cw, ch);
            ++failures;
        }
    }
    // DPI degisimi: pencere ayni yoldan yeniden olceklenir, bos kare kalmaz.
    RECT r{};
    GetWindowRect(h, &r);
    SendMessageW(h, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&r));
    pump(150);
    if (distinct_samples(h) < 3) {
        std::printf("blank window after DPI 144\n");
        ++failures;
    }
    SendMessageW(h, WM_DPICHANGED, MAKEWPARAM(96, 96), reinterpret_cast<LPARAM>(&r));
    pump(150);

    // ---- Canli test gorunumu (spec 2026-11-12 A4): gercek pencere + DC alt dikdortgen yolu. GPU yuku yok: kareler
    // kodla uretilir. Denetimler: kosu canli ekranla basladiginda pencere Olcumler sayfasina gecer ve gorunum
    // ekrandadir; yeni kare yalniz canli bolgeyi yeniden cizer (hizli yol); hizli yolun sonucu ayni ornekle yapilan tam
    // cizimle piksel piksel aynidir; Esc mevcut durdurma yolunu cagirir; kosu bitince sayfa geri gelir. ----
    {
        size_to(h, 1280, 720);
        int stops = 0;
        w->on_test_stop = [&stops] { ++stops; };
        ef::core::measure::MeasureState ms;
        ms.builtin_available = true;
        ms.scope = ef::core::measure::BuiltinScope::Balance;
        ms.phase = ef::core::measure::TestPhase::Running;
        ms.stage_key = "measure.stage_balance_k1";
        ms.elapsed_s = 21;  // started_ms = 0: gecen sure sabit (goruntu karsilastirmasi saniyeyle degismez)
        ms.live_view.active = true;
        w->set_measure(ms);
        pump(300);
        if (!w->live_view_visible()) {
            std::printf("live view not visible after a run started with it\n");
            ++failures;
        }
        w->set_live_test(live_sample(0));
        pump(400);
        w->set_live_test(live_sample(0));  // yuk cubugu gecisi bitti: son hal cizilir
        pump(150);
        const auto a = client_pixels(h);
        w->reset_paint_stats();
        w->set_live_test(live_sample(3));
        pump(400);
        w->set_live_test(live_sample(3));
        pump(150);
        const auto fast_stats = w->paint_stats();
        const auto b = client_pixels(h);
        InvalidateRect(h, nullptr, FALSE);  // ayni ornekle tam cizim
        UpdateWindow(h);
        pump(150);
        const auto c = client_pixels(h);
        std::size_t changed = 0, differs = 0;
        if (a.size() == b.size())
            for (std::size_t i = 0; i < a.size(); ++i) changed += a[i] != b[i] ? 1u : 0u;
        if (b.size() == c.size())
            for (std::size_t i = 0; i < b.size(); ++i) differs += b[i] != c[i] ? 1u : 0u;
        if (a.empty() || a.size() != b.size() || b.size() != c.size()) {
            std::printf("live view: window pixels could not be read\n");
            ++failures;
        } else {
            if (fast_stats.live != fast_stats.count || fast_stats.live == 0) {
                std::printf("live view: a new frame did not take the region-only paint path (paints=%llu region=%llu)\n", fast_stats.count, fast_stats.live);
                ++failures;
            }
            if (changed < a.size() / 20) {
                std::printf("live view: new preview frame not visible (changed pixels %zu of %zu)\n", changed, a.size());
                ++failures;
            }
            if (differs != 0) {
                std::printf("live view: region-only paint differs from the full paint (%zu pixels)\n", differs);
                ++failures;
            }
        }
        // Maliyet: hizli yol (yalniz canli bolge) ve ayni icerigin tam cizimi.
        w->reset_paint_stats();
        for (int i = 0; i < 60; ++i) {
            w->set_live_test(live_sample(i));
            UpdateWindow(h);
            pump(2);
        }
        const auto fs = w->paint_stats();
        const auto full = measure("live_full_1280x720", 30);
        std::printf("paint live_region 1280x720: n=%llu region=%llu avg_ms=%.2f max_ms=%.2f (full avg_ms=%.2f) changed_px=%zu\n", fs.count, fs.live,
                    fs.count ? fs.total_ms / static_cast<double>(fs.count) : 0.0, fs.max_ms, full.count ? full.total_ms / static_cast<double>(full.count) : 0.0, changed);
        if (fs.live < 55) {
            std::printf("live view: region-only paints %llu of 60 frames\n", fs.live);
            ++failures;
        }
        // Kesirli olcek (%125): canli bolgenin ust kenari piksel sinirina denk gelmez; hizli yol yine tam cizimle ayni olmali.
        {
            RECT wr{};
            GetWindowRect(h, &wr);
            SendMessageW(h, WM_DPICHANGED, MAKEWPARAM(120, 120), reinterpret_cast<LPARAM>(&wr));
            pump(250);
            w->set_live_test(live_sample(5));
            pump(400);
            w->reset_paint_stats();
            w->set_live_test(live_sample(5));
            pump(150);
            const auto region_paints = w->paint_stats().live;
            const auto b2 = client_pixels(h);
            InvalidateRect(h, nullptr, FALSE);
            UpdateWindow(h);
            pump(150);
            const auto c2 = client_pixels(h);
            std::size_t differs2 = b2.size() == c2.size() ? 0 : 1;
            if (b2.size() == c2.size())
                for (std::size_t i = 0; i < b2.size(); ++i) differs2 += b2[i] != c2[i] ? 1u : 0u;
            if (b2.empty() || region_paints == 0 || differs2 != 0) {
                std::printf("live view at 125%%: region-only paint differs from the full paint (%zu pixels, region paints %llu)\n", differs2, region_paints);
                ++failures;
            }
            SendMessageW(h, WM_DPICHANGED, MAKEWPARAM(96, 96), reinterpret_cast<LPARAM>(&wr));
            pump(250);
        }
        // Esc: mevcut durdurma yolu (odak gerekmez; mesaj dogrudan pencereye).
        SendMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0);
        if (stops != 1) {
            std::printf("live view: Esc did not call the stop path (calls=%d)\n", stops);
            ++failures;
        }
        // Simge durumunda yeni kare cizim uretmez ve gorunum "ekranda" sayilmaz (onizleme kopyalanmaz).
        ShowWindow(h, SW_SHOWMINNOACTIVE);
        pump(250);
        w->reset_paint_stats();
        for (int i = 0; i < 10; ++i) {
            w->set_live_test(live_sample(i));
            pump(20);
        }
        if (w->paint_stats().count != 0 || w->live_view_visible()) {
            std::printf("live view: painted while minimized (%llu)\n", w->paint_stats().count);
            ++failures;
        }
        ShowWindow(h, SW_SHOWNOACTIVATE);
        pump(250);
        // Kosu bitti: gorunum kapanir, Olcumler sayfasi geri gelir; Esc artik testi durdurmaz. Onizlemenin hedef
        // bitmap'i (960x540) kosu bitince hemen birakilir; bosta onbellekte beklemez.
        w->set_live_test(live_sample(1));
        pump(150);
        const std::size_t cached_live = w->image_cache_size();
        ms.phase = ef::core::measure::TestPhase::Cancelled;
        ms.live_view.active = false;
        w->set_measure(ms);
        if (cached_live == 0 || w->image_cache_size() != 0) {
            std::printf("live view: preview bitmap kept after the run ended (cached during=%zu after=%zu)\n", cached_live, w->image_cache_size());
            ++failures;
        }
        pump(250);
        SendMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0);
        if (w->live_view_visible() || stops != 1) {
            std::printf("live view: still active after the run ended (stops=%d)\n", stops);
            ++failures;
        }
        if (distinct_samples(h) < 3) {
            std::printf("blank window after the live view closed\n");
            ++failures;
        }
        w->on_test_stop = nullptr;
    }

    // Simge durumu / gizli: cizim yok (WM_PAINT sayaci artmaz).
    ShowWindow(h, SW_SHOWMINNOACTIVE);
    pump(250);
    w->reset_paint_stats();
    pump(1500);
    const auto mini = w->paint_stats().count;
    ShowWindow(h, SW_SHOWNOACTIVATE);
    pump(200);
    if (distinct_samples(h) < 3) {
        std::printf("blank window after restore\n");
        ++failures;
    }
    ShowWindow(h, SW_HIDE);
    pump(250);
    w->reset_paint_stats();
    const double cpu_a = cpu_ms();
    pump(3000);
    const auto hidden = w->paint_stats().count;
    const double hidden_cpu = cpu_ms() - cpu_a;
    ShowWindow(h, SW_SHOWNOACTIVATE);
    pump(300);
    w->reset_paint_stats();
    const double cpu_b = cpu_ms();
    pump(3000);
    const auto idle_paints = w->paint_stats().count;
    const double idle_cpu = cpu_ms() - cpu_b;
    if (mini != 0 || hidden != 0) {
        std::printf("painted while invisible: minimized=%llu hidden=%llu\n", mini, hidden);
        ++failures;
    }
    // Sizinti: 300 yeniden cizim sonrasi GDI/USER artmaz. Taban dongu hemen oncesi: pencere acilisindaki tek seferlik
    // sistem nesneleri (ornek dis UIA istemcisi) cizim sizintisi degildir; pencere yasam dongusunu ui_lifecycle olcer.
    const DWORD gdi_loop0 = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), user_loop0 = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int i = 0; i < 300; ++i) {
        InvalidateRect(h, nullptr, FALSE);
        UpdateWindow(h);
        pump(1);
    }
    pump(100);
    const DWORD gdi1 = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), user1 = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    const std::size_t mem1 = private_kib();
    std::printf("paint_probe: minimized_paints=%llu hidden_paints=%llu idle_paints_3s=%llu hidden_cpu_ms=%.1f idle_cpu_ms=%.1f gdi %lu->%lu user %lu->%lu private_kib %zu->%zu failures=%d\n",
                mini, hidden, idle_paints, hidden_cpu, idle_cpu, gdi0, gdi1, user0, user1, mem0, mem1, failures);
    if (gdi1 > gdi_loop0 + 2 || user1 > user_loop0 + 2) {
        std::printf("GDI/USER grew across repaints: gdi %lu->%lu user %lu->%lu\n", gdi_loop0, gdi1, user_loop0, user1);
        ++failures;
    }
    w.reset();
    return failures == 0 ? 0 : 1;
}
