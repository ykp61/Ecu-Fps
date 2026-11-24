#pragma once
// P9/B5: Olcum kosulari sirasinda kullanilan salt okuma sistem ornekleyicileri (CPU sureleri, masaustu bilesimcisi,
// guc/pil durumu). Denge testi, Oyunda Test ve ABBA deneyi paylasir. Hicbiri ayar yazmaz.
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/metrics/live.h"

namespace ef::app {

// Spec 2026-11-12 A3: denge testinin ortam karistiricilari (salt okuma). Okunamayan deger "var" sayilmaz: alan
// bilinmiyorsa false kalir (kural yalniz gorulen kaniti kullanir).
struct BalanceEnvironment {
    bool on_battery = false, battery_saver = false, power_saver_plan = false;
    bool efficiency_overlay = false;  // denge_v3 (Uyumluluk B4 bosluk 13): guc modu "en iyi guc verimliligi"
};
BalanceEnvironment read_balance_environment();

// denge_v2: kendi oturumumuzdaki masaustu bilesimcisi (dwm.exe) surecleri. "Baska program" sayilmaz.
std::vector<std::uint32_t> session_compositor_pids();

// GetSystemTimes + GetProcessTimes: ikisi de CPU SURESI (100 ns). `own`: payi "baska program" sayilmayan surec (denge
// testinde bu uygulama, oyun ici testte oyun). Okunamazsa nullopt.
std::optional<core::metrics::CpuTimes> read_cpu_times(HANDLE own, const std::vector<HANDLE>& exempt_procs = {}) noexcept;

inline std::int64_t steady_us(std::chrono::steady_clock::time_point t) noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
}

}  // namespace ef::app
