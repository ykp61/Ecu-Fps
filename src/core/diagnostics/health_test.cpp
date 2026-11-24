#include "core/diagnostics/health_test.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace ef::core::diagnostics {

namespace {
using readiness::Check;
using readiness::Level;

double median_of(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

std::uint64_t next_rand(std::uint64_t& s) {  // xorshift64*: tohum 0 olmaz
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ull;
}

std::uint64_t word_at(std::size_t i, RamPattern p, std::uint64_t& rng) {
    switch (p) {
        case RamPattern::WalkingOnes: return 1ull << (i % 64);
        case RamPattern::WalkingOnesInverse: return ~(1ull << (i % 64));
        case RamPattern::Prng: return next_rand(rng);
        case RamPattern::PrngInverse: return ~next_rand(rng);
    }
    return 0;
}
}  // namespace

std::optional<CpuSummary> cpu_summary(const std::vector<CpuSample>& samples) {
    if (samples.size() < kHealthMinCpuSamples) return std::nullopt;
    std::vector<double> first, last;
    for (std::size_t i = 0; i < 10; ++i) first.push_back(samples[i].pct_of_max_freq);
    for (std::size_t i = samples.size() - 20; i < samples.size(); ++i) last.push_back(samples[i].pct_of_max_freq);
    CpuSummary s;
    s.first_median = median_of(first);
    s.last_median = median_of(last);
    for (const auto& c : samples)
        if (c.temp_c && (!s.max_temp_c || *c.temp_c > *s.max_temp_c)) s.max_temp_c = c.temp_c;
    return s;
}

CpuVerdict cpu_verdict(const std::vector<CpuSample>& samples) {
    const auto sum = cpu_summary(samples);
    if (!sum || sum->first_median <= 0.0) return CpuVerdict::Insufficient;
    if (sum->last_median < 0.85 * sum->first_median) return CpuVerdict::Throttled;
    std::size_t with_limit = 0, limited = 0;
    for (const auto& c : samples)
        if (c.perf_limit_pct) {
            ++with_limit;
            if (*c.perf_limit_pct < 100.0) ++limited;
        }
    if (with_limit > 0 && limited * 10 > with_limit) return CpuVerdict::Throttled;  // > %10
    return CpuVerdict::Stable;
}

void ram_fill(std::uint64_t* buf, std::size_t words, RamPattern p, std::uint64_t seed) {
    std::uint64_t rng = seed ? seed : 0x9E3779B97F4A7C15ull;
    for (std::size_t i = 0; i < words; ++i) buf[i] = word_at(i, p, rng);
}

std::uint64_t ram_verify(const std::uint64_t* buf, std::size_t words, RamPattern p, std::uint64_t seed) {
    std::uint64_t rng = seed ? seed : 0x9E3779B97F4A7C15ull;
    std::uint64_t bad = 0;
    for (std::size_t i = 0; i < words; ++i)
        if (buf[i] != word_at(i, p, rng)) ++bad;
    return bad;
}

RamVerdict ram_verdict(const RamResult& r) {
    if (r.tested_bytes == 0) return RamVerdict::NotRun;
    if (r.errors > 0) return RamVerdict::Errors;
    return r.completed ? RamVerdict::Ok : RamVerdict::Partial;
}

std::uint64_t ram_test_bytes(std::uint64_t free_bytes) {
    if (free_bytes <= kHealthRamKeepFreeBytes) return 0;
    std::uint64_t n = std::min<std::uint64_t>(free_bytes / 4, kHealthRamMaxBytes);
    n = std::min<std::uint64_t>(n, free_bytes - kHealthRamKeepFreeBytes);
    n &= ~std::uint64_t{4095};
    return n < kHealthRamMinBytes ? 0 : n;
}

GpuVerdict gpu_verdict(bool available, bool device_removed_or_reset, bool new_display_incident) {
    if (!available) return GpuVerdict::Unavailable;
    return (device_removed_or_reset || new_display_incident) ? GpuVerdict::Faulted : GpuVerdict::Stable;
}

std::vector<Check> health_test_checks(bool running, int phase, int elapsed_s, const std::optional<HealthReport>& report) {
    std::vector<Check> v;
    Check head;
    head.id = "hardware.health_test";
    head.section = readiness::Section::Hardware;
    head.level = Level::Info;
    head.text_key = "chk.hardware.health_test";
    if (running) {
        head.text_key = "chk.hardware.health_test.running";
        head.args["step"] = phase == 2 ? "2" : "1";
        head.args["what"] = phase == 2 ? "RAM" : "CPU";
        head.args["sec"] = std::to_string(std::max(0, elapsed_s));
        head.args["total"] = std::to_string(phase == 2 ? kHealthRamSeconds : kHealthCpuSeconds);
        v.push_back(std::move(head));
        return v;  // olcum suruyor: dugme yok
    }
    head.action = readiness::ActionKind::RunHealthTest;
    if (report && report->refusal == HealthRefusal::BatterySaver) {
        head.text_key = "chk.hardware.health_test.refused_battery_saver";
        v.push_back(std::move(head));
        return v;
    }
    v.push_back(std::move(head));
    if (!report) return v;  // sonuc yok: sayi yok

    auto row = [](const char* id, Level lv, const char* key) {
        Check c;
        c.id = id;
        c.section = readiness::Section::Hardware;
        c.level = lv;
        c.text_key = key;
        return c;
    };
    {  // CPU
        const auto cv = cpu_verdict(report->cpu);
        const auto sum = cpu_summary(report->cpu);
        Check c = row("hardware.health_test.cpu", Level::Info, "chk.hardware.health_test.cpu_stable");
        if (cv == CpuVerdict::Throttled) {
            c.level = Level::Advice;
            c.text_key = "chk.hardware.health_test.cpu_throttled";
        } else if (cv == CpuVerdict::Insufficient) {
            c.level = Level::Unknown;
            c.text_key = "chk.hardware.health_test.cpu_insufficient";
        }
        if (sum) {
            char a[32], b[32];
            std::snprintf(a, sizeof a, "%.0f", sum->first_median);
            std::snprintf(b, sizeof b, "%.0f", sum->last_median);
            c.args["first"] = a;
            c.args["last"] = b;
        }
        v.push_back(std::move(c));
        Check t = row("hardware.health_test.temp", Level::Info, "chk.hardware.health_test.no_sensor");
        if (sum && sum->max_temp_c) {
            char tb[32];
            std::snprintf(tb, sizeof tb, "%.0f", *sum->max_temp_c);
            t.text_key = "chk.hardware.health_test.temp";
            t.args["temp"] = tb;
        }
        v.push_back(std::move(t));
    }
    if (report->ram_run) {  // RAM
        const auto rv = ram_verdict(report->ram);
        Check c = row("hardware.health_test.ram", Level::Info, "chk.hardware.health_test.ram_ok");
        c.args["mib"] = std::to_string(report->ram.tested_bytes >> 20);
        c.args["errors"] = std::to_string(report->ram.errors);
        if (rv == RamVerdict::Errors) {
            c.level = Level::Problem;
            c.text_key = "chk.hardware.health_test.ram_errors";
        } else if (rv == RamVerdict::Partial) {
            c.level = Level::Advice;
            c.text_key = "chk.hardware.health_test.ram_partial";
        } else if (rv == RamVerdict::NotRun) {
            c.level = Level::Unknown;
            c.text_key = "chk.hardware.health_test.ram_not_run";
        }
        v.push_back(std::move(c));
    }
    {  // GPU
        Check c = row("hardware.health_test.gpu", Level::Info, "chk.hardware.health_test.gpu_unavailable");
        if (report->gpu == GpuVerdict::Stable) c.text_key = "chk.hardware.health_test.gpu_stable";
        if (report->gpu == GpuVerdict::Faulted) {
            c.level = Level::Problem;
            c.text_key = "chk.hardware.health_test.gpu_faulted";
        }
        v.push_back(std::move(c));
    }
    return v;
}

}  // namespace ef::core::diagnostics
