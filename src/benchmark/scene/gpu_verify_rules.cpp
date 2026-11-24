// EF-X22: GPU hesap dogrulamasinin saf kurallari (D3D yok; birim testlenir).
#include <bit>
#include <cmath>

#include "benchmark/scene/gpu_verify.h"

namespace ef::benchmark::gpu_verify {

// HLSL shaders/verify.hlsl:verify_word ile bire bir ayni. Float ara degerleri tamsayi ve < 2^24: tam.
std::uint32_t reference_word(std::uint32_t index, std::uint32_t seed, std::uint32_t iterations) noexcept {
    std::uint32_t x = (index * 0x9E3779B9u) ^ seed;
    if (x == 0) x = 0x6D2B79F5u;
    std::uint32_t acc = index;
    for (std::uint32_t k = 0; k < iterations; ++k) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const float a = static_cast<float>(x & 0xFFFu);
        const float b = static_cast<float>((x >> 12) & 0xFFFu);
        const float p = a * b + a;  // <= 16773120 < 2^24
        acc = acc * 1664525u + (static_cast<std::uint32_t>(p) ^ k);
    }
    return acc ^ x;
}

void reference_fill(std::vector<std::uint32_t>& out, std::uint32_t words, std::uint32_t seed, std::uint32_t iterations) {
    out.resize(words);
    for (std::uint32_t i = 0; i < words; ++i) out[i] = reference_word(i, seed, iterations);
}

std::optional<std::uint64_t> ulp_distance(std::uint32_t a_bits, std::uint32_t b_bits) noexcept {
    const float a = std::bit_cast<float>(a_bits), b = std::bit_cast<float>(b_bits);
    if (!std::isfinite(a) || !std::isfinite(b)) return std::nullopt;
    // Isaret-buyukluk -> sirali tamsayi: -0 ve +0 ayni noktaya duser.
    auto ord = [](std::uint32_t v) -> std::int64_t {
        const std::int64_t mag = static_cast<std::int64_t>(v & 0x7FFFFFFFu);
        return (v & 0x80000000u) ? -mag : mag;
    };
    const std::int64_t d = ord(a_bits) - ord(b_bits);
    return static_cast<std::uint64_t>(d < 0 ? -d : d);
}

bool word_matches(std::uint32_t ref, std::uint32_t got, const Tolerance& t) noexcept {
    if (ref == got) return true;
    if (t.kind == Tolerance::Kind::Exact) return false;
    const auto d = ulp_distance(ref, got);
    return d && *d <= t.max_ulp;
}

CompareResult compare_words(const std::uint32_t* ref, std::size_t ref_n, const std::uint32_t* got, std::size_t got_n,
                            const Tolerance& t) noexcept {
    CompareResult r;
    const std::size_t n = ref_n < got_n ? ref_n : got_n;
    for (std::size_t i = 0; i < n; ++i)
        if (!word_matches(ref[i], got[i], t)) {
            if (!r.first_mismatch_index) r.first_mismatch_index = static_cast<std::uint32_t>(i);
            ++r.mismatched_words;
        }
    const std::size_t extra = ref_n > got_n ? ref_n - got_n : got_n - ref_n;
    if (extra) {
        if (!r.first_mismatch_index) r.first_mismatch_index = static_cast<std::uint32_t>(n);
        r.mismatched_words += extra;
    }
    return r;
}

Config quick_config() { return Config{}; }

Config full_config() {
    Config c;
    c.words = 1u << 18;  // 1 MiB cikti
    c.min_repeats = 8;
    c.max_repeats = 128;
    c.time_budget_ms = 10000;
    return c;
}

std::uint32_t dispatch_groups(std::uint32_t words) noexcept { return (words + kThreadsPerGroup - 1) / kThreadsPerGroup; }

std::uint64_t planned_bytes(const Config& c) noexcept { return 3ull * c.words * sizeof(std::uint32_t); }

ConfigVerdict validate_config(const Config& c) noexcept {
    if (c.words == 0 || c.words > kMaxWords || c.words % kThreadsPerGroup != 0) return ConfigVerdict::WordsInvalid;
    if (c.iterations == 0 || c.iterations > kMaxIterations) return ConfigVerdict::IterationsInvalid;
    if (c.min_repeats == 0 || c.max_repeats < c.min_repeats || c.max_repeats > kMaxRepeats) return ConfigVerdict::RepeatsInvalid;
    if (c.time_budget_ms == 0 || c.wait_timeout_ms == 0) return ConfigVerdict::TimeInvalid;
    if (planned_bytes(c) > kMaxBytes) return ConfigVerdict::MemoryOverBudget;
    return ConfigVerdict::Ok;
}

const char* config_reason(ConfigVerdict v) noexcept {
    switch (v) {
        case ConfigVerdict::Ok: return "";
        case ConfigVerdict::WordsInvalid:
        case ConfigVerdict::IterationsInvalid:
        case ConfigVerdict::RepeatsInvalid:
        case ConfigVerdict::TimeInvalid:
        case ConfigVerdict::MemoryOverBudget: return "gpu_verify_invalid";
    }
    return "gpu_verify_invalid";
}

bool may_start_repeat(std::uint32_t done, const Config& c, double elapsed_ms, double last_repeat_ms) noexcept {
    if (done >= c.max_repeats) return false;
    // Bir sonraki tekrar butceyi asacaksa baslatilmaz (son tekrar suresi tahmindir; ilk tekrar her zaman denenir).
    const double next = last_repeat_ms > 0 ? last_repeat_ms : 0;
    return elapsed_ms + next <= static_cast<double>(c.time_budget_ms);
}

std::string_view to_string(Status s) noexcept {
    switch (s) {
        case Status::NotRun: return "not_run";
        case Status::Ok: return "ok";
        case Status::Mismatch: return "mismatch";
        case Status::Unsupported: return "unsupported";
        case Status::SetupFailed: return "setup_failed";
        case Status::DeviceRemoved: return "device_removed";
        case Status::GpuTimeout: return "gpu_timeout";
        case Status::ReadbackFailed: return "readback_failed";
        case Status::Cancelled: return "cancelled";
        case Status::TimeLimit: return "time_limit";
        case Status::Invalid: return "invalid";
    }
    return "invalid";
}

Verdict verdict(Status s) noexcept {
    switch (s) {
        case Status::Ok: return {Effect::Pass, {}};
        case Status::Mismatch: return {Effect::FailNoScore, "gpu_compute_mismatch"};
        case Status::DeviceRemoved: return {Effect::FailNoScore, "device_removed"};
        case Status::GpuTimeout: return {Effect::FailNoScore, "gpu_timeout"};
        case Status::ReadbackFailed: return {Effect::FailNoScore, "gpu_verify_readback"};
        case Status::Invalid: return {Effect::FailNoScore, "gpu_verify_invalid"};
        case Status::TimeLimit: return {Effect::Partial, "gpu_verify_incomplete"};
        case Status::Cancelled: return {Effect::Cancelled, "user_cancel"};
        case Status::Unsupported: return {Effect::NoClaim, "gpu_verify_unsupported"};
        case Status::SetupFailed: return {Effect::NoClaim, "gpu_verify_setup_failed"};
        case Status::NotRun: return {Effect::NoClaim, {}};
    }
    return {Effect::NoClaim, {}};
}

}  // namespace ef::benchmark::gpu_verify
