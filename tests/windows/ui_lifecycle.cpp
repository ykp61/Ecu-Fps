// UI-05 kabul: pencere kapaninca UI kaynaklari serbest kalir. Ayni surecte pencere 200 kez
// olusturulup cizilir ve yok edilir (tepsiden ac/kapat senaryosu); GDI/USER nesne sayisi, surec
// yigini ve ozel bellek isinmadan sonra buyumemeli. Simge durumunda/gizliyken gorunurluk durumu
// dogru raporlanmali.
// Cikis: 0 gecti, 1 sizinti/hata, 2 altyapi.
//
// Kullanim: ef_ui_lifecycle [tur] [--trace] [--leak-heap=BAYT] [--leak-virt=BAYT] [--leak-gdi[=N]] [--lazy-step]
//   --lazy-step: tek seferlik +7 GDI (sizinti degil; testin GECMESI beklenir).
//   --leak-heap=932576: 932576 bayt sinifinda her turda 1 blok sizdirir; "LEAK_DETECTED pool" beklenir (ctest
//   windows.ui_lifecycle_detects_pool_leak). Normal kosuda sinif sayisi ust zarf <= kPoolCeiling, alt zarf farki <= 4.
//   --leak-* yalniz sizintiyi yakalama yetenegini kanitlamak icindir: her turda bilerek sizdirir ve
//   testin 1 (sizinti) ile cikmasi ve "LEAK_DETECTED ..." satirini yazmasi beklenir (ctest
//   windows.ui_lifecycle_detects_heap_leak / _private_leak: yalniz bu satira bakar).
//
// Bellek olcumu (neden bu metrikler): pencere kapandiktan sonra surec yiginindaki CANLI bayt turdan tura
// +-3 MiB oynar. Neden: yok edilen pencereden sonra 932576 baytlik (~911 KiB) buyuk bloklarin ADEDI turlar arasi
// 3..23 arasinda rastgele degisir (blok icinde uc iterator + sayac yapisi; 400 ms beklemeyle azalmaz, bir sonraki
// turlarda duser/artar) - sinirli ama zamanlamaya bagli bir tampon havuzu. Ayrica Direct2D HwndRenderTarget
// surucu belleginin (yigin disi) serbest kalmasi eszamansiz/basamaklidir. Tek ornek karsilastirmasi bu yuzden
// kararsizdi. Burada:
//   1) Yigin, HeapWalk ile iki sinifa ayrilir: kucuk bloklar (< 128 KiB) toplam bayt (kararli; +-0,4 MiB) ve
//      buyuk bloklarin ADEDI (rastgele havuz; yalniz alt zarfi sizinti sayilir).
//   2) Ozel bayt = surec ozel baytlari - buyuk yigin bloklari (havuz gurultusu ayiklanir; geriye surucu/VirtualAlloc/
//      kutuphane bellegi kalir). Tek ornek degil, esit uzunlukta iki pencerenin EN KUCUGU (alt zarf) karsilastirilir.
//      Eszamansiz yuksek ornekler alt zarfi etkilemez; gercek sizinti alt zarfi yukari iter.
//   3) GDI/USER nesne sayisi kesin sayimdir; ancak sistem kaynakli tek seferlik bir basamak (+7 GDI/+3 USER,
//      ~1/20 kosuda taban aninin onune/arkasina denk gelir) sizinti degildir. Bu yuzden "toplam artis" yerine
//      isinmadan sonra sayinin ARTTIGI TUR sayisi sayilir: pencere sizintisi turlarla tekrarlar.

#include <windows.h>
#include <malloc.h>
#include <psapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "ui/shell/main_window.h"
#include "ui/theme/theme.h"

using namespace ef::ui;

