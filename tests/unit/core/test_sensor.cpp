// EF-X24: sensor sozlesmesi; T01 eski deger, T04 takilan kaynak. Sahte saglayici.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "core/metrics/sensor.h"
#include "ef_test.h"

using namespace ef::core::metrics;
using ef::contracts::Measurement;
using ef::contracts::MissingReason;
using ef::contracts::Unit;

EF_TEST(x24_t01_stale_value_is_not_current) {
    SensorValue s;
    s.value = Measurement::of(61.0, Unit::Celsius);
    s.captured_ms = 1000;
    EF_CHECK(fresh_value(s, 2000).has_value());
    EF_CHECK(!fresh_value(s, 1000 + kSensorMaxAgeMs + 1).has_value());
    EF_CHECK(!fresh_value(s, 500).has_value());  // gelecekten gelen okuma
    s.captured_ms = -1;
    EF_CHECK(!fresh_value(s, 2000).has_value());
    SensorValue none;
    EF_CHECK(fresh_value(none, 2000).missing_reason() == MissingReason::NotSampled);
}

// Takilan kaynak SURE ile degil bir olayla (event) takilir: test serbest birakana kadar bitmez. Eski surum 400 ms uyuyor ve
// "okuyucu 300 ms'den kisa surede dondu" diye duvar saatine bakiyordu; yuk altinda (paralel derleme) zamanlayici 350 ms
// gecikince kaynak "bitmis" sayilip zaman asimi olmuyor ya da okuyucu 300 ms'yi asiyordu. Simdi: okuyucunun beklemedigi,
// kaynak hala takiliyken (finished == false) okuma donmus olmasindan kanitlanir; sayac icin kaynagin basladigi beklenir.
EF_TEST(x24_t04_hanging_source_times_out_and_circuit_opens) {
    struct Gate {
        std::mutex mu;
        std::condition_variable cv;
        bool started = false, release = false, finished = false;
    };
    auto gate = std::make_shared<Gate>();
    auto calls = std::make_shared<std::atomic<int>>(0);
    TimedSensor hung(
        [calls, gate] {
            ++*calls;
            std::unique_lock lk(gate->mu);
            gate->started = true;
            gate->cv.notify_all();
            gate->cv.wait(lk, [&] { return gate->release; });
            gate->finished = true;
            gate->cv.notify_all();
            return SensorValue{};
        },
        50, 10000);
    const auto v = hung.read(0);
    EF_CHECK(!v.value.has_value() && hung.timeouts() == 1);
    {
        std::unique_lock lk(gate->mu);
        EF_CHECK(!gate->finished);  // okuyucu beklemedi: kaynak hala takili iken okuma dondu
        EF_CHECK(gate->cv.wait_for(lk, std::chrono::seconds(10), [&] { return gate->started; }));  // cagri gercekten yapildi
    }
    EF_CHECK(hung.open_circuit(5000) && !hung.read(5000).value.has_value() && *calls == 1);  // devre acik: cagri yok
    {
        std::unique_lock lk(gate->mu);
        gate->release = true;
        gate->cv.notify_all();
        EF_CHECK(gate->cv.wait_for(lk, std::chrono::seconds(10), [&] { return gate->finished; }));  // ayrik is parcacigi bitsin
    }
}

EF_TEST(x24_working_source_returns_labelled_value) {
    TimedSensor ok([] {
        SensorValue s;
        s.value = Measurement::of(55.0, Unit::Celsius);
        s.source = "nvapi";
        s.device = "GPU A";
        s.captured_ms = 10;
        return s;
    });
    const auto v = ok.read(10);
    EF_CHECK(v.value.has_value() && v.source == "nvapi" && v.device == "GPU A" && ok.timeouts() == 0);
}
