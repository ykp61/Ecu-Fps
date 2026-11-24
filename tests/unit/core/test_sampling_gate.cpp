#include "core/metrics/sampling.h"
#include "ef_test.h"

using ef::core::metrics::SamplingGate;

EF_TEST(live_sampling_only_while_visible_at_1hz) {
    SamplingGate g(1000);
    // Gorunmez: 10 sn boyunca hic ornek yok
    for (std::int64_t t = 0; t < 10000; t += 100) EF_CHECK(!g.should_sample(t));
    EF_CHECK(g.taken() == 0 && g.wait_ms(0) == -1);
    // Gorunur: 10 sn'de ~10 ornek (hemen ilk ornek + her saniye)
    g.set_visible(true);
    for (std::int64_t t = 10000; t < 20000; t += 50) g.should_sample(t);
    EF_CHECK(g.taken() == 10);
    // Simge durumuna: ornek durur
    g.set_visible(false);
    const auto before = g.taken();
    for (std::int64_t t = 20000; t < 40000; t += 50) g.should_sample(t);
    EF_CHECK(g.taken() == before);
    // Yeniden gorunur: bekletmeden ilk ornek
    g.set_visible(true);
    EF_CHECK(g.should_sample(40000) && !g.should_sample(40500) && g.should_sample(41000));
}
