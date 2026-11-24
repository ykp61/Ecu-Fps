#pragma once
// GPU-02 platform: gercek NVAPI (System32\nvapi64.dll, GPU-01 guvenli yukleyici) ile DRS ve telemetri.
// Statik nvapi64.lib kullanilmaz; fonksiyonlar nvapi_QueryInterface + nvapi_interface.h kimlikleriyle alinir.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "contracts/result.h"
#include "core/adapters/nvidia/drs.h"

namespace ef::platform::nvidia {

struct GpuTelemetry {
    std::optional<int> temperature_c;
    std::optional<int> gpu_utilization_pct;
    // F-61: surucunun guncel performans dususu nedeni bit maskesi (NVAPI_GPU_PERF_DECREASE).
    // 0 = bildirilen dusus yok; desteklenmiyorsa nullopt. Tek basina ariza teshisi degildir.
    std::optional<std::uint32_t> perf_decrease_mask;
    // F-61: grafik motoru guncel saat (MHz, NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS). Desteklenmiyorsa nullopt.
    std::optional<double> graphics_clock_mhz;
    std::string name;
};

class Nvapi {
public:
    // Runtime yok/guvenilmez/baslatilamazsa hata (Unsupported/Denied); cokme yok.
    static contracts::Result<std::unique_ptr<Nvapi>> open();
    ~Nvapi();

    std::string driver_version() const;
    // Birincil (ilk) fiziksel GPU telemetrisi. Okunamayan alan nullopt.
    GpuTelemetry telemetry() const;
    // DRS oturumu (ayarlar yuklenmis). Yazma genellikle yonetici ister.
    std::unique_ptr<core::adapters::nvidia::DrsApi> drs();

    struct Impl;

private:
    explicit Nvapi(std::unique_ptr<Impl> i);
    std::unique_ptr<Impl> impl_;
};

// OPT-10: istek aninda acilan DRS oturumu. NVIDIA calisma zamani yoksa her cagri Unsupported (cokme yok).
// refresh() oturumu birakir; sonraki okuma ayarlari yeniden yukler (baska aracin degisikligi gorulsun).
class LazyDrs final : public core::adapters::nvidia::DrsApi {
public:
    contracts::Result<core::adapters::nvidia::DrsValue> get(const core::adapters::nvidia::DrsTarget& t) override;
    contracts::Status set(const core::adapters::nvidia::DrsTarget& t, std::uint32_t value) override;
    contracts::Status remove(const core::adapters::nvidia::DrsTarget& t) override;
    contracts::Status save() override;
    void refresh();
    bool available();

private:
    core::adapters::nvidia::DrsApi* ensure();
    std::unique_ptr<Nvapi> nv_;
    std::unique_ptr<core::adapters::nvidia::DrsApi> drs_;
    bool tried_ = false;
};

}  // namespace ef::platform::nvidia
