// T06: fare rapor hizi bulgusu. Yalniz kullanici "Olc" dediginde yapilan gercek olcumun sonucunu yazar;
// olcum yoksa sayi yoktur. Bilgi satiri (Level::Info): puana/payda girmez, "iyi" sayilmaz.
#include <cstdio>

#include "core/readiness/readiness.h"

namespace ef::core::readiness {

Check mouse_polling_check(bool measuring, const std::optional<measure::PollingResult>& result) {
    Check c;
    c.id = "latency.mouse_polling";
    c.section = Section::Latency;
    c.level = Level::Info;
    c.text_key = "chk.latency.mouse_polling";
    if (measuring) {
        c.text_key = "chk.latency.mouse_polling.measuring";
        return c;  // olcum suruyor: dugme yok
    }
    c.action = ActionKind::MeasureMouse;
    if (!result) return c;
    if (result->state != measure::PollingResult::State::Ok) {
        c.text_key = "chk.latency.mouse_polling.insufficient";
        return c;
    }
    char hz[32], ms[32];
    std::snprintf(hz, sizeof hz, "%.0f", result->hz);
    std::snprintf(ms, sizeof ms, "%.2f", result->median_interval_ms);
    c.args["hz"] = hz;
    c.args["ms"] = ms;
    c.text_key = result->at_least ? "chk.latency.mouse_polling.result_min" : "chk.latency.mouse_polling.result";
    return c;
}

}  // namespace ef::core::readiness
