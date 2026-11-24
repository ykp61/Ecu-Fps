#pragma once
// Task 9: canli ag hizi ornekleyicisi (salt okuma, yonetici gerekmez, PAKET GONDERMEZ).
//  - Bagdastirici: GetBestInterface(1.1.1.1) yalniz yonlendirme tablosuna bakar (network_reader ile
//    ayni yontem); if_index 10 sn onbellekte tutulur.
//  - Hiz: GetIfEntry2 InOctets/OutOctets iki ardisik ornek farki (core::metrics::net_rates);
//    baglanti hizi ReceiveLinkSpeed. Bekleme/uyku yoktur; canli is parcaciginda ~1 Hz cagrilir.
//  - Ilk ornek, bagdastirici degisimi, sayac geri gitmesi: hiz NotSampled. Rota yok / okuma hatasi:
//    uc alan SourceError. Sinif is parcacigi guvenli degildir.

#include <cstdint>
#include <optional>

#include "core/metrics/live.h"

namespace ef::platform::metrics {

class NetRateSampler {
public:
    void sample(core::metrics::LiveSnapshot& s, std::int64_t mono_ms);

private:
    std::uint32_t if_index_ = 0;
    std::int64_t if_ms_ = -1;
    std::optional<core::metrics::NetCounters> prev_;
};

}  // namespace ef::platform::metrics
