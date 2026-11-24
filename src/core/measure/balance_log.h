#pragma once
// Uyumluluk B8 (bosluk 8): logs/balance.log satirlari. Her denge adimi icin tek satir: durum, FPS, GPU mesgul orani,
// disjoint kare, gecersizlik nedeni, olcum penceresinde baska programlarin CPU yuzdesi. Uzak PC'de "Belirlenemedi"
// sonucunun nedenini destek paketinden okumak icindir. Okunamayan deger "-" yazilir (0 uydurulmaz). Kisisel veri yok.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ef::core::measure {

struct BalanceStepLog {
    std::string_view step;     // "S1" ... "T2"
    std::string_view status;   // kosucu durumu: measured | unmeasurable | timed_out | ...
    std::uint32_t attempts = 0;
    std::size_t frames = 0;
    double fps = 0;
    double cv = 0;
    std::optional<double> gpu_busy;    // [0,1]
    std::uint32_t disjoint_frames = 0;
    std::string_view invalid;          // StepStats::reason (bos = gecerli)
    std::optional<double> other_cpu_pct;
    std::string_view error;            // kosucu hata anahtari (bos = yok)
};
std::string balance_step_log_line(const BalanceStepLog& s);

struct BalanceRunLog {
    std::string_view rule;              // kBalanceRuleVersion
    std::optional<std::uint32_t> adapter_vendor_id;
    bool quick = false;
    bool ok = false;
    bool cancelled = false;
    std::string_view error;
    std::uint32_t retries = 0;
    std::optional<double> other_cpu_pct;
};
std::string balance_run_log_line(const BalanceRunLog& r);

}  // namespace ef::core::measure
