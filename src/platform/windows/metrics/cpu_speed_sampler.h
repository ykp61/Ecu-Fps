#pragma once
// Spec 2026-11-12 C1: CPU hiz siniri sayaclari (PDH, salt okuma; yonetici gerekmez).
//  - "\Processor Information(_Total)\% Processor Performance": anma frekansina gore ortalama hiz (turbo ile 100'u
//    asar). Oran sayacidir: open() taban ornegini alir; her sample() bir onceki toplamadan beri ORTALAMADIR.
//  - "\Processor Information(_Total)\% Performance Limit": Windows'un / bellenimin izin verdigi ust sinir (anlik).
// Sayaclar Ingilizce adla eklenir (Windows dilinden bagimsiz). Okunamayan deger nullopt kalir (0 yazilmaz).
// Bu sayaclar NEDEN tasimaz: sicaklik ya da "termal" iddiasi bu okumadan cikarilmaz.
// Sinif is parcacigi guvenli degildir; tek ornekleyici is parcacigindan kullanilir.

#include <optional>

namespace ef::platform::metrics {

struct CpuSpeedReading {
    std::optional<double> performance_pct;
    std::optional<double> limit_pct;
};

class CpuSpeedSampler {
public:
    CpuSpeedSampler() = default;
    ~CpuSpeedSampler();
    CpuSpeedSampler(const CpuSpeedSampler&) = delete;
    CpuSpeedSampler& operator=(const CpuSpeedSampler&) = delete;

    bool open();  // en az bir sayac eklenebildiyse true; taban ornegi alinir
    CpuSpeedReading sample();
    void close();
    bool is_open() const noexcept { return query_ != nullptr; }

private:
    void* query_ = nullptr;  // PDH_HQUERY (pdh.h basliga sizmasin)
    void* perf_ = nullptr;
    void* limit_ = nullptr;
};

}  // namespace ef::platform::metrics
