#pragma once
// EF-X24: sensor degeri sozlesmesi ve guvenli okuma (saf; birim testlenir).
//  - Her deger: birim + kaynak + cihaz kimligi + okuma zamani. Eski deger "simdi" diye gosterilmez.
//  - Kaynak takilirsa (surucu cagrisi donmuyor) okuyucu bekletilmez: sure asimi + devre kesici.
//  - Desteklenmeyen deger 0 degil, eksik. Uydurma/tahmini sensor yok.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "contracts/measure.h"

namespace ef::core::metrics {

struct SensorValue {
    contracts::Measurement value = contracts::Measurement::missing(contracts::Unit::Celsius, contracts::MissingReason::NotSampled);
    std::string source;     // "nvapi" ...
    std::string device;     // okunan cihazin adi (karma sistemde hangi GPU)
    std::int64_t captured_ms = -1;  // monoton zaman; -1 = yok
};

inline constexpr std::int64_t kSensorMaxAgeMs = 3000;
// Eski veya zamansiz deger: missing(NotSampled). Gelecekten gelen zaman da gecersiz.
contracts::Measurement fresh_value(const SensorValue& s, std::int64_t now_ms, std::int64_t max_age_ms = kSensorMaxAgeMs);

// Takilan kaynaga karsi: cagri ayri is parcaciginda, deadline asilirsa sonuc beklenmez ve devre
// cooldown boyunca acik kalir (o surede kaynak hic cagrilmaz). Takilan cagri bitince yeniden denenebilir.
class TimedSensor {
public:
    using Fn = std::function<SensorValue()>;
    TimedSensor(Fn fn, std::int64_t deadline_ms = 500, std::int64_t cooldown_ms = 30000);
    SensorValue read(std::int64_t now_ms);
    bool open_circuit(std::int64_t now_ms) const noexcept { return now_ms < blocked_until_; }
    int timeouts() const noexcept { return timeouts_; }

private:
    struct Shared;
    Fn fn_;
    std::int64_t deadline_ms_, cooldown_ms_;
    std::int64_t blocked_until_ = 0;
    int timeouts_ = 0;
    std::shared_ptr<Shared> busy_;  // takili cagri bitmeden yenisi baslatilmaz
};

}  // namespace ef::core::metrics
