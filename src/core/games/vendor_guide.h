#pragma once
// F-12/F-15/F-16/F-20/F-26/F-28: satıcı sürücü rehberi — yalnız statik, kanıta dayalı metin.
//  - Sürücü ayarı OKUNMAZ/YAZILMAZ: ADLX ve NVAPI DRS profil deposu bu üründe bağlı değildir.
//  - Metinler "şu an şöyle" demez; "menüde şunu kontrol edin" der. Uydurma durum/ölçüm yok.

#include <optional>
#include <string>
#include <vector>

#include "core/vendors/dispatch.h"

namespace ef::core::games {

struct VendorGuideSection {
    std::string title_key;               // vendor.<x>.title
    std::vector<std::string> point_keys;  // vendor.<x>.<konu>
};

// NVIDIA (F-12, F-15) ve AMD (F-16, F-20, F-26, F-28) bölümleri; sabit sıra.
std::vector<VendorGuideSection> vendor_guide_sections();

// ---------------------------------------------------------------------------------------------------------------
// R-B7: AMD / Intel dusuk gecikme rehberi (YALNIZ okuma + rehber; yazma YOK, ADR-039/ADR-040 m.9).
//  - "Desteklenmiyor" ile "Veri yetersiz" AYRIDIR:
//      Unsupported      : surucu runtime'i kullanilamaz (yok/guvenilmez/yuklenemedi) VEYA surucu ozelligi yok diyor.
//      InsufficientData : runtime yuklu ama ozellik durumu okunamadi (bilinmeyen surum/yetenek). Durum uydurulmaz.
//      GuideOnly        : runtime yuklu ve ozellik var; kullanici ayari KENDISI uygular. Uygulama okumaz/yazmaz.
//  - Yonetici hakki API yoklugunu gidermez; girdi olarak bile alinmaz.
//  - Farkli mekanizmalar (surucu paneli / oyun ici SDK) tek ad altinda BIRLESTIRILMEZ: ayri girdi, ayri anahtar.
//  - Surum numarasi, menu yolu, FPS/kazanc iddiasi yok. Resmi kaynaktan dogrulanamayan ayrinti yazilmaz.
//  - NVIDIA ve bilinmeyen uretici icin girdi uretilmez (NVIDIA bu dalda dokunulmaz).
// ---------------------------------------------------------------------------------------------------------------
enum class LowLatencyStatus { Unsupported, InsufficientData, GuideOnly };
enum class LowLatencyKind { DriverPanel, InGameSdk };

struct LowLatencyMechanism {
    LowLatencyKind kind = LowLatencyKind::DriverPanel;
    std::string text_key;  // vendor.ll.<amd|intel>.<driver|ingame>
};

struct LowLatencyProbe {
    vendors::Vendor vendor = vendors::Vendor::None;
    vendors::RuntimeState runtime = vendors::RuntimeState::Missing;
    std::optional<bool> feature_reported;  // nullopt = okunamadi (veri yetersiz)
};

struct LowLatencyGuide {
    vendors::Vendor vendor = vendors::Vendor::None;
    LowLatencyStatus status = LowLatencyStatus::Unsupported;
    std::string status_key;                       // vendor.ll.status.*
    std::string matrix_reason_key;                // hw.matrix.* (vendor_matrix G03 AMD / G04 Intel: neden yazma yok)
    std::vector<LowLatencyMechanism> mechanisms;  // Unsupported iken bos
    std::string footnote_key;                     // mechanisms doluysa: menu adlari dogrulanmadi notu
    bool offers_write = false;                    // DEGISMEZ: her zaman false (islem/Duzelt yok)
};

// AMD ve Intel icin rehber; NVIDIA/None icin nullopt (bu dal onlara dokunmaz).
std::optional<LowLatencyGuide> low_latency_guide(const LowLatencyProbe& probe);

// B-012: envanter + gozlenen runtime durumundan rehber girdileri. Yazilim/uzak bagdastirici, bilinmeyen uretici ve
// NVIDIA atlanir; ayni uretici bir kez (bagdastirici sirasi korunur). Runtime listede yoksa Missing sayilir.
// feature_reported her zaman nullopt: ADLX/IGCL bagli degil (ADR-039), ozellik durumu uydurulmaz.
std::vector<LowLatencyProbe> low_latency_probes(const std::vector<inventory::GpuAdapter>& gpus,
                                                const std::vector<vendors::RuntimeInfo>& runtimes);

// Satici kartinda cizilecek salt okuma satirlari (katalog anahtari). Her girdi icin: baslik, durum, varsa mekanizmalar
// ve dipnot, son olarak "neden yazma yok" (vendor_matrix). Islem/dugme yoktur.
struct LowLatencyLine {
    std::string key;
    bool heading = false;
    bool secondary = false;  // durum / dipnot / matris nedeni (ikincil ton)
};
std::vector<LowLatencyLine> low_latency_card_lines(const std::vector<LowLatencyProbe>& probes);

}  // namespace ef::core::games
