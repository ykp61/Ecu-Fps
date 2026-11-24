#pragma once
// UI-09: canli CPU/GPU/RAM/disk ornekleyicisi (PDH, yonetici gerekmez, salt okuma).
// Sayaclar Ingilizce adla eklenir (PdhAddEnglishCounterW); Turkce Windows'ta da calisir.
//  - CPU : \Processor Information(_Total)\% Processor Utility (yoksa \Processor(_Total)\% Processor Time)
//          en yuklu cekirdek: \Processor Information(*)\% Processor Time (yoksa \Processor(*)\% Processor Time);
//          sure tabanli: %90 doygunluk esigi frekansla olceklenen Utility ile karsilastirilmaz (Uyumluluk B3)
//  - GPU : \GPU Engine(*)\Utilization Percentage (yoksa NotSupported)
//  - Disk: \PhysicalDisk(*)\% Idle Time (_Total haric); kuyruk: Current Disk Queue Length;
//          G/C gecikmesi: Avg. Disk sec/Transfer (ms'e cevrilir) - yoksa NotSupported
//          akis: Disk Read Bytes/sec ve Disk Write Bytes/sec (_Total haric toplam, MB/s) - yoksa NotSupported
//  - Bellek basinci: \Memory\% Committed Bytes In Use; \Memory\Pages Input/sec (hard fault hizi)
//  - RAM : GlobalMemoryStatusEx; birimler: GetDiskFreeSpaceExW (yalniz sabit suruculer, 10 sn onbellek)
// Oran sayaclari iki ornek ister: open() sonrasi ilk sample() CPU/GPU/disk icin NotSampled doner.
// Cagiran (UI) yalniz pencere gorunurken ~1 Hz cagirir; sinif is parcacigi guvenli degildir.

#include <cstdint>
#include <vector>

#include "core/metrics/live.h"

namespace ef::platform::metrics {

class PdhSampler {
public:
    PdhSampler() = default;
    ~PdhSampler();
    PdhSampler(const PdhSampler&) = delete;
    PdhSampler& operator=(const PdhSampler&) = delete;

    // Sorgu ve sayaclari acar. CPU sayaci eklenemezse false (diger sayaclar opsiyoneldir).
    bool open();
    core::metrics::LiveSnapshot sample();
    void close();

    bool is_open() const noexcept { return query_ != nullptr; }
    bool gpu_supported() const noexcept { return gpu_ != nullptr; }
    bool disk_supported() const noexcept { return disk_ != nullptr; }
    // Tani: GPU joker sayacinin kac kez yeniden genisletildigi.
    std::uint32_t gpu_reexpansions() const noexcept { return gpu_reexpansions_; }

private:
    void maybe_reexpand_gpu(std::int64_t now_ms);
    contracts::Measurement read_gpu();
    contracts::Measurement read_disk();
    contracts::Measurement read_cpu_max_core();
    contracts::Measurement read_disk_queue();
    contracts::Measurement read_disk_io_ms();
    contracts::Measurement read_disk_rate(void* counter);
    void refresh_volumes(std::int64_t now_ms);

    using Handle = void*;  // PDH_HQUERY / PDH_HCOUNTER (pdh.h basliga sizmasin)
    Handle query_ = nullptr;
    Handle cpu_ = nullptr;
    Handle gpu_ = nullptr;
    Handle gpu_pending_ = nullptr;  // yeniden genisletilmis sayac; ikinci ornekten sonra gpu_ olur
    Handle disk_ = nullptr;
    Handle cpu_cores_ = nullptr;
    Handle disk_queue_ = nullptr;
    Handle disk_io_ = nullptr;
    Handle commit_used_ = nullptr;
    Handle hard_faults_ = nullptr;
    Handle disk_read_ = nullptr;
    Handle disk_write_ = nullptr;
    std::uint32_t collects_ = 0;
    std::uint32_t pending_collects_ = 0;
    std::int64_t gpu_expanded_ms_ = 0;
    std::uint32_t gpu_reexpansions_ = 0;
    std::int64_t volumes_ms_ = -1;
    std::vector<core::metrics::VolumeUse> volumes_;
    std::vector<unsigned char> buf_;  // PdhGetFormattedCounterArrayW tamponu (yeniden kullanilir)
    std::vector<core::metrics::GpuEngineSample> gpu_tmp_;
    std::vector<core::metrics::DiskIdleSample> disk_tmp_;
    std::vector<core::metrics::DiskRateSample> disk_rate_tmp_;
    std::vector<core::metrics::CoreCpuSample> core_tmp_;
};

}  // namespace ef::platform::metrics
