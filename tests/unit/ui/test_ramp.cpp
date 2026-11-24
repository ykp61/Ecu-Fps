#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include "ef_test.h"
#include "ui/theme/ramp.h"
#include "ui/theme/theme.h"

using namespace ef::ui;

namespace {
// a ile b arasinda (her kanal arada, en az bir kanal ikisinden de farkli).
bool strictly_between(const Color& c, const Color& a, const Color& b) {
    auto in = [](float v, float x, float y) { return v >= std::fmin(x, y) - 1e-6f && v <= std::fmax(x, y) + 1e-6f; };
    return in(c.r, a.r, b.r) && in(c.g, a.g, b.g) && in(c.b, a.b, b.b) && !(c == a) && !(c == b);
}
}  // namespace

EF_TEST(ramp_endpoints_and_stops) {
    const Palette& p = default_theme().dark;
    EF_CHECK(usage_color(p, 0) == p.accent);
    EF_CHECK(usage_color(p, 40) == p.accent);
    EF_CHECK(usage_color(p, 60) == p.warning);
    EF_CHECK(usage_color(p, 75) == p.orange);
    EF_CHECK(usage_color(p, 92) == p.error);
    EF_CHECK(usage_color(p, 100) == p.error);
    EF_CHECK(usage_color(p, -5) == p.accent);
    EF_CHECK(usage_color(p, 250) == p.error);
    EF_CHECK(strictly_between(usage_color(p, 50), p.accent, p.warning));
    EF_CHECK(strictly_between(usage_color(p, 68), p.warning, p.orange));
    EF_CHECK(strictly_between(usage_color(p, 85), p.orange, p.error));
    EF_CHECK(usage_color(p, std::numeric_limits<double>::quiet_NaN()) == p.accent);
    EF_CHECK(usage_color(p, std::numeric_limits<double>::infinity()) == p.error);
}

EF_TEST(ramp_words) {
    EF_CHECK(usage_word_key(0) == "live.usage.low");
    EF_CHECK(usage_word_key(54.9) == "live.usage.low");
    EF_CHECK(usage_word_key(55) == "live.usage.mid");
    EF_CHECK(usage_word_key(69.9) == "live.usage.mid");
    EF_CHECK(usage_word_key(70) == "live.usage.high");
    EF_CHECK(usage_word_key(87.9) == "live.usage.high");
    EF_CHECK(usage_word_key(88) == "live.usage.max");
    EF_CHECK(usage_word_key(400) == "live.usage.max");
    EF_CHECK(usage_word_key(std::numeric_limits<double>::quiet_NaN()) == "live.usage.low");
}

EF_TEST(ramp_alarm) {
    EF_CHECK(!usage_alarm(87.9));
    EF_CHECK(usage_alarm(88));
    EF_CHECK(usage_alarm(100));
    EF_CHECK(!usage_alarm(std::numeric_limits<double>::quiet_NaN()));
}

EF_TEST(ramp_glow_and_short_words) {
    EF_CHECK(!usage_glow(74.9));
    EF_CHECK(usage_glow(75));
    EF_CHECK(usage_glow(100));
    EF_CHECK(!usage_glow(std::numeric_limits<double>::quiet_NaN()));
    // Kisa kelime esikleri uzun kelimeyle ayni.
    EF_CHECK(usage_short_word_key(0) == "live.usage_short.low");
    EF_CHECK(usage_short_word_key(54.9) == "live.usage_short.low");
    EF_CHECK(usage_short_word_key(55) == "live.usage_short.mid");
    EF_CHECK(usage_short_word_key(70) == "live.usage_short.high");
    EF_CHECK(usage_short_word_key(88) == "live.usage_short.max");
    EF_CHECK(usage_short_word_key(std::numeric_limits<double>::quiet_NaN()) == "live.usage_short.low");
}

// Spec 2026-11-11 §4: 12 parcali LED cubuk; parca i, (i + 1) / 12 * 100 <= deger + 4 ise yanar.
EF_TEST(led_segment_rule) {
    static_assert(kLedSegments == 12);
    EF_CHECK(std::fabs(led_segment_pct(0) - 100.0 / 12.0) < 1e-9);
    EF_CHECK(std::fabs(led_segment_pct(5) - 50.0) < 1e-9);
    EF_CHECK(led_segment_pct(11) == 100.0);
    EF_CHECK(led_segment_pct(-1) == 0 && led_segment_pct(12) == 0);
    // %0: hic yanmaz (8,33 > 4). %4,34: ilk parca yanar (8,33 <= 8,34).
    EF_CHECK(led_lit_count(0) == 0);
    EF_CHECK(led_lit_count(4.3) == 0);
    EF_CHECK(led_lit_count(4.34) == 1);
    // %23: 8,33 / 16,67 / 25 <= 27 -> 3 parca; 33,33 > 27.
    EF_CHECK(led_lit_count(23) == 3);
    EF_CHECK(led_segment_lit(2, 23) && !led_segment_lit(3, 23));
    // %46: 50 <= 50 -> 6 parca (esitlik yanar).
    EF_CHECK(led_lit_count(46) == 6);
    EF_CHECK(led_lit_count(45.9) == 5);
    // %96 ve ustu: 100 <= 100 -> hepsi; %95,9: 11.
    EF_CHECK(led_lit_count(95.9) == 11);
    EF_CHECK(led_lit_count(96) == 12);
    EF_CHECK(led_lit_count(100) == 12);
    // Aralik disi deger kirpilir; NaN hic yakmaz; gecersiz parca yanmaz.
    EF_CHECK(led_lit_count(250) == 12);
    EF_CHECK(led_lit_count(-20) == 0);
    EF_CHECK(led_lit_count(std::numeric_limits<double>::quiet_NaN()) == 0);
    EF_CHECK(!led_segment_lit(-1, 100) && !led_segment_lit(12, 100));
    // Yanan parcalar soldan baslayan kesintisiz bir dizidir.
    for (double v = 0; v <= 100; v += 0.5) {
        const int n = led_lit_count(v);
        for (int i = 0; i < kLedSegments; ++i) EF_CHECK(led_segment_lit(i, v) == (i < n));
    }
}

