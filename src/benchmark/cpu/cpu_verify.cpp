#include "benchmark/cpu/cpu_verify.h"
#include "benchmark/cpu/cpu_groups.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <new>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_M_X64) || defined(_M_IX86)
#define EF_CPU_VERIFY_X86 1
#include <immintrin.h>
#include <intrin.h>
#else
#define EF_CPU_VERIFY_X86 0
#endif

namespace ef::benchmark::cpu_verify {

std::string_view isa_name(Isa i) noexcept {
    switch (i) {
        case Isa::Scalar: return "scalar";
        case Isa::Sse2: return "sse2";
        case Isa::Avx2: return "avx2";
        case Isa::Avx512: return "avx512";
    }
    return "scalar";
}

CpuidBits read_cpuid() noexcept {
    CpuidBits b;
#if EF_CPU_VERIFY_X86
    int r[4]{};
    __cpuid(r, 0);
    const int max_leaf = r[0];
    if (max_leaf < 1) return b;
    b.cpuid_ok = true;
    __cpuid(r, 1);
    b.sse2 = (r[3] & (1 << 26)) != 0;
    b.osxsave = (r[2] & (1 << 27)) != 0;
    b.avx = (r[2] & (1 << 28)) != 0;
    if (max_leaf >= 7) {
        __cpuidex(r, 7, 0);
        b.avx2 = (r[1] & (1 << 5)) != 0;
        b.avx512f = (r[1] & (1 << 16)) != 0;
    }
    // XGETBV yalniz isletim sistemi XSAVE'i etkinlestirdiyse gecerlidir (aksi halde #UD).
    if (b.osxsave) b.xcr0 = _xgetbv(0);
#endif
    return b;
}

bool isa_supported(Isa i, const CpuidBits& b) noexcept {
    constexpr std::uint64_t kYmm = 0x6;    // XCR0[1] SSE + XCR0[2] AVX durumu
    constexpr std::uint64_t kZmm = 0xE6;   // + XCR0[5] opmask, [6] ZMM_Hi256, [7] Hi16_ZMM
    switch (i) {
        case Isa::Scalar: return true;
        case Isa::Sse2: return b.cpuid_ok && b.sse2;
        case Isa::Avx2: return b.cpuid_ok && b.osxsave && b.avx && b.avx2 && (b.xcr0 & kYmm) == kYmm;
        case Isa::Avx512: return b.cpuid_ok && b.osxsave && b.avx512f && (b.xcr0 & kZmm) == kZmm;
    }
    return false;
}

void fill_input(std::uint32_t* out, std::uint32_t seed, std::uint64_t block) noexcept {
    for (std::uint32_t i = 0; i < kWordsPerBlock; ++i) {
        // splitmix64 (tohum, blok, indeks): komsu kelime/blok farkli girdi alir.
        std::uint64_t z = (block * kWordsPerBlock + i) ^ (static_cast<std::uint64_t>(seed) << 32);
        z += 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        out[i] = static_cast<std::uint32_t>(z);
    }
}

std::uint32_t round_constant(std::uint32_t seed, std::uint32_t round) noexcept {
    return seed * 0x9E3779B9u + round * 0x632BE5ABu + 0x7F4A7C15u;
}

std::uint32_t reference_word(std::uint32_t x, std::uint32_t seed, std::uint32_t rounds) noexcept {
    for (std::uint32_t r = 0; r < rounds; ++r) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        x += round_constant(seed, r);
        x = (x << 7) | (x >> 25);
    }
    return x;
}

