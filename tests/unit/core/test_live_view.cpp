// Canli test ekrani (spec 2026-11-12 A4): saf model (kare suresi, yuk yuzdeleri, adim yuk ayari, cubuklar, ilerleme),
// gorunum kosulu, onizleme posta kutusu (en yeni kazanir, beklemez, tampon yeniden kullanimi) ve kalici tercih.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#include "benchmark/balance/balance_runner.h"
#include "benchmark/scene/scene.h"
#include "core/measure/balance_view.h"
#include "core/measure/live_view.h"
#include "core/measure/view.h"
#include "core/settings/preferences.h"
#include "ef_test.h"

using namespace ef::core::measure;
using ef::core::settings::Preferences;

namespace {
bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
std::vector<std::uint8_t> solid(std::uint32_t w, std::uint32_t h, std::uint32_t stride, std::uint8_t v) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(stride) * h, 0xEE);  // dolgu baytlari 0xEE: kopyalanmamali
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w * 4; ++x) px[static_cast<std::size_t>(stride) * y + x] = v;
    return px;
}
}  // namespace

EF_TEST(live_view_numbers_are_empty_until_a_window_closed) {
    LiveSample s;
    s.fps = 120, s.cpu_ms = 4, s.gpu_ms = 6;  // has_numbers false: hicbir sayi turetilmez
    EF_CHECK(!live_frame_ms(s) && !live_cpu_load_pct(s) && !live_gpu_load_pct(s));
    s.has_numbers = true;
    EF_CHECK(live_frame_ms(s) && near(*live_frame_ms(s), 1000.0 / 120.0));
    // Yuk = kare basi sure / kare araligi: 4 ms / 8,33 ms = %48; 6 ms / 8,33 ms = %72.
    EF_CHECK(near(*live_cpu_load_pct(s), 48.0, 1e-6) && near(*live_gpu_load_pct(s), 72.0, 1e-6));
    // GPU zaman damgasi yoksa GPU yuku yok (0 yazilmaz); CPU yuku kalir.
    s.gpu_ms.reset();
    EF_CHECK(live_cpu_load_pct(s) && !live_gpu_load_pct(s));
    // Oran [0, 100] araligina kirpilir; gecersiz girdi sayi uretmez.
    s.cpu_ms = 50;
    EF_CHECK(near(*live_cpu_load_pct(s), 100.0));
    s.cpu_ms = -1;
    EF_CHECK(!live_cpu_load_pct(s));
    s.cpu_ms = 4;
    for (const double bad : {0.0, -5.0, std::nan("")}) {
        s.fps = bad;
        EF_CHECK(!live_frame_ms(s) && !live_cpu_load_pct(s));
    }
}

EF_TEST(live_view_load_kind_follows_the_balance_step_order) {
    namespace bb = ef::benchmark::balance;
    const auto code = [](bb::StepId id) { return static_cast<std::uint32_t>(id) + 1u; };
    EF_CHECK(live_load(code(bb::StepId::S1)) == LiveLoad::CpuHeavy && live_load(code(bb::StepId::T1)) == LiveLoad::CpuHeavy);
    for (const auto id : {bb::StepId::S2, bb::StepId::S3, bb::StepId::S4, bb::StepId::T2}) EF_CHECK(live_load(code(id)) == LiveLoad::GpuHeavy);
    for (const auto id : {bb::StepId::K1, bb::StepId::K2, bb::StepId::K3}) EF_CHECK(live_load(code(id)) == LiveLoad::Both);
    EF_CHECK(live_load(0) == LiveLoad::Unknown && live_load(10) == LiveLoad::Unknown);
    // Plan ile ayni: CPU profili simulasyon tasiyorsa CPU tam, GPU profili agirsa GPU tam.
    bb::BalanceOptions o;
    const auto plan = bb::balance_plan(o, 8);
    for (const auto& p : plan) {
        const bool cpu_full = p.cpu && p.cpu->sim, gpu_full = p.gpu && p.gpu->bloom;  // C: simulasyon; G: bloom acik
        const LiveLoad want = cpu_full && gpu_full ? LiveLoad::Both : cpu_full ? LiveLoad::CpuHeavy : LiveLoad::GpuHeavy;
        EF_CHECK(live_load(code(p.id)) == want);
        EF_CHECK(!balance_stage_key(code(p.id)).empty());  // gorunumun adim adi icin asama anahtari var
    }
    static_assert(kLiveRingSize == ef::benchmark::kTapRingSize);
    static_assert(kLivePreviewMaxSide >= ef::benchmark::kPreviewMaxWidth && kLivePreviewMaxSide >= ef::benchmark::kPreviewMaxHeight);
}

