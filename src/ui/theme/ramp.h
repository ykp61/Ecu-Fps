#pragma once
// Zehir yesili: canli kutucuk kullanim rampasi (spec §5). Saf hesap, platformdan bagimsiz (birim testlenir).
// Renkler yalniz Palette'ten; esikler: 0-40 accent, 60 warning, 75 orange, >=92 error, aralar dogrusal karisim.
// NaN 0 sayilir (veri yoksa cizim katmani zaten "—" gosterir; rampa alarm uretmez).

#include <cstdint>
#include <optional>
#include <string_view>

#include "ui/theme/theme.h"

namespace ef::ui {

Color usage_color(const Palette& p, double pct);
// i18n anahtari: "live.usage.low" (<55), "live.usage.mid" (<70), "live.usage.high" (<88), "live.usage.max" (>=88).
std::string_view usage_word_key(double pct);
// >= 88: kart cizgisi kirmizi.
bool usage_alarm(double pct);
// >= 75: kutucuk ikon kutusu parlar (yalniz koyu temada ve hareket azaltilmamisken; cizim katmani karar verir).
bool usage_glow(double pct);
// Kisa kullanim kelimesi (kutucukta gorunen): "live.usage_short.low|mid|high|max"; esikler usage_word_key ile ayni.
std::string_view usage_short_word_key(double pct);

// Siki yerlesim (spec 2026-11-11 §4): 12 parcali LED cubuk. Parca i (0 tabanli) (i + 1) / 12 * 100 <= deger + 4 ise
// yanar; yanan parcanin rengi kendi konumunun rampa rengidir: usage_color(p, led_segment_pct(i)). Deger [0,100]
// araligina kirpilir; NaN hic yakmaz. Aralik disi i yanmaz.
inline constexpr int kLedSegments = 12;
double led_segment_pct(int i);  // (i + 1) / 12 * 100; aralik disi i -> 0
bool led_segment_lit(int i, double pct);
int led_lit_count(double pct);

// Tepe izi (peak-hold): LED cubugunda son tepenin parcasi soluk isaretlenir. Zamana baglidir (kare hizindan
// bagimsiz): tepe kLedPeakHoldMs boyunca durur, sonra kLedPeakDecayPerSec puan/sn iner ve gecerli degere yetisince
// sifirlanir. Azaltilmis harekette inis yok: bekleme dolunca tepe dogrudan gecerli degere duser. Ek kare istemez:
// yalniz zaten cizilen karelerde (yeni canli ornek / gecis) hesaplanir. Deger yoksa (nullopt / NaN) iz silinir.
struct LedPeak {
    double pct = -1;  // < 0: iz yok
    std::int64_t at_ms = 0;
};
inline constexpr std::int64_t kLedPeakHoldMs = 1500;
inline constexpr double kLedPeakDecayPerSec = 20.0;
// Gozlemi isler; gosterilecek tepe (>= gecerli deger) ya da deger yoksa nullopt.
std::optional<double> led_peak_update(LedPeak& pk, std::optional<double> pct, std::int64_t now_ms, bool reduced_motion);
// Isaretlenecek tepe parcasi: tepenin en ust yanan parcasi gecerli degerle zaten yanmiyorsa onun dizini; degilse -1.
int led_peak_segment(double peak, double pct);

}  // namespace ef::ui
