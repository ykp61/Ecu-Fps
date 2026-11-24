#include "benchmark/scene/scene.h"

#include "benchmark/scene/arena.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ef::benchmark {

std::string_view to_string(Tier t) noexcept {
    switch (t) {
        case Tier::Low: return "low";
        case Tier::Medium: return "medium";
        case Tier::High: return "high";
    }
    return "medium";
}

std::string_view to_string(DrawMode m) noexcept { return m == DrawMode::PerObject ? "per_object" : "instanced"; }

TierParams tier_params(Tier t) noexcept {
    // Maliyetin buyuk kismi piksel basi isik dongusu (cozunurluk x isik). High, hizli masaustu GPU'sunda
    // 1080p'de GPU'yu darbogaz yapacak kadar agir; Low zayif iGPU'da da makul surede kosar.
    switch (t) {
        case Tier::Low: return {2000, 48, 6};
        case Tier::Medium: return {20000, 384, 10};
        case Tier::High: return {80000, 3072, 16};
    }
    return {20000, 384, 10};
}

std::uint32_t instance_count(Tier t) noexcept { return tier_params(t).instances; }
std::uint32_t light_count(Tier t) noexcept { return tier_params(t).lights; }

namespace {
// xorshift64*: platformdan bagimsiz, standart kutuphane dagilimlarina bagli degil (surumler arasi sabit).
struct Rng {
    std::uint64_t s;
    std::uint64_t next() noexcept {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ull;
    }
    float unit() noexcept { return static_cast<float>(next() >> 40) / static_cast<float>(1ull << 24); }  // [0,1)
    float range(float a, float b) noexcept { return a + (b - a) * unit(); }
};
}  // namespace

std::vector<Instance> generate_instances(const SceneConfig& c) {
    Rng r{c.seed ? c.seed : 1};
    const auto n = instance_count(c.tier);
    std::vector<Instance> v;
    v.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        Instance x{};
        // Halka seklinde sehir: yaricap 20..120, yukseklik 0..30
        const float a = r.range(0.f, 6.2831853f), rad = r.range(20.f, 120.f);
        x.pos = {std::cos(a) * rad, r.range(0.f, 30.f), std::sin(a) * rad};
        x.scale = r.range(0.3f, 2.5f);
        x.color = {r.range(0.2f, 1.f), r.range(0.2f, 1.f), r.range(0.2f, 1.f), 1.f};
        x.spin = r.range(-0.05f, 0.05f);
        v.push_back(x);
    }
    return v;
}

std::vector<PointLight> generate_lights(const SceneConfig& c) {
    Rng r{(c.seed ? c.seed : 1) ^ 0x9E3779B97F4A7C15ull};  // ornek akisindan bagimsiz akis
    const auto n = light_count(c.tier);
    std::vector<PointLight> v;
    v.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        PointLight l{};
        const float a = r.range(0.f, 6.2831853f), rad = r.range(0.f, 140.f), radius = r.range(10.f, 40.f);
        l.pos_inv_r2 = {std::cos(a) * rad, r.range(1.f, 28.f), std::sin(a) * rad, 1.f / (radius * radius)};
        l.color = {r.range(0.2f, 1.f), r.range(0.2f, 1.f), r.range(0.2f, 1.f), 1.f};
        v.push_back(l);
    }
    return v;
}

std::uint32_t light_draws(std::uint32_t lights) noexcept {
    const std::uint32_t n = lights / kLightsPerDraw + (lights % kLightsPerDraw ? 1u : 0u);
    return n ? n : 1u;
}

std::uint32_t orbit_period(const SceneConfig& c) noexcept {
    return duration_mode(c) ? kOrbitFrames : std::max<std::uint32_t>(1u, c.frames);
}

