#pragma once
// R13/INS-03: uzun test (isinma) raporunun urun gorunumu (saf; birim testlenir).
//  - Bu katman yalniz analiz SONUCUNU tasir; analyze_endurance semantigini yeniden hesaplamaz.
//  - Olculmeyen deger nullopt kalir; 0 veya tahmini sayi uretilmez.
//  - Sensor yoksa GPU saati/frekans degerleri yoktur ve "sensör yok — güvenli izleme sınırı" notu eklenir.
//  - Bu bir donanim saglik raporu DEGILDIR; not her zaman eklenir.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::measure {

enum class EnduranceStopKind : std::uint8_t { Completed, Cancelled, TimeBudget, ByteBudget, MemoryBudget, DeviceLost, HardwareError, SensorLost };
// Bolum basligi anahtari; Completed icin bos (yalniz tamamlandi satiri gerekmez). Cancelled icin de bos: erken
// durdurmada "kismi" / "en az 2 dakika" notu yazilir (B14), ayri iptal satiri yok.
std::string_view endurance_stop_key(EnduranceStopKind s) noexcept;

struct EnduranceView {
    EnduranceStopKind stop = EnduranceStopKind::Completed;
    std::optional<double> cold_fps, warm_fps, fps_change_pct;      // yalniz olculen pencereler
    std::optional<double> cold_gpu_mhz, warm_gpu_mhz;              // yalniz sensor varsa
    bool frequency_dropped = false;                                 // yalniz sensor varsa
    bool sensors_available = false;
    bool cause_vendor_thermal = false;  // yalniz destekli uretici termal bayragi GOZLENDIYSE true
    bool cause_vendor_power = false;    // guc limiti bayragi; PSU arizasi hukmu degildir
    bool load_not_comparable = false;   // pencereler esdeger yukte degil
    bool load_unknown = false;          // yuk okunamadi
    bool limit_flag_seen = false;       // bayrak goruldu ama neden sayilmadi
    // Uyumluluk B14: son iki 60 sn penceresi FPS (ve okunabiliyorsa GPU saati) %2 icinde mi; yalniz analiz sonucu.
    bool stability_checked = false;
    bool stable = false;
    bool stability_fps_only = false;
    double duration_s = 0;  // kosucu saatiyle son ornek (kismi sonuc etiketi)
    // GPU telemetrisi bu kosuda desteklenmiyordu (NVIDIA disi / sahne baska kartta): notlar "desteklenmiyor" der,
    // "okunamadi" demez. Destekli sensor bos donduyse false kalir.
    bool telemetry_unsupported = false;
    bool has_comparison() const noexcept { return cold_fps.has_value() && warm_fps.has_value(); }
    // Kullanici erken durdurdu ama soguk/isinmis pencere doldu: sonuc gosterilir, "kismi" etiketlidir.
    bool partial() const noexcept { return stop == EnduranceStopKind::Cancelled && has_comparison(); }
};

// B14: calisirken ilerleme cubugunun altindaki asama. Preparing: kosucu saati henuz baslamadi (cihaz/sahne kuruluyor).
enum class EndurancePhase : std::uint8_t { Preparing, Cold, Warming, Warm };
// started: kosucu t=0 bildirdi mi. Soguk: ilk window_s; isinmis: son window_s (planned_s - window_s sonrasi).
EndurancePhase endurance_phase(bool started, double elapsed_s, double planned_s, double window_s = 60) noexcept;
std::string_view endurance_phase_key(EndurancePhase p) noexcept;

// Rapor satiri: deger yoksa "Olculemedi" gosterilir (satir kaybolmaz); sahte deger yok.
struct EnduranceRow {
    std::string label_key;                    // katalog anahtari
    std::optional<double> value;              // olculen deger veya nullopt
    int decimals = 0;
    std::string unit;                         // "FPS" / "MHz" / "%" (birim simgesi)
};

std::vector<EnduranceRow> endurance_rows(const EnduranceView& v);
// Yerellestirilmis durum/uyari anahtarlari (katalogda hepsi var): durdurma nedeni, sensor siniri,
// yetersiz karsilastirma, "saglik raporu degil". Olculen degerlerden turetilen sayi yok.
std::vector<std::string> endurance_note_keys(const EnduranceView& v);
// Gecmis kaydi: yalniz OLCULEN metrikler (eksik metrik yazilmaz).
std::map<std::string, double> endurance_metrics(const EnduranceView& v);
// Gecmis kaydi: gercek dislamalar (butce/iptal, sensor yok, yetersiz pencere, frekans dususu).
std::vector<std::string> endurance_exclusions(const EnduranceView& v);

}  // namespace ef::core::measure
