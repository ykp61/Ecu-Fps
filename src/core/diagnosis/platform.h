#pragma once
// DIAG-04: platform kurallari (saf; birim testlenir) - yuk alti PCIe linki, uretici destekli limit
// bayraklari + frekans dususu, rakip surec yuku. Girdiler DIAG-01 zaman cizgisinin kullanilabilir
// pencerelerinden cagiran tarafca doldurulur (pencere basina bir ornek).
//  - PCIe yalniz GPU yuk altindayken degerlendirilir; bostaki Gen1/dar link guc tasarrufudur, darbogaz degil.
//  - Isi/guc icin evrensel sicaklik esigi YOK; yalniz uretici bayraklari (ornek NVIDIA perf cap reasons)
//    ve yuk alti frekans dususu. Bayrak desteklenmiyorsa sonuc "Unsupported"tir, "NotLimiting" degil.
//  - Rakip surec: oyun PID'i, kendi surecimiz ve PID 0 (bosta) haric; dusuk pay neden sayilmaz.
//  - Bu sinyallerden donanim arizasi (PSU/VRM/anakart) ASLA cikarilmaz: is_hardware_fault her zaman false.
//  - Eksik veri -> Insufficient. Guven yuzde degil, neden enum'udur.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::diagnosis {

enum class PlatformClass {
    NotLimiting,           // olculdu, sinirlayici degil
    PcieLinkReduced,       // yuk altinda link kart ve yuva azamisinin altinda
    ThermalLimit,          // uretici isi limiti bayragi (yuk altinda)
    PowerLimit,            // uretici guc limiti bayragi (sinir; ariza degil)
    VoltageLimit,          // uretici guvenilirlik/voltaj limiti bayragi
    BackgroundContention,  // rakip surec CPU/GPU payi
    Unsupported,           // gerekli sensor/bayrak bu cihazda/surucude yok
    Insufficient,          // veri yetersiz; karar verilmez
};
inline constexpr int kPlatformClassCount = 8;
std::string_view to_string(PlatformClass c) noexcept;
// Bu modul hicbir sinifi donanim arizasi olarak ilan etmez (PSU/VRM/anakart iddiasi yok).
constexpr bool is_hardware_fault(PlatformClass) noexcept { return false; }

enum class PlatformEvidence {
    GpuUnderLoad,
    GpuIdle,
    GpuLoadUnknown,
    IdleDownclockIgnored,  // bosta dusuk link: guc tasarrufu, sayilmadi
    LinkBelowCardMax,
    LinkBelowSlotMax,
    LinkLimitedBySlot,     // link = yuva azamisi (kart daha yuksek destekliyor): egitim hatasi degil
    LinkAtMax,
    LinkMaxUnknown,        // kart veya yuva azamisi bilinmiyor
    LinkCurrentUnknown,
    PerfCapThermal,
    PerfCapPower,
    PerfCapVoltage,
    PerfCapNone,
    PerfCapUnsupported,
    ClockDropUnderLoad,
    ClockStable,
    ClockUnknown,
    CompetingCpuShare,
    CompetingGpuShare,
    CompetingShareLow,
    ProcessDataMissing,
};
std::string_view to_string(PlatformEvidence e) noexcept;

enum class PlatformConfidence {
    SignalsAgree,          // bayrak + frekans dususu ayni yonde
    VendorFlagOnly,        // yalniz uretici bayragi (frekans bilinmiyor/dusmedi)
    DirectLinkReading,     // link okumasi yuk altinda dogrudan
    MeasuredShare,         // surec payi olculdu
    ConflictingSignals,    // frekans dustu ama bayrak yok vb.
    NoLoadObserved,        // yuk alti pencere yok
    MissingData,
    UnsupportedSensor,
};
std::string_view to_string(PlatformConfidence r) noexcept;

struct PlatformFinding {
    PlatformClass cls = PlatformClass::Insufficient;
    std::vector<PlatformEvidence> supporting;
    std::vector<PlatformEvidence> counter;
    PlatformConfidence reason = PlatformConfidence::MissingData;
    std::vector<std::string> processes;  // rakip surec adlari (kanit)
    bool hardware_fault() const noexcept { return is_hardware_fault(cls); }
};

// Esikler (kalibrasyon DIAG-06'da gozden gecirilir).
inline constexpr double kGpuLoadPct = 60.0;          // bu ve ustu "yuk altinda"
inline constexpr double kClockDropRatio = 0.90;      // referansin %90'inin alti "dusus"
inline constexpr double kLimitWindowShare = 0.25;    // yuk alti pencerelerin en az %25'inde bayrak
inline constexpr double kProcessSharePct = 10.0;     // tek surec CPU/GPU payi esigi
inline constexpr double kTotalCompetingPct = 25.0;   // rakiplerin toplam payi esigi

// ---- PCIe ----
struct PcieSample {
    std::optional<double> gpu_load_pct;
    std::optional<std::uint32_t> cur_gen, cur_width;
    std::optional<std::uint32_t> card_max_gen, card_max_width;
    std::optional<std::uint32_t> slot_max_gen, slot_max_width;
};
PlatformFinding evaluate_pcie(const std::vector<PcieSample>& windows);

// ---- Isi / guc limitleri ----
struct PerfCapFlags {
    bool thermal = false;  // NVIDIA: sw/hw thermal slowdown; diger ureticide esdegeri
    bool power = false;    // guc limiti (TDP/TGP)
    bool voltage = false;  // guvenilirlik voltaji (NVIDIA "reliability voltage")
};
// F-61: NVAPI_GPU_PERF_DECREASE bit maskesini isi/guc/voltaj siniflarina cevirir.
//  - mask == 0: surucu "bildirilen dusus yok" diyor; PerfCapFlags{false,false,false} doner.
//  - THERMAL/POWER_CONTROL bitleri ilgili sinifa eslenir; bu maskede voltaj nedeni yoktur.
//  - UNKNOWN veya taninmayan bit varsa nullopt: "limit yok" denmez, desteklenmiyor/atfedilemez kalir.
//  - AC_BATT/API_TRIGGERED/INSUFFICIENT_POWER bu eslemeye dahil degildir; tek baslarina isi/guc
//    limiti sayilmazlar (gelecek dilimde ayri kanit olarak ele alinabilir).
std::optional<PerfCapFlags> perf_caps_from_mask(std::uint32_t mask);
struct LimitSample {
    std::optional<PerfCapFlags> caps;  // nullopt: uretici/surucu bu bayragi saglamiyor
    std::optional<double> load_pct;
    std::optional<double> clock_mhz;
};
// reference_clock_mhz: ayni kosuda yuk alti gozlenen surekli saat (veya uretici boost); yoksa dusus bilinmez.
PlatformFinding evaluate_limits(const std::vector<LimitSample>& windows, std::optional<double> reference_clock_mhz);

// ---- Arka plan rekabeti ----
struct ProcessShare {
    std::uint32_t pid = 0;
    std::string name;
    std::optional<double> cpu_pct;  // toplam sistem CPU'sunun yuzdesi
    std::optional<double> gpu_pct;  // GPU motor kullanim payi
};
struct ProcessWindow {
    bool measured = false;  // surec listesi bu pencerede alinabildi mi
    std::vector<ProcessShare> processes;
};
PlatformFinding evaluate_background(const std::vector<ProcessWindow>& windows, std::uint32_t game_pid,
                                    std::uint32_t self_pid);

}  // namespace ef::core::diagnosis
