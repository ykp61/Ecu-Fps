#pragma once
// MEAS-03 platform: dis surecin (oyun) GPU kullanimi ve VRAM kullanimi, PID'ye bagli PDH sayaclariyla.
//  - "\GPU Process Memory(pid_<pid>_*)\Dedicated Usage" (bayt, tum adaptorler toplami)
//  - "\GPU Engine(pid_<pid>_*engtype_3D)\Utilization Percentage": surecin 3D motor kullanimi, BAGDASTIRICI (LUID)
//    bazinda toplanir ve surecin en cok kullandigi bagdastiricinin degeri verilir (spec 2026-11-12 B). Birden cok
//    ekran karti olan sistemde bagdastiricilar birbirine eklenmez; secilen LUID ayrica bildirilir.
// Bu deger surecin KULLANIMIDIR; butce (OS'un verdigi pay) degildir. Kendi DXGI butcemizle karistirilmaz.
// Sayac yoksa/surec yoksa nullopt + neden (0 yazilmaz). Yonetici gerekmez.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "core/metrics/process_engine.h"

namespace ef::measurement {

// Uyumluluk B4 (bosluk 15/16): motor sayaci artik butun turleri okur ("\GPU Engine(pid_<pid>_*)"); surecin GPU
// kullanimi Gorev Yoneticisi yontemiyle (bagdastirici basina en yuklu motor turu) ayrica verilir, 3D ikincildir.
// Joker sayaclar, bu surece ait ornek gorulmedikce ~5 sn'de bir ve acilistan ~30 sn sonra bir kez yeniden
// genisletilir (process_gpu_reexpand_due): sonradan olusan ornekler (cihaz yeniden kuruldu, bagdastirici degisti) okunur.
struct ProcessGpuReading {
    std::optional<std::uint64_t> dedicated_bytes;
    std::optional<double> engine_3d_pct;  // secilen bagdastiricidaki 3D motorlar (kirpilmamis; 100'u asabilir)
    std::string engine_luid;              // "hhhhhhhh:llllllll"; engine_3d_pct yoksa bos
    std::size_t engine_adapters = 0;      // surecin 3D ornegi bulunan bagdastirici sayisi
    // Gorev Yoneticisi yontemi: en yuksek bagdastiricida en yuklu motor turu (kirpilmamis). Yoksa bos.
    std::optional<double> engine_max_pct;
    std::string engine_max_luid;  // engine_max_pct'nin bagdastiricisi
    std::string engine_max_type;  // "3d", "compute_0", ...
    std::uint32_t reexpansions = 0;  // bu sorguda kac kez yeniden genisletildi (tani)
    std::string missing_reason;
};

class ProcessGpuSampler {
public:
    explicit ProcessGpuSampler(std::uint32_t pid) : pid_(pid) {}
    ~ProcessGpuSampler();
    ProcessGpuSampler(const ProcessGpuSampler&) = delete;
    ProcessGpuSampler& operator=(const ProcessGpuSampler&) = delete;

    bool open();                 // sorguyu kurar (ilk ornek yuzde icin taban)
    ProcessGpuReading sample();  // ikinci ve sonraki cagrilarda yuzde gecerli
    void close();
    std::uint32_t pid() const noexcept { return pid_; }

private:
    std::uint32_t pid_;
    void* query_ = nullptr;
    void* mem_ = nullptr;
    void* eng_ = nullptr;
    void* adapter_ = nullptr;
    bool primed_ = false;
    // Uyumluluk B4 (bosluk 16): yeniden genisletme. Yeni sayaclar eskileriyle birlikte toplanir; iki toplama sonra
    // (yuzde tabani hazir) eskilerin yerine gecer, olcumde bosluk olmaz.
    void* mem_pending_ = nullptr;
    void* eng_pending_ = nullptr;
    bool pending_ = false;
    std::uint32_t pending_collects_ = 0;
    std::uint32_t reexpansions_ = 0;
    core::metrics::ProcessGpuReexpand reexpand_;
    void start_reexpand();
};

}  // namespace ef::measurement
