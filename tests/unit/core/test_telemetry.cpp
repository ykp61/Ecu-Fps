// MEAS-03: ornek (samples) zaman cizgisi ve VRAM kaynak kurallari.
#include "core/measure/telemetry.h"
#include "ef_test.h"

using namespace ef::core::measure;

EF_TEST(telemetry_1hz_never_root_cause_for_ms_spike) {
    EF_CHECK(!can_attribute(1000, 40));   // 1 Hz sayac 40 ms takilmayi aciklayamaz
    EF_CHECK(!can_attribute(1000, 1500));  // yarim sure kurali
    EF_CHECK(can_attribute(1000, 2500));
    EF_CHECK(can_attribute(5, 40));        // 200 Hz kaynak aday olabilir
    std::vector<TelemetrySample> v;
    for (int i = 1; i <= 5; ++i) v.push_back({"cpu_pct", MetricSource::Pdh, i * 1000.0, 1000, 50.0 + i, {}});
    v.push_back({"gpu_pct", MetricSource::Pdh, 3000, 1000, std::nullopt, "counter_unavailable"});
    const auto w = context_for_window(v, "cpu_pct", 2480, 2520);  // 40 ms takilma
    EF_CHECK(w.value && w.attribution == Attribution::ContextOnly);
    EF_CHECK(context_for_window(v, "gpu_pct", 2480, 2520).attribution == Attribution::NoData);  // eksik deger tahmin edilmez
    EF_CHECK(context_for_window(v, "cpu_pct", 9000, 9100).attribution == Attribution::NoData);  // kapsam disi
}

EF_TEST(telemetry_own_dxgi_budget_is_not_game_vram) {
    const std::uint32_t game = 4242, self = 77;
    EF_CHECK(!vram_valid_for_game({VramScope::OwnProcessDxgiBudget, self, 8ull << 30}, game));
    EF_CHECK(!vram_valid_for_game({VramScope::OwnProcessDxgiBudget, game, 8ull << 30}, game));  // PID ayni olsa bile kaynak yanlis
    EF_CHECK(vram_valid_for_game({VramScope::ProcessUsage, game, 6ull << 30}, game));
    EF_CHECK(!vram_valid_for_game({VramScope::ProcessUsage, 999, 6ull << 30}, game));
    EF_CHECK(!vram_valid_for_game({VramScope::AdapterTotal, 0, 12ull << 30}, game));
    EF_CHECK(!vram_valid_for_game({VramScope::ProcessUsage, game, std::nullopt}, game));
}
