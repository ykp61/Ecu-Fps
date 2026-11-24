#pragma once
// GPU-02: NVIDIA surucu profili (DRS) adaptoru. Yalniz izinli ayar kimlikleri yazilir; snapshot
// degerin profilde acik mi yoksa global/temel/varsayilandan miras mi geldigini kaydeder.
// Geri alma: miras -> acik override silinir (miras geri gelir); acik -> eski deger yazilir.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::nvidia {

// Katalog eslemesi (research/SETTINGS_CATALOG.md). Kimlikler NvApiDriverSettings.h ile ayni.
enum class DrsSetting : std::uint32_t {
    PowerMode = 0x1057EB71,         // G01 PREFERRED_PSTATE (1 = PREFER_MAX; varsayilan 5 = OPTIMAL_POWER)
    MaxPrerendered = 0x007BA09E,    // G02 PRERENDERLIMIT (1 = kuyrugu 1 kareye sinirla; Low Latency "On"
                                    // esdegeri, resmi yardim metni; yalniz DX9/DX11, DX12/Vulkan oyun kontrolunde)
    VSync = 0x00A879CF,             // G05 VSYNCMODE
    FrameRateLimit = 0x10835002,    // G06 FRL_FPS
    // B6 (ADR-040 m.8): kimlik ve degerler resmi NVIDIA nvapi SDK deposu NvApiDriverSettings.h (MIT) ile dogrulandi
    // (artifacts/evidence/R-B6.md). Ikisi de admin.h bayragiyla KAPALI; izin listesinde olmalari geri alma icindir.
    ThreadedOptimization = 0x20C1221E,  // G13 OGL_THREAD_CONTROL (1 = ENABLE, 2 = DISABLE, DEFAULT 0 = surucu karari)
    FxaaEnable = 0x1074C972,            // G14 FXAA_ENABLE (0 = OFF = DEFAULT, 1 = ON)
    // "Ultra" dusuk gecikme kipi: resmi baslikta ayri DRS kimligi YOK -> eklenmedi (veri yetersiz; R-B6.md).
    ShaderCacheSize = 0x00AC8497,   // G07 PS_SHADERDISKCACHE_MAX_SIZE (baslik varsayilani 0x4000 = 16384;
                                    // MB birimiyle 16 GB; 10240 hedefi bu nedenle asla "artirma" degildir — M3 kapali)
};
bool is_allowed_setting(std::uint32_t id) noexcept;
std::string_view catalog_id(DrsSetting s) noexcept;  // "G01" ...

enum class DrsLocation { CurrentProfile, Global, Base, Default };
std::string_view to_string(DrsLocation l) noexcept;

struct DrsValue {
    std::uint32_t value = 0;
    DrsLocation location = DrsLocation::Default;
};

struct DrsTarget {
    bool global = true;
    std::string app;  // global=false ise uygulama exe adi (cs2.exe)
    std::uint32_t setting = 0;
};
// "drs:global:1057EB71" veya "drs:app:cs2.exe:1057EB71". Izinsiz ayar/gecersiz exe adi -> hata.
contracts::Result<DrsTarget> parse_drs_target(std::string_view target);
std::string format_drs_target(const DrsTarget& t);

class DrsApi {
public:
    virtual ~DrsApi() = default;
    // Etkin deger ve kaynagi. Uygulama profili yoksa MissingValue.
    virtual contracts::Result<DrsValue> get(const DrsTarget& t) = 0;
    virtual contracts::Status set(const DrsTarget& t, std::uint32_t value) = 0;
    virtual contracts::Status remove(const DrsTarget& t) = 0;  // acik degeri sil, mirasa don
    virtual contracts::Status save() = 0;
};

class DrsAdapter final : public recovery::SettingAdapter {
public:
    explicit DrsAdapter(DrsApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    // Profildeki ACIK deger (miras -> nullopt): restore semantigi ile tutarli.
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

    static std::string encode(std::uint32_t v);
    static std::optional<std::uint32_t> decode(std::string_view s);

private:
    DrsApi& api_;
};

// Testler icin bellek ici DRS (global + uygulama profilleri, miras zinciri).
class FakeDrs final : public DrsApi {
public:
    contracts::Result<DrsValue> get(const DrsTarget& t) override;
    contracts::Status set(const DrsTarget& t, std::uint32_t value) override;
    contracts::Status remove(const DrsTarget& t) override;
    contracts::Status save() override {
        ++saves;
        return contracts::ok_status();
    }
    std::map<std::uint32_t, std::uint32_t> defaults;            // surucu varsayilani
    std::map<std::uint32_t, std::uint32_t> global;              // global profilde acik
    std::map<std::string, std::map<std::uint32_t, std::uint32_t>> apps;  // uygulama profili -> acik
    int saves = 0;
};

}  // namespace ef::core::adapters::nvidia