namespace {

void transform_scalar(const std::uint32_t* in, std::uint32_t* out, std::uint32_t seed, std::uint32_t rounds) noexcept {
    for (std::uint32_t i = 0; i < kWordsPerBlock; ++i) out[i] = in[i];
    for (std::uint32_t r = 0; r < rounds; ++r) {
        const std::uint32_t k = round_constant(seed, r);
#pragma loop(no_vector)
        for (std::uint32_t i = 0; i < kWordsPerBlock; ++i) {
            std::uint32_t x = out[i];
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            x += k;
            out[i] = (x << 7) | (x >> 25);
        }
    }
}

#if EF_CPU_VERIFY_X86
void transform_sse2(const std::uint32_t* in, std::uint32_t* out, std::uint32_t seed, std::uint32_t rounds) noexcept {
    for (std::uint32_t i = 0; i < kWordsPerBlock; ++i) out[i] = in[i];
    for (std::uint32_t r = 0; r < rounds; ++r) {
        const __m128i k = _mm_set1_epi32(static_cast<int>(round_constant(seed, r)));
        for (std::uint32_t i = 0; i < kWordsPerBlock; i += 4) {
            auto* p = reinterpret_cast<__m128i*>(out + i);
            __m128i x = _mm_loadu_si128(p);
            x = _mm_xor_si128(x, _mm_slli_epi32(x, 13));
            x = _mm_xor_si128(x, _mm_srli_epi32(x, 17));
            x = _mm_xor_si128(x, _mm_slli_epi32(x, 5));
            x = _mm_add_epi32(x, k);
            x = _mm_or_si128(_mm_slli_epi32(x, 7), _mm_srli_epi32(x, 25));
            _mm_storeu_si128(p, x);
        }
    }
}

void transform_avx2(const std::uint32_t* in, std::uint32_t* out, std::uint32_t seed, std::uint32_t rounds) noexcept {
    for (std::uint32_t i = 0; i < kWordsPerBlock; ++i) out[i] = in[i];
    for (std::uint32_t r = 0; r < rounds; ++r) {
        const __m256i k = _mm256_set1_epi32(static_cast<int>(round_constant(seed, r)));
        for (std::uint32_t i = 0; i < kWordsPerBlock; i += 8) {
            auto* p = reinterpret_cast<__m256i*>(out + i);
            __m256i x = _mm256_loadu_si256(p);
            x = _mm256_xor_si256(x, _mm256_slli_epi32(x, 13));
            x = _mm256_xor_si256(x, _mm256_srli_epi32(x, 17));
            x = _mm256_xor_si256(x, _mm256_slli_epi32(x, 5));
            x = _mm256_add_epi32(x, k);
            x = _mm256_or_si256(_mm256_slli_epi32(x, 7), _mm256_srli_epi32(x, 25));
            _mm256_storeu_si256(p, x);
        }
    }
    _mm256_zeroupper();
}

void transform_avx512(const std::uint32_t* in, std::uint32_t* out, std::uint32_t seed, std::uint32_t rounds) noexcept {
    for (std::uint32_t i = 0; i < kWordsPerBlock; ++i) out[i] = in[i];
    for (std::uint32_t r = 0; r < rounds; ++r) {
        const __m512i k = _mm512_set1_epi32(static_cast<int>(round_constant(seed, r)));
        for (std::uint32_t i = 0; i < kWordsPerBlock; i += 16) {
            void* p = out + i;
            __m512i x = _mm512_loadu_si512(p);
            x = _mm512_xor_si512(x, _mm512_slli_epi32(x, 13));
            x = _mm512_xor_si512(x, _mm512_srli_epi32(x, 17));
            x = _mm512_xor_si512(x, _mm512_slli_epi32(x, 5));
            x = _mm512_add_epi32(x, k);
            x = _mm512_or_si512(_mm512_slli_epi32(x, 7), _mm512_srli_epi32(x, 25));
            _mm512_storeu_si512(p, x);
        }
    }
    _mm256_zeroupper();
}
#endif

}  // namespace

void transform(Isa isa, const std::uint32_t* in, std::uint32_t* out, std::uint32_t seed, std::uint32_t rounds) noexcept {
    switch (isa) {
#if EF_CPU_VERIFY_X86
        case Isa::Sse2: transform_sse2(in, out, seed, rounds); return;
        case Isa::Avx2: transform_avx2(in, out, seed, rounds); return;
        case Isa::Avx512: transform_avx512(in, out, seed, rounds); return;
#endif
        default: transform_scalar(in, out, seed, rounds); return;
    }
}

Config quick_config() {
    Config c;
    c.time_budget_ms = 3000;
    return c;
}

Config full_config() {
    Config c;
    c.time_budget_ms = 8000;
    c.min_blocks_per_isa = 16;
    return c;
}

