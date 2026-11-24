#include "core/metrics/sensor.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace ef::core::metrics {

contracts::Measurement fresh_value(const SensorValue& s, std::int64_t now_ms, std::int64_t max_age_ms) {
    if (!s.value.has_value()) return s.value;
    if (s.captured_ms < 0 || s.captured_ms > now_ms || now_ms - s.captured_ms > max_age_ms)
        return contracts::Measurement::missing(s.value.unit(), contracts::MissingReason::NotSampled);
    return s.value;
}

struct TimedSensor::Shared {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    SensorValue result;
};

TimedSensor::TimedSensor(Fn fn, std::int64_t deadline_ms, std::int64_t cooldown_ms)
    : fn_(std::move(fn)), deadline_ms_(deadline_ms), cooldown_ms_(cooldown_ms) {}

SensorValue TimedSensor::read(std::int64_t now_ms) {
    SensorValue missing;
    missing.value = contracts::Measurement::missing(contracts::Unit::Celsius, contracts::MissingReason::SourceError);
    if (!fn_ || open_circuit(now_ms)) return missing;
    if (busy_) {
        std::lock_guard lk(busy_->mu);
        if (!busy_->done) return missing;  // onceki cagri hala takili: ust uste is parcacigi yok
    }
    auto shared = std::make_shared<Shared>();
    busy_ = shared;
    std::thread([shared, fn = fn_] {
        SensorValue v;
        try {
            v = fn();
        } catch (...) {
            v.value = contracts::Measurement::missing(contracts::Unit::Celsius, contracts::MissingReason::SourceError);
        }
        std::lock_guard lk(shared->mu);
        shared->result = std::move(v);
        shared->done = true;
        shared->cv.notify_all();
    }).detach();
    std::unique_lock lk(shared->mu);
    if (!shared->cv.wait_for(lk, std::chrono::milliseconds(deadline_ms_), [&] { return shared->done; })) {
        ++timeouts_;
        blocked_until_ = now_ms + cooldown_ms_;
        return missing;
    }
    return shared->result;
}

}  // namespace ef::core::metrics