EF_TEST(live_view_frame_bars_and_step_fraction) {
    LiveSample s;
    EF_CHECK(live_frame_bars(s, 32).empty() && !live_step_fraction(s));
    s.fps_ring[0] = 100.f, s.fps_ring[1] = 0.f, s.fps_ring[2] = 50.f, s.fps_ring[3] = 200.f, s.fps_ring[4] = 999.f;
    s.ring_count = 4;  // besinci oge gecersiz bolgede
    auto bars = live_frame_bars(s, 32);
    EF_CHECK(bars.size() == 3 && near(bars[0], 10.0) && near(bars[1], 20.0) && near(bars[2], 5.0));  // 0 FPS kovasi atlanir
    bars = live_frame_bars(s, 2);  // en yeniler kalir
    EF_CHECK(bars.size() == 2 && near(bars[0], 20.0) && near(bars[1], 5.0));
    EF_CHECK(live_frame_bars(s, 0).empty());
    s.ring_count = 99;  // bozuk sayac dizinin disina tasmaz
    EF_CHECK(live_frame_bars(s, 64).size() <= kLiveRingSize);
    // Ilerleme: tamamlanan adim orani.
    s.step_index = 1, s.step_total = 9;
    EF_CHECK(near(*live_step_fraction(s), 0.0));
    s.step_index = 5;
    EF_CHECK(near(*live_step_fraction(s), 4.0 / 9.0));
    s.step_index = 10;
    EF_CHECK(!live_step_fraction(s));
}

EF_TEST(live_view_shown_only_for_a_running_app_test_started_with_it) {
    MeasureState m;
    EF_CHECK(m.live_view.user_on && !m.live_view.active);  // varsayilan: ayar acik, kosu yok
    EF_CHECK(!live_view_shown(m));
    m.live_view.active = true;
    EF_CHECK(!live_view_shown(m));  // test islemiyor
    for (const auto ph : {TestPhase::Preparing, TestPhase::Running, TestPhase::Stopping}) {
        m.phase = ph;
        EF_CHECK(live_view_shown(m));
    }
    for (const auto ph : {TestPhase::Done, TestPhase::Cancelled, TestPhase::Failed, TestPhase::Idle}) {
        m.phase = ph;
        EF_CHECK(!live_view_shown(m));
    }
    m.phase = TestPhase::Running;
    m.source = TestSource::InGame;
    EF_CHECK(!live_view_shown(m));  // Oyunda Test'te canli gorunum yok
    m.source = TestSource::Builtin;
    m.live_view.active = false;
    EF_CHECK(!live_view_shown(m));  // ayar kapaliyken / pencere gorunmezken baslayan kosu
}

EF_TEST(live_preview_mailbox_latest_wins_and_never_copies_when_unwanted) {
    PreviewMailbox box;
    const auto a = solid(4, 2, 20, 0x11);
    EF_CHECK(!box.wanted());
    EF_CHECK(!box.offer(a.data(), 4, 2, 20) && !box.take());  // istenmiyor: kare dusurulur
    EF_CHECK(box.offered() == 1 && box.dropped() == 1);
    box.set_wanted(true);
    EF_CHECK(box.offer(a.data(), 4, 2, 20));
    const auto b = solid(4, 2, 16, 0x22);
    EF_CHECK(box.offer(b.data(), 4, 2, 16));  // okunmamis onceki karenin yerini alir
    const auto img = box.take();
    EF_CHECK(img && img->w == 4 && img->h == 2 && img->bgra.size() == 4u * 2u * 4u);
    bool all_b = true;
    for (const auto v : img->bgra) all_b = all_b && v == 0x22;
    EF_CHECK(all_b);
    EF_CHECK(!box.take());  // yeni kare yok
    // Satir dolgusu kopyalanmaz (stride > width * 4).
    EF_CHECK(box.offer(a.data(), 4, 2, 20));
    const auto padded = box.take();
    bool all_a = padded && padded->bgra.size() == 32;
    if (padded)
        for (const auto v : padded->bgra) all_a = all_a && v == 0x11;
    EF_CHECK(all_a);
    EF_CHECK(ef::core::games::image_pixels_valid(*padded, kLivePreviewMaxSide));
    // Gecersiz kare: bos, sifir boyut, sinir ustu, kisa satir.
    EF_CHECK(!box.offer(nullptr, 4, 2, 16) && !box.offer(a.data(), 0, 2, 16) && !box.offer(a.data(), 4, 0, 16));
    EF_CHECK(!box.offer(a.data(), kLivePreviewMaxSide + 1, 1, (kLivePreviewMaxSide + 1) * 4) && !box.offer(a.data(), 4, 2, 15));
    EF_CHECK(!box.take());
}

EF_TEST(live_preview_mailbox_reuses_released_buffers) {
    PreviewMailbox box;
    box.set_wanted(true);
    const auto px = solid(8, 8, 32, 0x33);
    EF_CHECK(box.offer(px.data(), 8, 8, 32));
    auto first = box.take();
    const auto* addr = first.get();
    const auto* data = first->bgra.data();
    first.reset();  // tampon havuza doner
    EF_CHECK(box.offer(px.data(), 8, 8, 32));
    const auto second = box.take();
    EF_CHECK(second.get() == addr && second->bgra.data() == data);  // ayni tampon: kare basina bellek ayirma yok
    // Tutulan kare degismez: yeni kare ayri tampona yazilir.
    const auto other = solid(8, 8, 32, 0x44);
    EF_CHECK(box.offer(other.data(), 8, 8, 32));
    const auto third = box.take();
    EF_CHECK(third.get() != second.get() && second->bgra[0] == 0x33 && third->bgra[0] == 0x44);
    // Kutu silindikten sonra da kare gecerli kalir (havuz paylasimli).
    ef::core::games::ImageRef kept;
    {
        PreviewMailbox temp;
        temp.set_wanted(true);
        EF_CHECK(temp.offer(px.data(), 8, 8, 32));
        kept = temp.take();
        temp.clear();
    }
    EF_CHECK(kept && kept->bgra.size() == 8u * 8u * 4u && kept->bgra[5] == 0x33);
}