ConfigVerdict validate_config(const Config& c) noexcept {
    if (c.rounds == 0 || c.rounds > kMaxRounds) return ConfigVerdict::RoundsInvalid;
    if (c.threads > kMaxThreads) return ConfigVerdict::ThreadsInvalid;
    if (c.time_budget_ms == 0 || c.time_budget_ms > kMaxTimeBudgetMs) return ConfigVerdict::TimeInvalid;
    if (c.slice_ms < 4 || c.slice_ms > c.time_budget_ms) return ConfigVerdict::SliceInvalid;
    if ((c.isa_mask & kAllIsas) == 0) return ConfigVerdict::NoIsa;
    return ConfigVerdict::Ok;
}

const char* config_reason(ConfigVerdict v) noexcept { return v == ConfigVerdict::Ok ? "" : "cpu_verify_invalid"; }

std::uint32_t phase_at(double elapsed_ms, std::uint32_t budget_ms) noexcept {
    if (!(elapsed_ms >= 0) || budget_ms == 0 || elapsed_ms >= budget_ms) return 4;
    const auto p = static_cast<std::uint32_t>(elapsed_ms * 4.0 / budget_ms);
    return std::min<std::uint32_t>(p, 3);
}

bool busy_at(double elapsed_ms, std::uint32_t slice_ms, std::uint32_t duty_percent) noexcept {
    if (duty_percent >= 100) return true;
    if (duty_percent == 0 || slice_ms == 0 || !(elapsed_ms >= 0)) return false;
    const double pos = elapsed_ms - static_cast<double>(static_cast<std::uint64_t>(elapsed_ms / slice_ms)) * slice_ms;
    return pos < static_cast<double>(slice_ms) * duty_percent / 100.0;
}

std::string_view to_string(Status s) noexcept {
    switch (s) {
        case Status::NotRun: return "not_run";
        case Status::Ok: return "ok";
        case Status::Mismatch: return "mismatch";
        case Status::Incomplete: return "incomplete";
        case Status::Cancelled: return "cancelled";
        case Status::Invalid: return "invalid";
    }
    return "invalid";
}

Status decide(const Result& r, const Config& c, bool cancelled) noexcept {
    if (r.mismatched_blocks > 0) return Status::Mismatch;  // iptal/sure sonunda da korunur
    if (cancelled) return Status::Cancelled;
    if (r.worker_failed || r.blocks == 0) return Status::Incomplete;
    for (const auto& i : r.isas)
        if (i.supported && i.blocks < std::max<std::uint32_t>(1, c.min_blocks_per_isa)) return Status::Incomplete;
    return Status::Ok;
}