Camera camera_at(const SceneConfig& c, std::uint32_t frame) noexcept {
    const std::uint32_t period = orbit_period(c);
    const float t = static_cast<float>(frame % period) / static_cast<float>(period);
    const float a = t * 6.2831853f;
    return Camera{{std::cos(a) * 150.f, 45.f + 15.f * std::sin(a * 2.f), std::sin(a) * 150.f}, {0.f, 10.f, 0.f}};
}

std::uint64_t workload_hash(const SceneConfig& c, const std::vector<Instance>& v) noexcept {
    if (c.arena) return arena_workload_hash(c);  // ayri kimlik; asagidaki scene_v3 yolu degismedi
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    };
    mix(kSceneVersion.data(), kSceneVersion.size());
    mix(&c.seed, sizeof c.seed);
    const auto tier = static_cast<int>(c.tier);
    mix(&tier, sizeof tier);
    const auto tp = tier_params(c.tier);
    mix(&tp.lights, sizeof tp.lights);
    mix(&tp.blur_radius, sizeof tp.blur_radius);
    mix(&c.width, sizeof c.width);
    mix(&c.height, sizeof c.height);
    mix(&c.frames, sizeof c.frames);
    mix(&c.warmup_frames, sizeof c.warmup_frames);
    mix(&c.warmup_s, sizeof c.warmup_s);
    mix(&c.measure_s, sizeof c.measure_s);
    if (!v.empty()) mix(v.data(), v.size() * sizeof(Instance));
    // Isik listesi tohum + tier'dan turetilir; ayni config -> ayni liste. Yine de ham bitler karistirilir.
    const auto lights = generate_lights(c);
    if (!lights.empty()) mix(lights.data(), lights.size() * sizeof(PointLight));
    return h;
}

bool duration_mode(const SceneConfig& c) noexcept { return c.measure_s > 0; }

ScenePhase scene_phase(const SceneConfig& c, std::uint64_t frame, double elapsed_s) noexcept {
    if (duration_mode(c)) {
        if (frame >= c.max_frames) return ScenePhase::Done;
        const double w = c.warmup_s > 0 ? c.warmup_s : 0.0;
        if (elapsed_s < w) return ScenePhase::Warmup;
        if (elapsed_s < w + c.measure_s) return ScenePhase::Measure;
        return ScenePhase::Done;
    }
    if (frame < c.warmup_frames) return ScenePhase::Warmup;
    if (frame < static_cast<std::uint64_t>(c.warmup_frames) + c.frames) return ScenePhase::Measure;
    return ScenePhase::Done;
}

Tier pick_scene_tier(const std::array<double, 3>& avg_frame_ms, double max_frame_ms) noexcept {
    if (!(max_frame_ms > 0)) return Tier::Low;
    const Tier order[3] = {Tier::High, Tier::Medium, Tier::Low};
    for (const Tier t : order) {
        const double ms = avg_frame_ms[static_cast<std::size_t>(t)];
        if (ms > 0 && ms <= max_frame_ms) return t;
    }
    return Tier::Low;
}

bool GpuQueryRing::issue(bool measured, std::uint32_t& slot) noexcept {
    if (full()) return false;
    slot = static_cast<std::uint32_t>(tail_ % slots_);
    measured_[slot] = measured;
    gap_before_[slot] = gap_pending_;
    gap_pending_ = false;
    ++tail_;
    return true;
}

void GpuQueryRing::note_unissued(bool measured) noexcept {
    if (measured) ++excluded_;
    gap_pending_ = true;
}

