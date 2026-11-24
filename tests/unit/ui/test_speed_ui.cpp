// Task 12: internet hiz testi kontrolleri — sabit kimlikler, UIA adlari, odak sirasi ve metin anahtarlari.
#include <algorithm>
#include <iterator>

#include "core/i18n/catalog.h"
#include "core/net/speed_test.h"
#include "ef_test.h"
#include "ui/shell/screen.h"

using namespace ef::ui;

namespace {
bool has(const std::vector<Hit>& v, Hit h) { return std::find(v.begin(), v.end(), h) != v.end(); }
}  // namespace

EF_TEST(speed_hits_are_fixed_named_controls) {
    for (const Hit h : {Hit::SpeedStart, Hit::SpeedStop}) {
        EF_CHECK(classify_hit(h).kind == HitKind::Fixed);
        EF_CHECK(std::find(std::begin(kFixedHits), std::end(kFixedHits), h) != std::end(kFixedHits));
    }
    EF_CHECK(hit_automation_id(Hit::SpeedStart) == L"measure.speed.start");
    EF_CHECK(hit_automation_id(Hit::SpeedStop) == L"measure.speed.stop");
}

// Olcumler sayfasinda: calisirken yalniz Durdur; baslatilabilirken yalniz Baslat; oyun kapisi kapaliyken ikisi de yok.
EF_TEST(speed_focus_order_follows_state) {
    UiState s;
    s.page = Page::Measurements;
    s.speed_can_start = true;
    auto v = focus_order(s);
    EF_CHECK(has(v, Hit::SpeedStart) && !has(v, Hit::SpeedStop));
    s.speed_can_start = false;
    s.speed_running = true;
    v = focus_order(s);
    EF_CHECK(has(v, Hit::SpeedStop) && !has(v, Hit::SpeedStart));
    s.speed_running = false;
    v = focus_order(s);
    EF_CHECK(!has(v, Hit::SpeedStop) && !has(v, Hit::SpeedStart));
    s.page = Page::Overview;
    s.speed_can_start = true;
    EF_CHECK(!has(focus_order(s), Hit::SpeedStart));  // yalniz Olcumler sayfasinda
}

EF_TEST(speed_strings_are_cataloged) {
    using ef::core::net::SpeedFail;
    for (const auto f : {SpeedFail::Offline, SpeedFail::Timeout, SpeedFail::Tls, SpeedFail::Http, SpeedFail::Budget, SpeedFail::Gated})
        EF_CHECK(ef::core::i18n::id_of(ef::core::net::speed_fail_key(f)).has_value());
    for (const char* k : {"speed.title", "speed.question", "speed.detail", "speed.start", "speed.stop", "speed.again",
                          "speed.phase.latency", "speed.phase.down", "speed.phase.up", "speed.state.ready", "speed.state.latency",
                          "speed.state.down", "speed.state.up", "speed.state.done", "speed.state.cancelled", "speed.result",
                          "speed.down", "speed.up", "speed.latency", "speed.data_used", "speed.mbs", "speed.mbps", "speed.ms",
                          "speed.mb", "speed.server", "speed.note", "speed.blocked_game", "speed.gauge"})
        EF_CHECK(ef::core::i18n::id_of(k).has_value());
}