Result run(const Config& c, const std::atomic<bool>& cancel) {
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    const auto elapsed_ms = [&t0] { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); };
    Result res;
    for (std::uint32_t i = 0; i < kIsaCount; ++i) res.isas[i].isa = static_cast<Isa>(i);
    if (validate_config(c) != ConfigVerdict::Ok) {
        res.status = Status::Invalid;
        return res;
    }
    const CpuidBits bits = c.cpuid_override ? *c.cpuid_override : read_cpuid();
    std::vector<Isa> active;
    for (auto& ir : res.isas) {
        ir.supported = (c.isa_mask & isa_bit(ir.isa)) != 0 && isa_supported(ir.isa, bits);
#if !EF_CPU_VERIFY_X86
        if (ir.isa != Isa::Scalar) ir.supported = false;  // SIMD yolu derlenmedi: desteklenmiyor
#endif
        if (ir.supported) active.push_back(ir.isa);
    }
    if (active.empty()) {
        res.status = Status::Invalid;  // isa_mask yalniz desteklenmeyenleri secti: hic dogrulama yok
        return res;
    }
    if (cancel.load()) {
        res.cancelled = true;
        res.status = decide(res, c, true);
        return res;
    }
    std::uint32_t threads = c.threads ? c.threads : logical_processor_count();
    threads = std::min(threads, kMaxThreads);
    res.threads = threads;

    std::atomic<std::uint64_t> next{0};
    std::atomic<bool> stop{false};
    std::array<std::atomic<std::uint64_t>, kIsaCount> blocks{}, mm_blocks{}, mm_words{};
    std::array<std::atomic<std::uint64_t>, 4> phase_blocks{};
    std::atomic<std::uint64_t> first_bad{std::numeric_limits<std::uint64_t>::max()};

    std::atomic<bool> failed{false};

    // Istisna is parcacigindan disari cikmaz (cikarsa std::terminate): bellek ayrilamazsa dogrulama eksik sayilir.
    auto worker = [&](std::uint32_t slot) noexcept {
      if (threads > 1) spread_current_thread(slot, threads);  // > 64 CPU: tum islemci gruplari
      try {
        if (c.inject_worker_throw && slot == 0) throw std::bad_alloc();
        std::vector<std::uint32_t> in(kWordsPerBlock), ref(kWordsPerBlock), out(kWordsPerBlock);
        while (!stop.load(std::memory_order_relaxed) && !cancel.load(std::memory_order_relaxed)) {
            const double el = elapsed_ms();
            const std::uint32_t ph = phase_at(el, c.time_budget_ms);
            if (ph >= 4) break;
            const std::uint32_t duty = kPhaseDutyPercent[ph];
            if (!busy_at(el, c.slice_ms, duty)) {
                // Yuk payi disinda periyodun kalanini uyu (yuk dusurulur; olcum degil).
                const double pos = el - static_cast<double>(static_cast<std::uint64_t>(el / c.slice_ms)) * c.slice_ms;
                const auto rest = std::max(1.0, static_cast<double>(c.slice_ms) - pos);
                std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(rest));
                continue;
            }
            const std::uint64_t b = next.fetch_add(1, std::memory_order_relaxed);
            if (c.max_blocks && b >= c.max_blocks) {
                stop = true;
                break;
            }
            const Isa isa = active[b % active.size()];
            fill_input(in.data(), c.seed, b);
            for (std::uint32_t i = 0; i < kWordsPerBlock; ++i) ref[i] = reference_word(in[i], c.seed, c.rounds);
            transform(isa, in.data(), out.data(), c.seed, c.rounds);
            if (c.inject_fault_block >= 0 && b == static_cast<std::uint64_t>(c.inject_fault_block)) out[0] ^= 1u;
            std::uint64_t bad = 0;
            for (std::uint32_t i = 0; i < kWordsPerBlock; ++i) bad += ref[i] != out[i];
            const auto idx = static_cast<std::uint32_t>(isa);
            blocks[idx].fetch_add(1, std::memory_order_relaxed);
            phase_blocks[ph].fetch_add(1, std::memory_order_relaxed);
            if (bad) {
                mm_blocks[idx].fetch_add(1, std::memory_order_relaxed);
                mm_words[idx].fetch_add(bad, std::memory_order_relaxed);
                std::uint64_t cur = first_bad.load();
                while (b < cur && !first_bad.compare_exchange_weak(cur, b)) {
                }
            }
        }
      } catch (...) {
        failed = true;
      }
    };
    {
        // Baslatma hatasi (std::system_error / bad_alloc) disari tasinmaz: baslayan is parcaciklari her durumda
        // birlestirilir (birlesmemis std::thread yikimi std::terminate olurdu); dogrulama eksik sayilir.
        std::vector<std::thread> pool;
        try {
            pool.reserve(threads);
            for (std::uint32_t t = 0; t < threads; ++t) {
                if (c.inject_thread_start_failure >= 0 && t == static_cast<std::uint32_t>(c.inject_thread_start_failure))
                    throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
                pool.emplace_back(worker, t);
            }
        } catch (...) {
            failed = true;
        }
        for (auto& t : pool) t.join();
    }
    res.worker_failed = failed.load();
    for (std::uint32_t i = 0; i < kIsaCount; ++i) {
        res.isas[i].blocks = blocks[i].load();
        res.isas[i].mismatched_blocks = mm_blocks[i].load();
        res.isas[i].mismatched_words = mm_words[i].load();
        res.blocks += res.isas[i].blocks;
        res.mismatched_blocks += res.isas[i].mismatched_blocks;
    }
    for (std::uint32_t p = 0; p < 4; ++p) res.phase_blocks[p] = phase_blocks[p].load();
    if (const auto fb = first_bad.load(); fb != std::numeric_limits<std::uint64_t>::max()) {
        res.first_mismatch_block = fb;
        res.first_mismatch_isa = active[fb % active.size()];
    }
    res.cancelled = cancel.load();
    res.total_ms = elapsed_ms();
    res.status = decide(res, c, res.cancelled);
    return res;
}

}  // namespace ef::benchmark::cpu_verify
