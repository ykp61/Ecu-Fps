#pragma once
// GPU-04: Intel Graphics Control Library 3D ayarlari icin donanimdan bagimsiz sozlesme. Gercek ControlLib baglayicisi
// ve urun plani YOKTUR: Intel GPU'da dogrulanana kadar hicbir kullanici akisina baglanmaz (B-004). Sahte API ile
// sinanan yalniz sozlesme, hata esleme ve geri alma kurallaridir; gercek donanim testi sayilmaz.
// Kurallar: desteklenmeyen ozellik yazilmaz (kontrollu Unsupported); yetki hatasi Denied, yonetici hakki API
// yoklugunu gidermez; geri alma yakalanan eski degeri yazar. CPU/RAM/voltaj/fan yazimi YOKTUR.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "contracts/support.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::intel {

enum class Igcl3dFeature { LowLatency, EnduranceGaming };
std::string_view to_string(Igcl3dFeature f) noexcept;

// Hedef: "igcl:gpu<index>:low_latency" | ":endurance_gaming"
struct Igcl3dTarget {
    std::uint32_t gpu = 0;
    Igcl3dFeature feature = Igcl3dFeature::LowLatency;
};
contracts::Result<Igcl3dTarget> parse_igcl_target(std::string_view s);

// Baglayicinin IGCL ctl_result_t degerlerini indirgedigi siniflar (sayisal kodlar burada varsayilmaz).
enum class IgclResult { Success, Unsupported, InsufficientPermissions, DeviceLost, Other };
contracts::Status igcl_status(IgclResult r, std::string_view what);

class Igcl3dApi {
public:
    virtual ~Igcl3dApi() = default;
    virtual IgclResult is_supported(const Igcl3dTarget& t, bool& out) = 0;
    virtual IgclResult is_enabled(const Igcl3dTarget& t, bool& out) = 0;
    virtual IgclResult set_enabled(const Igcl3dTarget& t, bool on) = 0;
};

class Igcl3dAdapter final : public recovery::SettingAdapter {
public:
    explicit Igcl3dAdapter(Igcl3dApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;
    contracts::Capability capability(const Igcl3dTarget& t, std::string provider_version);

private:
    Igcl3dApi& api_;
};

class FakeIgcl3d final : public Igcl3dApi {
public:
    IgclResult is_supported(const Igcl3dTarget& t, bool& out) override;
    IgclResult is_enabled(const Igcl3dTarget& t, bool& out) override;
    IgclResult set_enabled(const Igcl3dTarget& t, bool on) override;
    std::map<int, bool> supported, enabled;
    IgclResult support_result = IgclResult::Success;
    IgclResult write_result = IgclResult::Success;
    int writes = 0;
};

}  // namespace ef::core::adapters::intel
