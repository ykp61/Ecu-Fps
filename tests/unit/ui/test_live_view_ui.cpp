// Canli test ekrani arayuzu (spec 2026-11-12 A4): sabit kimlikler ve UIA AutomationId'leri, odak sirasi (canli gorunum:
// sekmeler + "Testi durdur"; ayar anahtari yalniz cizildiyse ve test islemiyorken), yuk cubugu gecisleri (azaltilmis
// harekette aninda; ayri animasyon zamanlayicisi istemez).
#include <algorithm>
#include <iterator>
#include <vector>

#include "core/i18n/catalog.h"
#include "ef_test.h"
#include "ui/shell/screen.h"

using namespace ef::ui;
namespace cm = ef::core::measure;

namespace {
bool has(const std::vector<Hit>& v, Hit h) { return std::find(v.begin(), v.end(), h) != v.end(); }
cm::LiveSample sample(double fps, double cpu_ms, double gpu_ms) {
    cm::LiveSample s;
    s.has_numbers = true;
    s.fps = fps;
    s.cpu_ms = cpu_ms;
    s.gpu_ms = gpu_ms;
    return s;
}
}  // namespace

EF_TEST(live_view_hits_are_fixed_and_named) {
    for (Hit h : {Hit::LiveView, Hit::LiveStep, Hit::LiveFps, Hit::LiveLoad}) {
        EF_CHECK(classify_hit(h).kind == HitKind::Fixed);
        EF_CHECK(std::find(std::begin(kFixedHits), std::end(kFixedHits), h) != std::end(kFixedHits));
    }
    EF_CHECK(hit_automation_id(Hit::LiveView) == L"measure.live_view");
    EF_CHECK(hit_automation_id(Hit::LiveStep) == L"measure.live.step");
    EF_CHECK(hit_automation_id(Hit::LiveFps) == L"measure.live.fps");
    EF_CHECK(hit_automation_id(Hit::LiveLoad) == L"measure.live.load");
    // Mevcut kimlikler yerinde (durdurma dugmesi degismedi).
    static_assert(static_cast<int>(Hit::TestStop) == 113 && static_cast<int>(Hit::ScopeBalance) == 143);
    EF_CHECK(hit_automation_id(Hit::TestStop) == L"measure.stop");
}

EF_TEST(live_view_focus_order_is_tabs_then_stop) {
    UiState s;
    s.page = Page::Measurements;
    s.measure.builtin_available = true;
    s.measure.scope = cm::BuiltinScope::Balance;
    s.measure.phase = cm::TestPhase::Running;
    s.measure.live_view.active = true;
    s.measure_live_toggle = true;
    s.speed_can_start = true;
    s.detection_retry = true;  // canli gorunumde oyun seridi cizilmez: durak eklenmez
    s.live_view = true;
    auto ord = focus_order(s);
    EF_CHECK(ord.size() == kNavTabCount + 1 && ord.back() == Hit::TestStop);
    for (const Page pg : kNavOrder) EF_CHECK(has(ord, nav_hit(static_cast<std::size_t>(pg))));
    // Durduruluyor: dugme devre disi cizilir, Tab almaz.
    s.measure.phase = cm::TestPhase::Stopping;
    ord = focus_order(s);
    EF_CHECK(ord.size() == kNavTabCount && !has(ord, Hit::TestStop));
    // Gorunum kapaliyken (ayni durum, baska sayfa cizimi) mevcut sira aynen.
    s.live_view = false;
    s.detection_retry = false;
    s.measure.phase = cm::TestPhase::Running;
    ord = focus_order(s);
    EF_CHECK(has(ord, Hit::TestStop) && has(ord, Hit::SpeedStart));
    EF_CHECK(!has(ord, Hit::LiveView) && !has(ord, Hit::ModeQuick));  // test islerken kart ici secenekler devre disi
}

