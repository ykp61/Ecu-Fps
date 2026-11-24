#pragma once
// Denge testi (spec 2026-11-12 A1): kare simulasyonu icin KALICI fork-join isci havuzu.
//  - Is parcaciklari bir kez kurulur ve adim boyunca yasar; kare basina is parcacigi acilmaz.
//  - Her isci her karede ayni sabit indeks araligini isler (worker_range); sonuc zamanlamaya bagli degildir
//    (FrameSim cift tamponludur: onceki kareyi okur, yalniz kendi araligina yazar).
//  - Oncelik / cekirdek atamasi DEGISTIRILMEZ (OS zamanlayicisi karar verir).
//  - step(): iscileri uyandirir, hepsi bitene kadar cagiran is parcacigini bekletir, sonra commit eder.
//    Cagiran is parcacigi simulasyona katilmaz (cizim is parcacigi; kendi payi ayri olculur).

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "benchmark/scene/frame_sim.h"

namespace ef::benchmark {

class SimPool {
public:
    // workers == 0 -> 1. Is parcacigi acilamazsa std::system_error firlatir (acilanlar durdurulur).
    explicit SimPool(std::uint32_t workers);
    ~SimPool();
    SimPool(const SimPool&) = delete;
    SimPool& operator=(const SimPool&) = delete;

    std::uint32_t workers() const noexcept { return count_; }
    // Bir kare: tum araliklar iscilerde hesaplanir, hepsi bitince commit. Ayni anda tek cagiran.
    void step(FrameSim& sim);

private:
    void run(std::uint32_t worker);
    void shutdown() noexcept;

    const std::uint32_t count_;     // isci sayisi (is parcaciklari baslamadan once sabit)
    std::mutex mutex_;
    std::condition_variable start_, done_;
    FrameSim* job_ = nullptr;       // mutex_ altinda
    std::uint64_t generation_ = 0;  // her step() ile artar
    std::uint32_t remaining_ = 0;   // bu karede bitmemis isci
    bool stop_ = false;
    std::vector<std::thread> threads_;
};

}  // namespace ef::benchmark
