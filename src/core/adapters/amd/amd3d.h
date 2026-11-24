#pragma once
// GPU-03: AMD Radeon 3D ayarlari adaptoru (G03 Anti-Lag, G08 Chill, G09 Boost) — lisanstan bagimsiz
// mantik katmani. Gercek ADLX baglayicisi AMD SDK lisansi kabul edilene dek yoktur (B-006); bu katman
// ayni arayuzle sahte API uzerinde test edilir.
// Kurallar: IsSupported=false -> kontrollu Unsupported (yazma denenmez); izin hatasi Denied;
// geri alma eski degeri yazar. CPU/PBO/voltaj YAZIMI YOKTUR (yalniz izleme, o da lisans bekliyor).

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "contracts/support.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::amd {

enum class Amd3dFeature { AntiLag, Chill, Boost };
std::string_view to_string(Amd3dFeature f) noexcept;
std::string_view catalog_id(Amd3dFeature f) noexcept;  // G03 / G08 / G09

// Hedef: "adlx:gpu<index>:antilag" | ":chill" | ":boost"
struct Amd3dTarget {
    std::uint32_t gpu = 0;
    Amd3dFeature feature = Amd3dFeature::AntiLag;
};
contracts::Result<Amd3dTarget> parse_amd_target(std::string_view s);

// ADLX IADLX3D* arayuzlerinin ihtiyac duyulan alt kumesi (IsSupported/IsEnabled/SetEnabled).
class Amd3dApi {
public:
    virtual ~Amd3dApi() = default;
    virtual contracts::Result<bool> is_supported(const Amd3dTarget& t) = 0;
    virtual contracts::Result<bool> is_enabled(const Amd3dTarget& t) = 0;
    virtual contracts::Status set_enabled(const Amd3dTarget& t, bool on) = 0;
};

class Amd3dAdapter final : public recovery::SettingAdapter {
public:
    explicit Amd3dAdapter(Amd3dApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

    // Capability sorgusu (registry'ye kaydedilir): destek / desteklenmiyor / izin / bilinmiyor.
    contracts::Capability capability(const Amd3dTarget& t, std::string provider_version);

private:
    Amd3dApi& api_;
};

class FakeAmd3d final : public Amd3dApi {
public:
    contracts::Result<bool> is_supported(const Amd3dTarget& t) override;
    contracts::Result<bool> is_enabled(const Amd3dTarget& t) override;
    contracts::Status set_enabled(const Amd3dTarget& t, bool on) override;
    std::map<int, bool> supported;  // feature -> destek
    std::map<int, bool> enabled;
    bool deny_writes = false;
};

// CPU izleme (Ryzen Master Monitoring SDK) durumu: lisans kabulu olmadan Unsupported; yazma yolu yok.
contracts::Capability amd_cpu_monitoring_capability();

}  // namespace ef::core::adapters::amd