namespace {

// Esikler: 200 tur x ~0,1 sn. Pencereler: isinma = ilk yarisi; A = isinmadan sonraki ilk W tur,
// B = son W tur (W = tur/5), merkezleri ~60 tur arayla.
// Kalibrasyon (birlesik kod, 16 kosu x 200 tur): kucuk yigin farki -105..+434 KiB; buyuk blok adedi farki -1..+3;
// ozel bayt (buyuk bloklar haric) farki +78..+651 KiB. Esikler en kotu gozlemin ~2-3 kati.
constexpr long long kHeapToleranceBytes = 1024 * 1024;        // kucuk (<128 KiB) yigin bloklari, alt zarf (~17 KiB/tur sizinti yakalanir)
constexpr long long kBigBlockTolerance = 8;                    // buyuk yigin bloklari adedi, alt zarf (~1 buyuk blok / 7 tur)
constexpr int kMaxObjectRises = 3;   // isinmadan sonra GDI/USER sayisinin arttigi tur sayisi ust siniri
constexpr int kMaxObjectStep = 32;   // toplam GDI/USER artisi ust siniri (tek seferlik sistem basamagi icin)
// 932576 bayt (~911 KiB) sinifi (P9-B9): pencere kapaninca kalan ve turlar arasi sinirli oynayan tampon havuzu. Sayim
// [kPoolLo, kPoolHi] araligindaki canli bloklardir (debug CRT basligi ve yigin dolgusu bu sinifin %1'inden kucuktur).
// Iki ayri sinir: (a) en kotu tur (ust zarf) kPoolCeiling'i asmaz = havuz sinirsiz buyumez; (b) alt zarf (son pencerenin
// EN KUCUGU - ilk pencerenin EN KUCUGU) kPoolEnvelopeTolerance'i asmaz = "yavas tutma": her turda bir blok kalsaydi alt
// zarf turla birlikte yukselirdi. Kalibrasyon: artifacts/evidence/P9-flake-50.log ve artifacts/ui-performance.md.
constexpr std::size_t kPoolLo = 923000;
constexpr std::size_t kPoolHi = 942000;
constexpr long long kPoolCeiling = 40;
// T8 (2026-11-20) yeniden olcum, debug, tek basina: 10 kosu alt zarf farki -1..+1 (ust zarf 12..24); ef_unit_tests CPU
// yuku altinda 4 kosu -1..+2; onceki derleme-yuku kosusunda bir kez +3 (docs/audit/2026-11-18-puan-tablosu-p9-sonrasi.md).
// Eski sinir 2 yuk altindaki dogal gurultunun icindeydi (kararsizlik nedeni). Sinir 4 = gozlenen en kotu +3'un bir ustu.
// Algilama tabani: pencereler A=[100,140) B=[160,200) arasi ~60 tur; >= 1 blok / ~15 tur sizinti alt zarfi > 4 yapar
// (her turda 1 blok: ~+60, ctest windows.ui_lifecycle_detects_pool_leak). Ust zarf siniri (40) degismedi.
constexpr long long kPoolEnvelopeTolerance = 4;
constexpr long long kPrivateToleranceBytes = 2 * 1024 * 1024;  // ozel bayt - buyuk yigin bloklari, alt zarf (~33 KiB/tur sizinti yakalanir)

void pump(int ms) {
    const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(ms);
    MSG msg;
    while (GetTickCount64() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(5);
    }
}

// Determinizm: sabit sureli pump yuk altinda (paralel ctest) tam bir kare cizdirmeyebilir; o zaman D2D/DirectWrite/UIA
// onbellekleri taban turundan SONRA isinir ve buyume sizinti gibi gorunur. Bu yuzden her turda tam bir WM_PAINT
// zorlanir ve paint_stats().count ile dogrulanir (sure siniri yalniz takilmaya karsi; zaman asimi basarisizliktir).
bool pump_until_painted(MainWindow& w, int timeout_ms) {
    const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
    MSG msg;
    while (w.paint_stats().count < 1) {
        RedrawWindow(w.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (GetTickCount64() >= end) return false;
        if (w.paint_stats().count < 1) Sleep(5);
    }
    return true;
}

// Kuyruk bosalana kadar dagit (sabit sure beklemek yerine): geciken kMsgVisibility / WM_NCDESTROY gibi iletiler
// sonraki turun olcumune kaymaz.
void drain_queue() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

std::size_t private_bytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
    return pmc.PrivateUsage;
}

// Uygulama pencere kapaninca _heapmin() cagirir (src/app/app.cpp); ayni kosulu olcumden once yansitiriz ki serbest
// ama isletim sistemine donmemis heap sayfalari "ozel bellek" sayilmasin.
std::size_t private_bytes_settled() {
    drain_queue();
    _heapmin();
    return private_bytes();
}

// Tum surec yiginlarinda CANLI (ayrilmis) bloklar: kucuk (< kBigBlock) toplam bayt ve buyuk blok bayt/adedi.
// HeapWalk yigini kilitler; kilit altinda ayirim yapan cagri (printf vb.) yapma.
constexpr std::size_t kBigBlock = 128 * 1024;
struct HeapStats {
    std::size_t small_bytes = 0;
    std::size_t big_bytes = 0;
    std::size_t big_count = 0;
    std::size_t pool_count = 0;  // [kPoolLo, kPoolHi] baytlik (932576 sinifi) canli bloklar
};

HeapStats heap_stats() {
    HANDLE heaps[64];
    const DWORD n = GetProcessHeaps(64, heaps);
    HeapStats st;
    for (DWORD i = 0; i < n && i < 64; ++i) {
        if (!HeapLock(heaps[i])) continue;
        PROCESS_HEAP_ENTRY e{};
        while (HeapWalk(heaps[i], &e)) {
            if (!(e.wFlags & PROCESS_HEAP_ENTRY_BUSY)) continue;
            if (e.cbData >= kBigBlock) {
                st.big_bytes += e.cbData;
                ++st.big_count;
                if (e.cbData >= kPoolLo && e.cbData <= kPoolHi) ++st.pool_count;
            } else {
                st.small_bytes += e.cbData;
            }
        }
        HeapUnlock(heaps[i]);
    }
    return st;
}

std::size_t window_min(const std::vector<std::size_t>& v, int from, int to) {  // [from, to)
    return *std::min_element(v.begin() + from, v.begin() + to);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    int rounds = 200;
    bool trace = false, lazy_step = false;
    int leak_gdi = 0;
    std::size_t leak_heap = 0, leak_virt = 0;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--trace") trace = true;
        else if (a == L"--leak-gdi") leak_gdi = 1;
        else if (a.rfind(L"--leak-gdi=", 0) == 0) leak_gdi = _wtoi(a.c_str() + 11);  // her N turda bir
        else if (a == L"--lazy-step") lazy_step = true;  // tek seferlik, sizinti OLMAYAN +7 GDI artisi (esik testi: gecmeli)
        else if (a.rfind(L"--leak-heap=", 0) == 0) leak_heap = static_cast<std::size_t>(_wtoi(a.c_str() + 12));
        else if (a.rfind(L"--leak-virt=", 0) == 0) leak_virt = static_cast<std::size_t>(_wtoi(a.c_str() + 12));
        else if (_wtoi(a.c_str()) > 0) rounds = _wtoi(a.c_str());
    }
    if (rounds < 50) return 2;  // alt zarf pencereleri icin yeterli ornek gerekir
    const int warm = rounds / 2;
    const int win = rounds / 5;

    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    if (!cat.ok()) return 2;
    const auto& theme = default_theme();
    DWORD gdi0 = 0, user0 = 0, prev_g = 0, prev_u = 0;
    int object_rises = 0;
    int failures = 0;
    // Tur sonu ornekleri (pencere yok edilmis): ozel bayt (buyuk yigin bloklari haric), kucuk yigin bayti, buyuk blok adedi.
    std::vector<std::size_t> priv, heap_small, heap_big, heap_pool;
    for (int round = 0; round < rounds; ++round) {
        MainWindow::Options o;
        o.window_class = L"EcuFps.LifecycleTest";
        o.title = L"Ecu Fps lifecycle";
        o.quit_on_destroy = false;
        auto w = MainWindow::create(GetModuleHandleW(nullptr), theme, cat.value(), o);
        if (!w) return 2;
        ShowWindow(w->hwnd(), SW_SHOWNOACTIVATE);
        SetWindowPos(w->hwnd(), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);  // kullanicinin penceresinin onune cikmaz
        UpdateWindow(w->hwnd());
        if (!pump_until_painted(*w, 10000)) {
            std::printf("round %d: no full paint within 10 s\n", round);
            return 2;
        }
        pump(60);
        if (w->activity().get() != Visibility::Visible) {
            std::printf("round %d: expected Visible, got %d\n", round, static_cast<int>(w->activity().get()));
            ++failures;
        }
        if (round == 0) {
            ShowWindow(w->hwnd(), SW_MINIMIZE);
            pump(80);
            if (w->activity().get() != Visibility::Minimized) {
                std::printf("expected Minimized, got %d\n", static_cast<int>(w->activity().get()));
                ++failures;
            }
            ShowWindow(w->hwnd(), SW_HIDE);
            pump(80);
            if (w->activity().get() != Visibility::Hidden) {
                std::printf("expected Hidden, got %d\n", static_cast<int>(w->activity().get()));
                ++failures;
            }
        }
        w.reset();  // pencere ve kaynaklari birak
        // Bilerek sizinti (yalniz sizinti-yakalama kaniti icin; normal kosuda kapali).
        if (leak_heap) {
            void* p = HeapAlloc(GetProcessHeap(), 0, leak_heap);
            if (p) std::memset(p, 1, leak_heap);
        }
        if (leak_virt) {
            void* p = VirtualAlloc(nullptr, leak_virt, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (p) std::memset(p, 1, leak_virt);
        }
        if (leak_gdi > 0 && round % leak_gdi == 0) CreateBitmap(8, 8, 1, 32, nullptr);
        if (lazy_step && round == warm + win) {
            for (int k = 0; k < 7; ++k) CreateBitmap(8, 8, 1, 32, nullptr);
        }
        pump(30);
        drain_queue();
        const std::size_t priv_total = private_bytes_settled();
        const HeapStats hs = heap_stats();
        priv.push_back(priv_total > hs.big_bytes ? priv_total - hs.big_bytes : 0);  // buyuk yigin bloklari ayri izlenir
        heap_small.push_back(hs.small_bytes);
        heap_big.push_back(hs.big_count);
        heap_pool.push_back(hs.pool_count);
        if (trace)
            std::printf("T,%d,%zu,%zu,%zu,%zu,%lu,%lu,%zu\n", round, priv_total, hs.small_bytes, hs.big_bytes, hs.big_count,
                        GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS),
                        hs.pool_count);
        const DWORD g = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        const DWORD u = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        if (round == warm - 1) {  // isinma sonrasi taban: buradan sonra buyume sizinti sayilir
            gdi0 = g;
            user0 = u;
        } else if (round >= warm && (g > prev_g || u > prev_u)) {
            ++object_rises;  // nesne sayisinin arttigi tur
        }
        prev_g = g;
        prev_u = u;
    }
    const DWORD gdi1 = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD user1 = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);

    const long long dpriv = static_cast<long long>(window_min(priv, rounds - win, rounds)) - static_cast<long long>(window_min(priv, warm, warm + win));
    const long long dheap = static_cast<long long>(window_min(heap_small, rounds - win, rounds)) - static_cast<long long>(window_min(heap_small, warm, warm + win));
    const long long dbig = static_cast<long long>(window_min(heap_big, rounds - win, rounds)) - static_cast<long long>(window_min(heap_big, warm, warm + win));
    const long long dpool = static_cast<long long>(window_min(heap_pool, rounds - win, rounds)) - static_cast<long long>(window_min(heap_pool, warm, warm + win));
    const long long pool_max = static_cast<long long>(*std::max_element(heap_pool.begin() + warm, heap_pool.end()));
    const long long pool_min = static_cast<long long>(*std::min_element(heap_pool.begin() + warm, heap_pool.end()));
    std::printf("ui_lifecycle: rounds=%d gdi %lu->%lu (rises %d) user %lu->%lu heap_small_delta_kib=%lld big_blocks_delta=%lld private_delta_kib=%lld pool932576 min=%lld max=%lld envelope_delta=%lld failures=%d\n",
                rounds, gdi0, gdi1, object_rises, user0, user1, dheap / 1024, dbig, dpriv / 1024, pool_min, pool_max, dpool, failures);
    // Nesne sayisi: bir pencere sizintisi turlarla tekrarlar (>= 1 nesne / ~25 tur => kisa surede >3 artis turu).
    // Sistem kaynakli TEK SEFERLIK basamak (ornegin ilk kez yuklenen bir giris/erisilebilirlik bileseninin ~7 GDI + 3 USER
    // nesnesi; 20 kosuda ~1 kez, taban aninin onune/arkasina rastlar) sizinti degildir: artis turu sayisi <= 3 ve toplam
    // artis <= 32 olan basamaga izin verilir.
    const bool leak_objects = object_rises > kMaxObjectRises || gdi1 > gdi0 + kMaxObjectStep || user1 > user0 + kMaxObjectStep;
    const bool leak_heap_seen = dheap > kHeapToleranceBytes || dbig > kBigBlockTolerance;
    const bool leak_priv_seen = dpriv > kPrivateToleranceBytes;
    const bool leak_pool_seen = pool_max > kPoolCeiling || dpool > kPoolEnvelopeTolerance;
    if (leak_objects) std::printf("LEAK_DETECTED objects\n");
    if (leak_heap_seen) std::printf("LEAK_DETECTED heap\n");
    if (leak_priv_seen) std::printf("LEAK_DETECTED private\n");
    if (leak_pool_seen) std::printf("LEAK_DETECTED pool\n");
    const bool leak = leak_objects || leak_heap_seen || leak_priv_seen || leak_pool_seen;
    return (failures == 0 && !leak) ? 0 : 1;
}
