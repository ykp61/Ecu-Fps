#pragma once
// EF-X13: yuk altinda gecikme (bufferbloat) testi karari (saf; birim testlenir).
//  - Bosta RTT tabani, sonra indirme ve yukleme yuku AYRI; her fazda ayni hedefe RTT.
//  - Metrik: p95(yuklu RTT) - p95(bos RTT), yon basina. "Baglanti iyi" yalniz hat doyurulduysa denir.
//  - Butce: toplam 60 sn / 256 MiB. Asilirsa durur. Iptal her an.
//  - Islettigimiz/izinli bir test sunucusu yoksa test calismaz: BlockedBackend (ucretsiz 3. taraf sunucuya yuk yok).
//  - Oyun acikken, algilama belirsizken veya baska test surerken baslamaz.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ef::core::readiness {

inline constexpr double kLoadMaxSeconds = 60.0;
inline constexpr std::uint64_t kLoadMaxBytes = 256ull << 20;

enum class LoadVerdict { Ok, Bloat, LossUnderLoad, InsufficientLoad, BlockedBackend, Cancelled, BudgetReached, Gated, NoBaseline };
std::string_view to_string(LoadVerdict v) noexcept;

struct LoadPhase {
    std::vector<std::optional<double>> rtt_ms;  // kayip ornek nullopt
    double mbps = 0;                            // fazdaki ortalama aktarim
    double seconds = 0;
    bool plateau = false;                       // son pencerede aktarim yatay (hat doymus olabilir)
};

struct LoadTestInput {
    bool endpoint_configured = false;
    bool gate_open = false;         // oyun yok, algilama taze, baska test yok
    bool cancelled = false;
    double elapsed_s = 0;
    std::uint64_t bytes = 0;
    std::optional<double> link_mbps;  // bagdastirici baglanti hizi
    std::vector<std::optional<double>> idle_rtt_ms;
    LoadPhase download, upload;
};

struct LoadTestResult {
    LoadVerdict verdict = LoadVerdict::BlockedBackend;
    std::optional<double> idle_p95_ms, bloat_down_ms, bloat_up_ms;
    double idle_loss_pct = 0, down_loss_pct = 0, up_loss_pct = 0;  // ICMP oncelik dusurmesi de kayip gibi gorunebilir
};

// Calisma sirasinda: devam edilebilir mi (butce/iptal). Gerekce yoksa nullopt.
std::optional<LoadVerdict> load_budget_stop(double elapsed_s, std::uint64_t bytes, bool cancelled) noexcept;
LoadTestResult evaluate_load_test(const LoadTestInput& in);

}  // namespace ef::core::readiness