void GpuQueryRing::resolve_oldest(bool disjoint, std::uint64_t freq, bool read_ok, std::uint64_t a, std::uint64_t b) {
    if (pending() == 0) return;
    const auto slot = oldest_slot();
    ++head_;
    const bool ok = !disjoint && freq != 0 && read_ok && b >= a;
    const bool chain = ok && !gap_before_[slot] && have_prev_end_ && prev_freq_ == freq && b >= prev_end_;
    if (ok) {  // canli gosterge: isinma dahil (skor serilerine girmez)
        live_sum_ms_ += static_cast<double>(b - a) * 1000.0 / static_cast<double>(freq);
        ++live_n_;
    }
    if (measured_[slot]) {  // isinma karesi sayilmaz ama zincir capasi olabilir
        if (!ok) {
            ++excluded_;
        } else {
            elapsed_ms_.push_back(static_cast<double>(b - a) * 1000.0 / static_cast<double>(freq));
            if (chain) pacing_ms_.push_back(static_cast<double>(b - prev_end_) * 1000.0 / static_cast<double>(freq));
        }
    }
    have_prev_end_ = ok;
    if (ok) prev_end_ = b, prev_freq_ = freq;
}

// ---- Canli test ekrani (A4) ----

namespace {
std::uint32_t to_fixed(double v, double scale) noexcept {
    if (!std::isfinite(v) || !(v > 0.0)) return 0;
    const double x = v * scale + 0.5;
    return x >= 4294967295.0 ? 0xFFFFFFFFu : static_cast<std::uint32_t>(x);
}
}  // namespace

void FrameTap::begin_step(std::uint32_t index, std::uint32_t total, std::uint32_t code, std::uint32_t w, std::uint32_t h) noexcept {
    set_phase(TapPhase::Prepare);
    step_index.store(index, std::memory_order_relaxed);
    step_total.store(total, std::memory_order_relaxed);
    step_code.store(code, std::memory_order_relaxed);
    set_resolution(w, h);
    reset_live();
}

void FrameTap::reset_live() noexcept {
    ring_pushes.store(0, std::memory_order_release);
    fps_milli.store(0, std::memory_order_relaxed);
    cpu_us.store(0, std::memory_order_relaxed);
    gpu_us.store(0, std::memory_order_relaxed);
    gpu_valid.store(0, std::memory_order_relaxed);
    wait_permille.store(0, std::memory_order_relaxed);
}

void FrameTap::set_resolution(std::uint32_t w, std::uint32_t h) noexcept {
    width.store(w, std::memory_order_relaxed);
    height.store(h, std::memory_order_relaxed);
}

void FrameTap::publish(double fps, double cpu_ms, std::optional<double> gpu_ms, double wait_share, double bucket_fps) noexcept {
    fps_milli.store(to_fixed(fps, 1000.0), std::memory_order_relaxed);
    cpu_us.store(to_fixed(cpu_ms, 1000.0), std::memory_order_relaxed);
    gpu_us.store(gpu_ms ? to_fixed(*gpu_ms, 1000.0) : 0u, std::memory_order_relaxed);
    gpu_valid.store(gpu_ms ? 1u : 0u, std::memory_order_relaxed);
    wait_permille.store(to_fixed(std::clamp(wait_share, 0.0, 1.0), 1000.0), std::memory_order_relaxed);
    const std::uint64_t n = ring_pushes.load(std::memory_order_relaxed);  // tek yazan: cizim is parcacigi
    ring_milli[static_cast<std::size_t>(n % kTapRingSize)].store(to_fixed(bucket_fps, 1000.0), std::memory_order_relaxed);
    ring_pushes.store(n + 1, std::memory_order_release);
}

