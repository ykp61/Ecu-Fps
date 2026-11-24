#pragma once
// Uyumluluk B8 (bosluk 8): destek paketindeki diag.json. Uzak bir PC'deki sorunu anlatan, kisisel veri icermeyen
// salt okuma ozeti: CPU topolojisi, DXGI bagdastiricilari (hangisi kullaniliyor), ekranlar, guc kaynagi, sistem diski
// doluluk orani, dil secim zinciri, ortam bayragi kararlari, destek matrisi. Kullanici adi, bilgisayar adi, yol, seri
// numarasi ALANI YOKTUR; paket yine de tum metni redakte eder (savunma). Okunamayan alan null yazilir (0 uydurulmaz).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/capabilities/device_verification.h"
#include "core/recovery/restore_report.h"
#include "core/serialization/json.h"

namespace ef::core::support {

struct DiagCpu {
    std::string vendor, brand;
    std::optional<std::uint32_t> packages, physical_cores, logical_processors, performance_cores, efficiency_cores;
    std::optional<bool> smt, hybrid;
};

struct DiagAdapter {
    std::uint32_t index = 0;
    std::string name;
    std::optional<std::uint32_t> vendor_id, device_id;
    std::string luid;            // "hhhhhhhh:llllllll" (oturum ici kimlik; kisisel degil)
    std::string driver_version;  // bos = okunamadi
    std::optional<std::uint64_t> dedicated_vram_mib, shared_mib;
    std::string kind;            // discrete | integrated | software | remote | unknown
    bool high_performance = false;  // DXGI yuksek performans tercihindeki ilk donanim bagdastiricisi (oyunlarin karti)
};

struct DiagDisplay {
    std::uint32_t index = 0;
    std::string name;  // EDID model adi (seri numarasi degil)
    bool primary = false;
    std::uint32_t width = 0, height = 0;
    std::optional<double> hz_current, hz_max_same_res;
    bool dynamic_refresh = false;
    std::optional<std::uint32_t> dpi;
    std::optional<bool> hdr_supported, hdr_enabled, vrr_supported;
    std::string gpu_vendor;  // ekrani suren GPU ureticisi (bilinmiyorsa bos)
};

struct DiagPower {
    std::optional<bool> on_ac, has_battery, battery_saver;
    std::optional<int> battery_percent;
    std::string overlay;  // guc modu (best_efficiency | balanced | ...); bos = okunamadi
    std::string plan;     // guc plani turu; bos = okunamadi
};

struct DiagLanguage {
    std::string preference;  // kayitli tercih: system | tr-TR | en-US
    std::string windows_ui;  // Windows goruntu dili (ornek tr-TR)
    std::string resolved;    // acik katalog
};

// Ortam bayragi: ad, ham deger (en cok 8 karakter, yoksa bos) ve uygulamanin verdigi karar.
struct DiagFlag {
    std::string name, value, decision;
};

struct DiagInput {
    std::string app_version, build_id, os_build;
    DiagCpu cpu;
    std::vector<DiagAdapter> adapters;
    std::vector<DiagDisplay> displays;
    DiagPower power;
    std::optional<double> system_disk_used_pct;
    DiagLanguage language;
    std::vector<DiagFlag> flags;
    capabilities::DeviceVerification device;
    json::Value support_matrix;  // Registry::support_matrix; bos (null) = uretilmedi
    // Saha 2026-11-15: geri alinamamis ayarlar (tum journal kapsamlari; recovery::to_json ile redakte: ayar, beklenen,
    // okunan, neden, hata kodu). restore_failures_known=false -> null (okunamadi; "hata yok" denmez).
    std::vector<recovery::FailedRestoreItem> restore_failures;
    bool restore_failures_known = false;
};

inline constexpr std::int64_t kDiagSchemaVersion = 1;

json::Value diag_json(const DiagInput& in);

}  // namespace ef::core::support
