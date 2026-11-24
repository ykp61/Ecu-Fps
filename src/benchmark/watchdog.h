#pragma once
// Alt test bekcisi (ortak): Uygulama Testi alt testleri (runner.cpp) ve denge testi adimlari (balance_runner.cpp)
// ayni bekciyi kullanir. Ust iptal veya zaman asimi -> yerel iptal bayragi (`local`); calisan is bu bayragi
// yoklar. RAII: kapsam sonunda bekci is parcacigi durur ve katilir. Yoklama araligi 20 ms.
//  - `timed_out`: yerel iptal zaman asimindan mi geldi (kullanici iptalinden ayirmak icin).
//  - Bekci yalniz bayrak kaldirir; kaynak birakmaz, is parcacigi oldurmez.

#include <atomic>
#include <chrono>
#include <thread>

namespace ef::benchmark {

struct Watchdog {
    std::atomic<bool> local{false};
    std::atomic<bool> stop{false};
    std::atomic<bool> timed_out{false};
    std::thread t;
    Watchdog(const std::atomic<bool>& parent, double timeout_s) {
        t = std::thread([this, &parent, timeout_s] {
            const auto t0 = std::chrono::steady_clock::now();
            while (!stop.load()) {
                if (parent.load()) local = true;
                if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > timeout_s) {
                    timed_out = true;
                    local = true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
    }
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    ~Watchdog() {
        stop = true;
        t.join();
    }
};

}  // namespace ef::benchmark