FrameTapLive FrameTap::snapshot() const noexcept {
    FrameTapLive s;
    s.step_index = step_index.load(std::memory_order_relaxed);
    s.step_total = step_total.load(std::memory_order_relaxed);
    s.step_code = step_code.load(std::memory_order_relaxed);
    s.width = width.load(std::memory_order_relaxed);
    s.height = height.load(std::memory_order_relaxed);
    const std::uint32_t ph = phase.load(std::memory_order_relaxed);
    s.phase = ph <= static_cast<std::uint32_t>(TapPhase::Measure) ? static_cast<TapPhase>(ph) : TapPhase::Idle;
    const std::uint64_t n = ring_pushes.load(std::memory_order_acquire);
    s.updates = n;
    s.has_window = n > 0;
    if (!s.has_window) return s;
    s.fps = fps_milli.load(std::memory_order_relaxed) / 1000.0;
    s.cpu_ms = cpu_us.load(std::memory_order_relaxed) / 1000.0;
    if (gpu_valid.load(std::memory_order_relaxed)) s.gpu_ms = gpu_us.load(std::memory_order_relaxed) / 1000.0;
    s.wait_share = wait_permille.load(std::memory_order_relaxed) / 1000.0;
    s.ring_count = n < kTapRingSize ? static_cast<std::size_t>(n) : kTapRingSize;
    const std::uint64_t first = n - s.ring_count;
    for (std::size_t i = 0; i < s.ring_count; ++i) {
        const auto v = ring_milli[static_cast<std::size_t>((first + i) % kTapRingSize)].load(std::memory_order_relaxed);
        s.fps_ring[i] = static_cast<float>(static_cast<double>(v) / 1000.0);
    }
    return s;
}

void TapFeeder::frame(double interval_ms, double cpu_ms, double wait_ms) noexcept {
    if (!tap_ || !std::isfinite(interval_ms) || !(interval_ms > 0.0) || !(interval_ms < 60000.0)) return;
    cur_.span_ms += interval_ms;
    if (std::isfinite(cpu_ms) && cpu_ms > 0.0) cur_.cpu_ms += cpu_ms;
    if (std::isfinite(wait_ms) && wait_ms > 0.0) cur_.wait_ms += wait_ms;
    ++cur_.frames;
    if (cur_.span_ms < kTapBucketMs) return;
    const double bucket_fps = static_cast<double>(cur_.frames) * 1000.0 / cur_.span_ms;
    window_[closed_ % kTapWindowBuckets] = cur_;
    ++closed_;
    cur_ = Bucket{};
    Bucket sum;
    const std::size_t n = closed_ < kTapWindowBuckets ? closed_ : kTapWindowBuckets;
    for (std::size_t i = 0; i < n; ++i) {
        const Bucket& b = window_[i];
        sum.span_ms += b.span_ms, sum.cpu_ms += b.cpu_ms, sum.wait_ms += b.wait_ms, sum.gpu_ms += b.gpu_ms;
        sum.frames += b.frames, sum.gpu_n += b.gpu_n;
    }
    const double frames = static_cast<double>(sum.frames);
    tap_->publish(frames * 1000.0 / sum.span_ms, sum.cpu_ms / frames,
                  sum.gpu_n ? std::optional<double>(sum.gpu_ms / static_cast<double>(sum.gpu_n)) : std::nullopt, sum.wait_ms / sum.span_ms,
                  bucket_fps);
}

void TapFeeder::gpu(double sum_ms, std::uint32_t n) noexcept {
    if (!tap_ || n == 0 || !std::isfinite(sum_ms) || sum_ms < 0.0) return;
    cur_.gpu_ms += sum_ms;
    cur_.gpu_n += n;
}

PreviewSize preview_size(std::uint32_t width, std::uint32_t height) noexcept {
    if (width == 0 || height == 0) return {};
    if (width <= kPreviewMaxWidth && height <= kPreviewMaxHeight) return {width, height};
    // Kutuya sigdir: olcek = min(960 / w, 540 / h); tamsayi aritmetigi (asagi yuvarlama, en az 1).
    const std::uint64_t w = width, h = height;
    PreviewSize p;
    if (w * kPreviewMaxHeight >= h * kPreviewMaxWidth) {  // genislik sinirlar
        p.width = kPreviewMaxWidth;
        p.height = static_cast<std::uint32_t>(std::max<std::uint64_t>(1, h * kPreviewMaxWidth / w));
    } else {
        p.height = kPreviewMaxHeight;
        p.width = static_cast<std::uint32_t>(std::max<std::uint64_t>(1, w * kPreviewMaxHeight / h));
    }
    return p;
}

