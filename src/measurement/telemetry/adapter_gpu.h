#pragma once
// Spec 2026-11-12 A3 (D6): denge testinin "arka plan GPU" karistiricisi icin, bir bagdastiricida (LUID) BASKA
// sureclerin 3D motor kullanimi. PDH "\GPU Engine(*engtype_3D)\Utilization Percentage" butun surecler icin bir kez
// eklenir, `seconds` kadar beklenir ve aralik ortalamasi okunur (oran sayaci: iki toplama gerekir).
//  - Toplama kurali saftir: core::metrics::other_process_3d_pct (kendi surecimiz haric; motor basina surecler
//    toplanir, bagdastirici degeri motorlarin en buyugu).
//  - Sayac yoksa / okunamadiysa / `cancel` kalktiysa nullopt (0 yazilmaz; kural bunu "bilinmiyor" notu yapar).
//  - Yalniz kosudan ONCE cagrilir: GPU motoru orneklerini toplamak CPU harcar, olcum sirasinda yapilmaz.
// Salt okuma; yonetici gerekmez.

#include <atomic>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ef::measurement {

inline constexpr double kOtherGpuSampleSeconds = 1.0;

std::optional<double> sample_other_process_gpu_pct(std::string_view adapter_luid, std::uint32_t own_pid, double seconds,
                                                   const std::atomic<bool>& cancel, const std::vector<std::uint32_t>& exempt_pids = {});

}  // namespace ef::measurement
