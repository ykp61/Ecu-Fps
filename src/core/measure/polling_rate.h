#pragma once
// T06: fare rapor hizi (Hz) tahmini (saf; birim testlenir). Girdi: her fare hareket raporunun QPC zaman damgasi.
//  - 20 ms'den uzun aralik (fare durdu) ve <= 0 aralik (ayni zaman damgasini paylasan toplu kayit) dislanir.
//  - Kalan gecerli aralik sayisi < kMinPollingIntervals ise Insufficient; sayi uretilmez.
//  - hz = 1000 / medyan aralik (ms). "Nominal" degere (125/500/1000) yuvarlanmaz; UI olculen degeri "~" ile gosterir.
//  - at_least: toplu (birlesmis) rapor goruldu ya da hz > 2000: Windows yuksek hizda WM_INPUT'u birlestirir, kayit basina
//    zaman damgasi yoktur; deger alt sinirdir ("en az"). Tahmin ust sinira cikarilmaz.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ef::core::measure {

inline constexpr std::size_t kMinPollingIntervals = 300;
inline constexpr double kMaxPollingIntervalMs = 20.0;
inline constexpr double kPollingAtLeastAboveHz = 2000.0;

struct PollingResult {
    enum class State { Ok, Insufficient } state = State::Insufficient;
    double hz = 0.0;
    double median_interval_ms = 0.0;
    double p95_interval_ms = 0.0;
    std::size_t samples = 0;  // kullanilan gecerli aralik sayisi
    bool at_least = false;    // deger alt sinir (birlesmis rapor / yuksek hiz)
};

PollingResult estimate_polling_rate(const std::vector<std::int64_t>& qpc_ticks, std::int64_t qpc_freq);

}  // namespace ef::core::measure
