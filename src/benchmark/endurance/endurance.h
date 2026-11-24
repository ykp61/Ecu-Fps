#pragma once
// INS-03: uzun test ve isinma analizi (saf cekirdek; birim testlenir).
//  - Butce: sure, yazilan bayt, bellek tavanlari; asilirsa test durur (Partial) — sinirsiz uzun test yok.
//  - Iptal her an: kosucu her ornekte iptal bayragini kontrol eder (acil iptal).
//  - Soguk (ilk pencere) / isinmis (son pencere) karsilastirmasi kosu-ici pencere ortalamalariyla; frekans
//    dususu ve FPS dususu ayri raporlanir. Desteklenen ureticinin sinirlama bayragi yoksa "neden" yazilmaz.
//  - Sensor yoksa: "guvenli izleme siniri" acikca belirtilir; sonuc donanim saglik raporu degildir.

#include <optional>
#include <string>
#include <vector>

namespace ef::benchmark {

struct EnduranceBudget {
    double planned_seconds = 600;   // 10 dk: bu sureye ulasan kosu tamamlanmistir (Completed)
    double max_seconds = 660;       // guvenlik tavani; normalde planli sure once dolar (TimeBudget = asim)
    std::uint64_t max_bytes_written = 1ull << 30;
    std::uint64_t max_memory_bytes = 512ull << 20;
};

struct EnduranceSample {
    double t_s = 0;
    std::optional<double> fps;
    std::optional<double> gpu_clock_mhz, cpu_clock_mhz;
    std::optional<bool> vendor_thermal_flag;  // destekliyse; yoksa nullopt
    std::optional<bool> vendor_power_flag;    // guc limiti; tek basina PSU arizasi degildir
    std::optional<double> gpu_load_pct;       // pencerelerin esdeger yuk altinda oldugunun kaniti
    std::uint64_t bytes_written = 0, memory_bytes = 0;
};

enum class EnduranceStop { Completed, Cancelled, TimeBudget, ByteBudget, MemoryBudget, DeviceLost, HardwareError, SensorLost };
std::string_view to_string(EnduranceStop s) noexcept;
// Ornek akisinda butce kontrolu (kosucu her ornekte cagirir).
// Planli sure doldu mu (gecerli bitis); butce asimindan ayridir.
bool planned_duration_reached(const EnduranceBudget& b, const EnduranceSample& s) noexcept;
std::optional<EnduranceStop> budget_exceeded(const EnduranceBudget& b, const EnduranceSample& s) noexcept;

// Saat bir kez okunduktan sonra ust uste kMaxMissing ornek gelmezse sensor kaybi: test korunmasiz surdurulmez.
class SensorWatch {
public:
    static constexpr int kMaxMissing = 5;
    bool lost(bool have) noexcept {
        if (have) {
            seen_ = true;
            missing_ = 0;
            return false;
        }
        return seen_ && ++missing_ >= kMaxMissing;
    }

private:
    bool seen_ = false;
    int missing_ = 0;
};

struct EnduranceReport {
    EnduranceStop stop = EnduranceStop::Completed;
    std::optional<double> cold_fps, warm_fps, fps_change_pct;
    std::optional<double> cold_gpu_mhz, warm_gpu_mhz;
    bool frequency_dropped = false;
    // "vendor_thermal_flag" / "vendor_power_flag": bayrak ISINMIS pencerede gozlendi, saat ve is ciktisi dustu; aksi bos.
    std::string cause;
    bool load_not_comparable = false;  // yuk farkli/dusuk (kare siniri vb.): saat dususu isi kaniti degil
    bool load_unknown = false;         // yuk okunamadi: esdegerlik kanitlanamaz, neden yazilmaz
    bool limit_flag_seen = false;      // isinmis pencerede bayrak goruldu (neden sayilmasa da)
    bool sensors_available = false;
    bool is_health_report = false;  // her zaman false: saglik raporu degil
    // Uyumluluk B14: son iki pencere (sondan onceki ve son window_s) FPS ve okunabiliyorsa GPU saati ortalamasi
    // birbirine kStableTolerance icinde mi. Iki pencerede FPS yoksa degerlendirilmez (stability_checked=false).
    bool stability_checked = false;
    bool stable = false;
    bool stability_fps_only = false;  // GPU saati iki pencerede okunamadi: yalniz FPS'e bakildi
    double duration_s = 0;            // son ornegin zamani (kosucu saati; ornek yoksa 0)
    // B14 incelemesi: GPU telemetrisi bu kosuda DESTEKLENMIYOR (NVAPI yok, sahne baska/yazilim kartinda). "Okunamadi"dan
    // ayridir: destekli sensor bos donerse false kalir. Kosucu sahne acildiktan sonra yazar; bilinmiyorsa false.
    bool telemetry_unsupported = false;
};

inline constexpr double kStableTolerance = 0.02;  // %2

// B14: kosucu saatine gore zaman damgali telemetri okumasi.
struct TelemetryReading {
    double t_s = 0;
    std::optional<double> gpu_clock_mhz, gpu_load_pct;
    std::optional<bool> thermal, power;
};
// Yalniz [a, b] cizim penceresindeki okumalar: saat/yuk ortalamasi, bayraklar herhangi biri. Pencerede okuma yoksa
// (veya alan hic okunmadiysa) alan bos kalir; pencere disindaki (hazirlik/bosaltma) okumalar sayilmaz.
void telemetry_in_window(const std::vector<TelemetryReading>& r, double a, double b, EnduranceSample& out);

// window_s: soguk/isinmis pencere uzunlugu (ornek 60 s).
EnduranceReport analyze_endurance(const std::vector<EnduranceSample>& s, EnduranceStop stop, double window_s = 60);

}  // namespace ef::benchmark
