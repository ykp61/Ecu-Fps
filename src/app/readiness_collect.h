#pragma once
// CHK: Oyun Hazirligi okuyucularini (platform/windows/readiness/*) calistirip kurallardan bulgu toplar.
// Salt okuma; ayar degistirmez. collect_* ag paketi gondermez; run_network_test yalniz kullanici
// "Agi test et" dediginde ICMP yanki gonderir (modem + 1.1.1.1 + 8.8.8.8, 30 ornek).
#include <atomic>
#include <functional>
#include <optional>
#include <vector>

#include "core/diagnostics/health_test.h"
#include "core/diagnostics/incident_engine.h"
#include "core/readiness/readiness.h"

namespace ef::app {
// game_exe_paths: P6, kullanicinin sectigi/tespit edilen oyunlarin tam EXE yollari (utf-8); bos olabilir.
// incident_days: EF-X01 olay gunlugu penceresi (7 varsayilan, kullanici secerse 30; baska deger 7'ye duser).
std::vector<core::readiness::Check> collect_readiness_checks(const std::vector<core::readiness::PingStats>& pings,
                                                             int incident_days = core::diagnostics::kDefaultDays,
                                                             const std::vector<std::string>& game_exe_paths = {},
                                                             core::readiness::GpuPrefInfo* gpu_pref_out = nullptr);  // T01: plan icin ayni okuma
std::vector<core::readiness::PingStats> run_network_test(const std::atomic<bool>& cancel);
// T10: yalniz kullanici "DNS karsilastir" dediginde: mevcut sistem DNS'i + hazir listeler; her cozucuye yalniz A sorgusu.
// nullopt: iptal edildi veya hic cozucu yok (onceki sonuc korunur).
std::optional<core::net::DnsBenchReport> run_dns_compare(const std::atomic<bool>& cancel);
// T16: saglik testi kosucusu (app_health.cpp). Ayar yazmaz; yalniz CPU yuku + bellek deseni. progress(phase, sec) is
// parcaciginda ~1 Hz cagrilir. nullopt: iptal edildi (kismi sonuc sunulmaz).
std::optional<core::diagnostics::HealthReport> run_health_test(const std::atomic<bool>& cancel, const std::function<void(int, int)>& progress);
}