EF_TEST(live_view_setting_toggle_tab_stop_follows_drawing) {
    UiState s;
    s.page = Page::Measurements;
    s.measure.builtin_available = true;
    s.measure.scope = cm::BuiltinScope::Balance;
    s.measure_live_toggle = true;
    s.speed_can_start = true;
    auto ord = focus_order(s);
    EF_CHECK(has(ord, Hit::LiveView));
    // B10 gorunen sira: Test karti ici sure secimi -> canli test ekrani satiri -> Internet hizi.
    const auto pos = [&](Hit h) { return std::find(ord.begin(), ord.end(), h) - ord.begin(); };
    EF_CHECK(pos(Hit::ModeFull) < pos(Hit::LiveView) && pos(Hit::LiveView) < pos(Hit::SpeedStart));
    s.measure_live_toggle = false;  // Genel test kapsami / Oyunda Test: anahtar cizilmez
    EF_CHECK(!has(focus_order(s), Hit::LiveView));
    s.measure_live_toggle = true;
    s.measure.endurance.running = true;  // uzun test surerken kilitli
    EF_CHECK(!has(focus_order(s), Hit::LiveView));
}

EF_TEST(live_view_load_bars_glide_between_samples_and_snap_with_reduced_motion) {
    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    const auto& theme = default_theme();
    Screen screen(theme, cat.value());
    UiState s;
    // Ilk ornek: gecis yok (deger dogrudan yazilir). 100 FPS: 5 ms -> %50, 2 ms -> %20.
    screen.set_live_sample(s, sample(100, 5, 2), 1000);
    EF_CHECK(s.live_cpu_seen && s.live_gpu_seen);
    EF_CHECK(s.live_cpu.value(1000, theme.easing) == 50.f && s.live_gpu.value(1000, theme.easing) == 20.f);
    // Sonraki ornek: hedefe dogru kayar; sure bitince hedeftedir.
    screen.set_live_sample(s, sample(100, 9, 2), 2000);
    const float mid = s.live_cpu.value(2050, theme.easing);
    EF_CHECK(mid > 50.f && mid < 90.f);
    EF_CHECK(s.live_cpu.value(3000, theme.easing) == 90.f);
    EF_CHECK(!s.live_gpu.active(2050));  // degismeyen deger gecis baslatmaz
    // Yuk cubuklari ayri animasyon zamanlayicisi istemez (onizleme karesiyle birlikte cizilir): test surerken 60 Hz yok.
    EF_CHECK(!screen.animating(s, 2050));
    // GPU zaman damgasi kayboldu: deger yok; geri geldiginde gecissiz yazilir.
    cm::LiveSample no_gpu = sample(100, 9, 2);
    no_gpu.gpu_ms.reset();
    screen.set_live_sample(s, no_gpu, 4000);
    EF_CHECK(!s.live_gpu_seen);
    screen.set_live_sample(s, sample(100, 9, 7), 5000);
    EF_CHECK(s.live_gpu_seen && s.live_gpu.value(5000, theme.easing) == 70.f);
    // Azaltilmis hareket: aninda.
    UiState r;
    r.reduced_motion = true;
    screen.set_live_sample(r, sample(100, 5, 2), 1000);
    screen.set_live_sample(r, sample(100, 9, 8), 2000);
    EF_CHECK(r.live_cpu.value(2000, theme.easing) == 90.f && r.live_gpu.value(2000, theme.easing) == 80.f);
    EF_CHECK(!r.live_cpu.active(2000) && !r.live_gpu.active(2000));
    // Ornek durum nesnesinde saklanir (cizim buradan okur); bos ornek pikselleri birakir.
    EF_CHECK(r.live_test.has_numbers && r.live_test.fps == 100);
    screen.set_live_sample(r, {}, 3000);
    EF_CHECK(!r.live_test.has_numbers && !r.live_test.preview && !r.live_cpu_seen && !r.live_gpu_seen);
}
