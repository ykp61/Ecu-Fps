#include "core/measure/balance_log.h"

#include <cmath>
#include <cstdio>

namespace ef::core::measure {

namespace {
std::string fixed(double v, int digits) {
    if (!std::isfinite(v)) return "-";
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", digits, v);
    return buf;
}
std::string opt(const std::optional<double>& v, int digits) { return v ? fixed(*v, digits) : std::string("-"); }
// Anahtar degerleri bosluk / satir sonu tasimaz (tek satir, ayristirilabilir).
std::string token(std::string_view s) {
    if (s.empty()) return "-";
    std::string out;
    for (const char c : s) out.push_back(c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '=' ? '_' : c);
    return out;
}
}  // namespace

std::string balance_step_log_line(const BalanceStepLog& s) {
    std::string out = "step=" + token(s.step);
    out += " status=" + token(s.status);
    out += " attempts=" + std::to_string(s.attempts);
    out += " frames=" + std::to_string(s.frames);
    out += " fps=" + (s.frames > 0 ? fixed(s.fps, 1) : std::string("-"));
    out += " cv=" + (s.frames > 0 ? fixed(s.cv, 4) : std::string("-"));
    out += " gpu_busy=" + opt(s.gpu_busy, 3);
    out += " disjoint=" + std::to_string(s.disjoint_frames);
    out += " invalid=" + token(s.invalid);
    out += " other_cpu=" + opt(s.other_cpu_pct, 1);
    out += " error=" + token(s.error);
    return out;
}

std::string balance_run_log_line(const BalanceRunLog& r) {
    std::string out = "run rule=" + token(r.rule);
    char vid[16] = "-";
    if (r.adapter_vendor_id) std::snprintf(vid, sizeof vid, "%04X", *r.adapter_vendor_id);
    out += " vendor=";
    out += vid;
    out += std::string(" mode=") + (r.quick ? "quick" : "full");
    out += std::string(" ok=") + (r.ok ? "1" : "0");
    out += std::string(" cancelled=") + (r.cancelled ? "1" : "0");
    out += " retries=" + std::to_string(r.retries);
    out += " other_cpu=" + opt(r.other_cpu_pct, 1);
    out += " error=" + token(r.error);
    return out;
}

}  // namespace ef::core::measure
