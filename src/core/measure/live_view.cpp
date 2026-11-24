#include "core/measure/live_view.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace ef::core::measure {

namespace {
bool usable_fps(const LiveSample& s) noexcept { return s.has_numbers && std::isfinite(s.fps) && s.fps > 0.0; }
std::optional<double> share_pct(double ms, double fps) noexcept {
    if (!std::isfinite(ms) || ms < 0.0) return std::nullopt;
    return std::clamp(ms * fps / 10.0, 0.0, 100.0);  // ms / (1000 / fps) * 100
}
constexpr std::size_t kSpareBuffers = 3;  // kutudaki + UI'daki + yazilan kare icin yeterli
}  // namespace

std::optional<double> live_frame_ms(const LiveSample& s) noexcept {
    if (!usable_fps(s)) return std::nullopt;
    return 1000.0 / s.fps;
}

std::optional<double> live_cpu_load_pct(const LiveSample& s) noexcept {
    if (!usable_fps(s)) return std::nullopt;
    return share_pct(s.cpu_ms, s.fps);
}

std::optional<double> live_gpu_load_pct(const LiveSample& s) noexcept {
    if (!usable_fps(s) || !s.gpu_ms) return std::nullopt;
    return share_pct(*s.gpu_ms, s.fps);
}

LiveLoad live_load(std::uint32_t step_code) noexcept {
    switch (step_code) {
        case 1:
        case 8: return LiveLoad::CpuHeavy;  // S1, T1
        case 2:
        case 3:
        case 4:
        case 9: return LiveLoad::GpuHeavy;  // S2-S4, T2
        case 5:
        case 6:
        case 7: return LiveLoad::Both;      // K1-K3
        default: return LiveLoad::Unknown;
    }
}

std::vector<double> live_frame_bars(const LiveSample& s, std::size_t max_bars) {
    std::vector<double> out;
    if (max_bars == 0) return out;
    const std::size_t n = std::min(s.ring_count, s.fps_ring.size());
    for (std::size_t i = 0; i < n; ++i) {
        const double f = static_cast<double>(s.fps_ring[i]);
        if (std::isfinite(f) && f > 0.0) out.push_back(1000.0 / f);
    }
    if (out.size() > max_bars) out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(max_bars));
    return out;
}

std::optional<double> live_step_fraction(const LiveSample& s) noexcept {
    if (s.step_index == 0 || s.step_total == 0 || s.step_index > s.step_total) return std::nullopt;
    return static_cast<double>(s.step_index - 1) / static_cast<double>(s.step_total);
}

// Yedek tamponlar. Birakilan karenin tamponu buraya doner (silici herhangi bir is parcaciginda calisabilir); kilit
// alinamazsa tampon dogrudan silinir — hicbir taraf digerini beklemez.
// recycle: kosu suruyor mu. clear() (kosu bitti) bunu kapatir; o andan sonra birakilan kareler (UI'nin elinde kalan son
// kare gibi) havuza DONMEZ, silinir — aksi halde her kosudan sonra 960x540'lik bir tampon bosta bellekte kalirdi.
// Sonraki kosunun ilk offer() cagrisi yeniden acar.
struct PreviewMailbox::Pool {
    std::mutex mu;
    std::vector<std::unique_ptr<games::ImagePixels>> spare;
    bool recycle = true;
};

PreviewMailbox::PreviewMailbox() : pool_(std::make_shared<Pool>()) {}

std::size_t PreviewMailbox::spare_buffers() const {
    std::lock_guard lk(pool_->mu);
    return pool_->spare.size();
}

bool PreviewMailbox::offer(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t stride) {
    offered_.fetch_add(1, std::memory_order_relaxed);
    const auto drop = [this] {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    };
    if (!wanted()) return drop();
    const std::size_t row = std::size_t{width} * 4;
    if (!pixels || width == 0 || height == 0 || width > kLivePreviewMaxSide || height > kLivePreviewMaxSide || stride < row) return drop();
    std::unique_ptr<games::ImagePixels> buf;
    if (std::unique_lock lk(pool_->mu, std::try_to_lock); lk) {
        pool_->recycle = true;  // kosu suruyor: birakilan tamponlar yeniden kullanilir
        if (!pool_->spare.empty()) {
            buf = std::move(pool_->spare.back());
            pool_->spare.pop_back();
        }
    }
    if (!buf) buf = std::make_unique<games::ImagePixels>();
    buf->w = width;
    buf->h = height;
    buf->bgra.resize(row * height);
    for (std::uint32_t y = 0; y < height; ++y) std::memcpy(buf->bgra.data() + row * y, pixels + std::size_t{stride} * y, row);
    games::ImageRef img(buf.release(), [weak = std::weak_ptr<Pool>(pool_)](const games::ImagePixels* p) {
        std::unique_ptr<games::ImagePixels> own(const_cast<games::ImagePixels*>(p));
        if (const auto pool = weak.lock())
            if (std::unique_lock lk(pool->mu, std::try_to_lock); lk && pool->recycle && pool->spare.size() < kSpareBuffers)
                pool->spare.push_back(std::move(own));
    });
    games::ImageRef old;  // kilit disinda birakilir
    {
        std::unique_lock lk(mu_, std::try_to_lock);
        if (!lk) return drop();  // tuketici o an kutuda: beklenmez (img kapsam sonunda havuza doner)
        old = std::move(latest_);
        latest_ = std::move(img);
    }
    return true;
}

games::ImageRef PreviewMailbox::take() {
    std::lock_guard lk(mu_);
    return std::exchange(latest_, nullptr);
}

void PreviewMailbox::clear() {
    games::ImageRef old;
    {
        std::lock_guard lk(mu_);
        old = std::move(latest_);
    }
    old.reset();
    std::lock_guard lk(pool_->mu);
    pool_->spare.clear();
    pool_->recycle = false;  // bundan sonra birakilan kareler silinir (bosta tampon tutulmaz)
}

}  // namespace ef::core::measure