// Kosu bitince (clear) kutu onizleme tamponu tutmaz: UI'nin elinde kalan son kare clear()'dan SONRA birakildiginda
// havuza donmez (aksi halde her kosudan sonra 960x540'lik bir tampon bosta bellekte kalirdi). Sonraki kosu yeniden
// kullanimi acar.
EF_TEST(live_preview_mailbox_holds_no_buffer_after_the_run) {
    PreviewMailbox box;
    box.set_wanted(true);
    const auto px = solid(8, 8, 32, 0x55);
    EF_CHECK(box.spare_buffers() == 0);
    EF_CHECK(box.offer(px.data(), 8, 8, 32));
    auto shown = box.take();  // UI'nin gosterdigi kare
    EF_CHECK(box.offer(px.data(), 8, 8, 32) && box.offer(px.data(), 8, 8, 32));  // okunmayan kare havuza doner
    EF_CHECK(box.spare_buffers() == 1);
    // Kosu bitti: uretici durdu, kutu bosaltildi; UI son kareyi daha sonra birakir.
    box.set_wanted(false);
    box.clear();
    EF_CHECK(box.spare_buffers() == 0 && !box.take());
    EF_CHECK(shown && shown->bgra.size() == 8u * 8u * 4u && shown->bgra[0] == 0x55);  // tutulan kare gecerli kalir
    shown.reset();
    EF_CHECK(box.spare_buffers() == 0);  // havuza donmedi: silindi
    // Sonraki kosu: yeniden kullanim yine calisir.
    box.set_wanted(true);
    EF_CHECK(box.offer(px.data(), 8, 8, 32));
    auto again = box.take();
    const auto* addr = again.get();
    again.reset();
    EF_CHECK(box.spare_buffers() == 1);
    EF_CHECK(box.offer(px.data(), 8, 8, 32));
    EF_CHECK(box.take().get() == addr);
}

EF_TEST(live_preview_mailbox_producer_and_consumer_do_not_block_each_other) {
    PreviewMailbox box;
    box.set_wanted(true);
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> taken{0};
    std::thread consumer([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            if (const auto img = box.take()) {
                // Kare tutarli: butun baytlar ayni deger (yarim yazilmis kare gorulmez).
                const std::uint8_t v = img->bgra.front();
                for (const auto b : img->bgra)
                    if (b != v) taken.store(1u << 30, std::memory_order_relaxed);
                taken.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    std::vector<std::uint8_t> px(16 * 16 * 4);
    std::uint64_t delivered = 0;
    for (int i = 0; i < 4000; ++i) {
        std::fill(px.begin(), px.end(), static_cast<std::uint8_t>(i));
        delivered += box.offer(px.data(), 16, 16, 64) ? 1u : 0u;
    }
    // Son teslim edilen kare kutuda okunmayi bekler (en yeni kazanir); tuketici onu alana kadar beklenir. Eski surum
    // hemen durduruyordu: uretici 4000 kareyi tuketici is parcacigi daha planlanmadan bitirirse tuketici hic kare almiyor
    // ve "taken > 0" yuk altinda (paralel derleme) rastgele kirmiziydi. Sinir yalniz takilmaya karsi.
    const auto wait_end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (taken.load(std::memory_order_relaxed) == 0 && std::chrono::steady_clock::now() < wait_end)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    stop = true;
    consumer.join();
    EF_CHECK(box.offered() == 4000 && delivered + box.dropped() == 4000);
    EF_CHECK(delivered > 0 && taken.load() > 0 && taken.load() <= delivered);  // tutarsiz kare gorulseydi sayac 2^30'u asardi
}

EF_TEST(preferences_live_test_view_defaults_on_and_round_trips) {
    Preferences p;
    EF_CHECK(p.live_test_view);
    p.live_test_view = false;
    auto r = Preferences::parse(p.serialize());
    EF_CHECK(r.ok() && !r.value().live_test_view);
    // Eski dosyada alan yok: varsayilan acik. Yanlis tur yok sayilir.
    r = Preferences::parse(R"({"schema_version":1})");
    EF_CHECK(r.ok() && r.value().live_test_view);
    r = Preferences::parse(R"({"schema_version":1,"live_test_view":"no"})");
    EF_CHECK(r.ok() && r.value().live_test_view);
    r = Preferences::parse(R"({"schema_version":1,"live_test_view":false})");
    EF_CHECK(r.ok() && !r.value().live_test_view);
}