EF_TEST(led_segment_color_follows_position) {
    // Yanan parcanin rengi kendi konumunun rampa rengidir: ilk parcalar accent, son parca error.
    const Palette& p = default_theme().dark;
    EF_CHECK(usage_color(p, led_segment_pct(0)) == p.accent);
    EF_CHECK(usage_color(p, led_segment_pct(3)) == p.accent);   // 33,3
    EF_CHECK(usage_color(p, led_segment_pct(8)) == p.orange);   // 75
    EF_CHECK(usage_color(p, led_segment_pct(11)) == p.error);   // 100
}

EF_TEST(led_peak_hold_is_time_based) {
    // Tepe, kare sayisindan bagimsiz: ayni zaman noktasinda kac kez cizilirse cizilsin ayni tepe.
    LedPeak a, b;
    EF_CHECK(led_peak_update(a, 80.0, 1000, false) == 80.0);
    EF_CHECK(led_peak_update(b, 80.0, 1000, false) == 80.0);
    for (std::int64_t t = 1001; t < 3000; t += 16) (void)led_peak_update(a, 30.0, t, false);  // ~60 Hz
    (void)led_peak_update(b, 30.0, 2000, false);                                              // ~1 Hz
    const auto pa = led_peak_update(a, 30.0, 3000, false);
    const auto pb = led_peak_update(b, 30.0, 3000, false);
    EF_CHECK(pa && pb && std::fabs(*pa - *pb) < 1e-9);
    // Bekleme suresince tepe sabit; sonra kLedPeakDecayPerSec puan/sn iner.
    LedPeak c;
    (void)led_peak_update(c, 90.0, 0, false);
    EF_CHECK(led_peak_update(c, 20.0, kLedPeakHoldMs, false) == 90.0);
    const auto later = led_peak_update(c, 20.0, kLedPeakHoldMs + 1000, false);
    EF_CHECK(later && std::fabs(*later - (90.0 - kLedPeakDecayPerSec)) < 1e-9);
    // Gecerli degere yetisince iz sifirlanir ve gecerli deger doner (tepe < deger olmaz).
    const auto caught = led_peak_update(c, 20.0, kLedPeakHoldMs + 60000, false);
    EF_CHECK(caught == 20.0);
    // Yeni tepe izi hemen yukari tasir.
    EF_CHECK(led_peak_update(c, 55.0, kLedPeakHoldMs + 60001, false) == 55.0);
}

EF_TEST(led_peak_reduced_motion_and_missing) {
    // Azaltilmis harekette inis yok: bekleme boyunca tepe, sonra dogrudan gecerli deger.
    LedPeak p;
    (void)led_peak_update(p, 90.0, 0, true);
    EF_CHECK(led_peak_update(p, 10.0, kLedPeakHoldMs - 1, true) == 90.0);
    EF_CHECK(led_peak_update(p, 10.0, kLedPeakHoldMs + 1, true) == 10.0);
    // Deger yok / NaN: iz silinir; sonraki deger yeni tepe olur.
    (void)led_peak_update(p, 95.0, 5000, false);
    EF_CHECK(!led_peak_update(p, std::nullopt, 5001, false));
    EF_CHECK(p.pct < 0);
    EF_CHECK(!led_peak_update(p, std::numeric_limits<double>::quiet_NaN(), 5002, false));
    EF_CHECK(led_peak_update(p, 12.0, 5003, false) == 12.0);
    // Saat geri giderse bekler (inis yok).
    (void)led_peak_update(p, 70.0, 10000, false);
    EF_CHECK(led_peak_update(p, 5.0, 9000, false) == 70.0);
    // Aralik disi kirpilir.
    LedPeak q;
    EF_CHECK(led_peak_update(q, 250.0, 0, false) == 100.0);
}

EF_TEST(led_peak_segment_only_above_lit) {
    // Tepe parcasi yalniz gecerli degerle yanmayan bir parcadaysa isaretlenir.
    EF_CHECK(led_peak_segment(50.0, 50.0) == -1);
    EF_CHECK(led_peak_segment(47.0, 46.0) == -1);       // ayni parca sayisi (6)
    EF_CHECK(led_peak_segment(96.0, 10.0) == 11);       // en ust parca
    EF_CHECK(led_peak_segment(46.0, 23.0) == 5);        // 6 parca yanardi; 3 yaniyor -> dizin 5
    EF_CHECK(led_peak_segment(0.0, 0.0) == -1);         // hic yanmayan tepe
    for (double v = 0; v <= 100; v += 2.5) {
        const int seg = led_peak_segment(100.0, v);
        EF_CHECK(seg == -1 || !led_segment_lit(seg, v));
    }
}
