#pragma once
// R15: benzer sistemlerle karsilastirma (saf; birim testlenir).
//  - Yalniz GERCEK ornekler kullanilir: eslesme parmak izi + kaynak + olcum kosullari ayni olmalidir.
//  - Eslesen gercek ornek sayisi kMinPeerSamples'in altindaysa sonuc Insufficient'dir; yuzdelik URETILMEZ.
//  - Yuzdelik yalniz Comparable durumunda ve gercek degerlerden hesaplanir (midrank):
//      pct = 100 * (degerden kucuk + 0.5 * esit) / N.
//  - Bu modul ag/telemetri/yukleme bilmez; cagiran yerel havuzu verir. Bos havuz "veri havuzu yok"tur,
//    eksik veri gizlenmez ve ornek sayisi uydurulmaz.

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::insights {

// En az bu kadar eslesen GERCEK ornek olmadan yuzdelik gosterilmez. Esik, veri yerine gecmez: altinda
// daima Insufficient ve gercek sayi doner.
inline constexpr std::size_t kMinPeerSamples = 20;

struct PeerSample {
    std::string fingerprint;                  // kosul izi (ornek: measure::fingerprint)
    std::string source;                       // "builtin" / "in_game"
    std::map<std::string, std::string> conditions;  // olcum kosullari; eslesme icin birebir ayni olmali
    std::map<std::string, double> metrics;    // olculen metrikler; eksik metrik yoktur (0 degil)
};

struct PeerQuery {
    std::string fingerprint;
    std::string source;
    std::map<std::string, std::string> conditions;
    std::string metric;  // "scene_avg_fps" / "avg_fps" ...
    double value = 0;
};

enum class PeerOutcome { Insufficient, Comparable };

enum class PeerInsufficientReason {
    NoPool,            // yerel havuz bos: veri havuzu kurulmadi (N=0)
    NoMatchingSamples,  // havuz var ama kosullara/metrige uyan gercek ornek yok
    BelowMinimum,       // eslesen gercek ornek var, kMinPeerSamples'in altinda
    InvalidQuery,       // sorgu metriği/degeri kullanilamaz (sonuc uretilmez)
};
std::string_view to_string(PeerOutcome o) noexcept;
std::string_view to_string(PeerInsufficientReason r) noexcept;

struct PeerComparison {
    PeerOutcome outcome = PeerOutcome::Insufficient;
    PeerInsufficientReason reason = PeerInsufficientReason::NoPool;
    std::size_t matching_samples = 0;  // kosullara uyan ve metrigi gercek degerle tasiyan ornek sayisi (N)
    std::size_t required_samples = kMinPeerSamples;
    std::optional<double> percentile;  // yalniz Comparable; gercek orneklerden
};

// q.metric tasiyan, parmak izi/kaynak/kosullari birebir eslesen ornekler disinda hicbir girdi kullanilmaz.
PeerComparison compare_peers(const PeerQuery& q, const std::vector<PeerSample>& pool);

}  // namespace ef::core::insights
