#include "benchmark/scene/sim_pool.h"

namespace ef::benchmark {

SimPool::SimPool(std::uint32_t workers) : count_(workers ? workers : 1) {
    threads_.reserve(count_);
    try {
        for (std::uint32_t w = 0; w < count_; ++w) threads_.emplace_back([this, w] { run(w); });
    } catch (...) {
        shutdown();
        throw;
    }
}

SimPool::~SimPool() { shutdown(); }

void SimPool::shutdown() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    start_.notify_all();
    for (std::thread& t : threads_)
        if (t.joinable()) t.join();
    threads_.clear();
}

void SimPool::step(FrameSim& sim) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        job_ = &sim;
        remaining_ = count_;
        ++generation_;
        start_.notify_all();
        done_.wait(lock, [this] { return remaining_ == 0; });
        job_ = nullptr;
    }
    sim.commit();
}

void SimPool::run(std::uint32_t worker) {
    std::uint64_t seen = 0;
    for (;;) {
        FrameSim* job = nullptr;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            start_.wait(lock, [&] { return stop_ || generation_ != seen; });
            if (stop_) return;
            seen = generation_;
            job = job_;
        }
        if (job) job->step_range(worker_range(job->config().objects, count_, worker));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (remaining_ > 0 && --remaining_ == 0) done_.notify_one();
        }
    }
}

}  // namespace ef::benchmark