bool copy_preview(const PreviewFrame& f, std::vector<std::uint8_t>& out) {
    const std::size_t row = std::size_t{f.width} * 4;
    if (!f.pixels || f.width == 0 || f.height == 0 || f.stride < row) return false;
    out.resize(row * f.height);
    for (std::uint32_t y = 0; y < f.height; ++y) std::memcpy(out.data() + row * y, f.pixels + std::size_t{f.stride} * y, row);
    return true;
}

bool PreviewRing::should_issue(double now_ms) const noexcept {
    if (count_ >= kPreviewSlots) return false;
    return !issued_ || now_ms - last_ms_ >= kPreviewMinIntervalMs;
}

std::optional<std::uint32_t> PreviewRing::issue(double now_ms, std::uint64_t frame, bool measuring) noexcept {
    if (count_ >= kPreviewSlots) return std::nullopt;
    const std::uint32_t slot = next_slot_;
    q_[(head_ + count_) % kPreviewSlots] = Pending{slot, frame, measuring};
    ++count_;
    next_slot_ = (next_slot_ + 1) % kPreviewSlots;
    issued_ = true;
    last_ms_ = now_ms;
    return slot;
}

std::optional<PreviewRing::Pending> PreviewRing::ready(std::uint64_t frame) const noexcept {
    if (count_ == 0) return std::nullopt;
    const Pending& p = q_[head_];
    if (frame < p.frame + kPreviewDelayFrames) return std::nullopt;
    return p;
}

void PreviewRing::pop() noexcept {
    if (count_ == 0) return;
    head_ = (head_ + 1) % kPreviewSlots;
    --count_;
}

DurationStats summarize_ms(const std::vector<double>& ms) {
    DurationStats s;
    std::vector<double> v;
    v.reserve(ms.size());
    for (const double x : ms)
        if (std::isfinite(x) && x >= 0) v.push_back(x);
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.n = v.size();
    for (const double x : v) s.sum_ms += x;
    s.avg_ms = s.sum_ms / static_cast<double>(s.n);
    auto rank = [&](double p) {
        auto k = static_cast<std::size_t>(std::ceil(p * static_cast<double>(s.n)));
        if (k == 0) k = 1;
        return v[std::min(k, s.n) - 1];
    };
    s.p50_ms = rank(0.50);
    s.p99_ms = rank(0.99);
    s.max_ms = v.back();
    return s;
}

std::string_view to_string(FrameSource s) noexcept {
    return s == FrameSource::GpuCompletion ? "gpu_completion" : "submit_cadence";
}

std::optional<double> pacing_coverage(std::size_t pacing_n, std::size_t submit_n) noexcept {
    if (submit_n == 0) return std::nullopt;
    return std::min(1.0, static_cast<double>(pacing_n) / static_cast<double>(submit_n));
}

FrameSource choose_frame_source(bool timestamps, std::size_t pacing_n, std::size_t submit_n, std::uint32_t excluded) noexcept {
    if (!timestamps || pacing_n == 0 || submit_n == 0 || excluded != 0) return FrameSource::SubmitCadence;
    // pacing_n * 1000 >= submit_n * 995 (tamsayi; tasma yok: kare sayisi max_frames ile sinirli)
    if (pacing_n * 1000 < submit_n * 995) return FrameSource::SubmitCadence;
    return FrameSource::GpuCompletion;
}

std::optional<double> queue_saturation(const DurationStats& elapsed, const DurationStats& pacing) noexcept {
    if (elapsed.n == 0 || pacing.n == 0 || !(pacing.avg_ms > 0)) return std::nullopt;
    const double r = elapsed.avg_ms / pacing.avg_ms;
    if (!std::isfinite(r)) return std::nullopt;
    return std::clamp(r, 0.0, 1.0);
}

}  // namespace ef::benchmark
