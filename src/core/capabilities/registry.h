#pragma once
// Capability registry (INV-04): ozellik x cihaz destek durumu, ortam parmak izi ile onbellek,
// gecersiz kilma olaylari ve release kapisi.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "contracts/support.h"
#include "core/serialization/json.h"

namespace ef::core::capabilities {

// ---- Ayar katalogu (research/setting_catalog.json, derleme zamaninda gomulu) ----
enum class FeatureGroup { Windows, Gpu, Game, Hardware, Startup, Excluded };
std::string_view to_string(FeatureGroup g) noexcept;

struct FeatureDef {
    std::string id;     // "W01"
    FeatureGroup group = FeatureGroup::Windows;
    std::string name;   // Turkce ad
    bool catalog_release_enabled = false;
};

contracts::Result<std::vector<FeatureDef>> parse_feature_catalog(std::string_view json_text);
const std::vector<FeatureDef>& embedded_features();  // bozuksa bos (test yakalar)
std::string_view embedded_catalog_json() noexcept;

// ---- Ortam parmak izi: degisirse onceki dogrulamalar bayatlar ----
struct Environment {
    std::string os_build;                      // "26300.9711"
    std::vector<std::string> gpu_drivers;      // "10DE:2C02=32.0.16.2101"
    std::string app_version;                   // "0.1.0"
};
std::string fingerprint(const Environment& e);

enum class InvalidationEvent { DriverChanged, OsUpdated, DeviceChanged, AppUpdated, UserRequested };
std::string_view to_string(InvalidationEvent e) noexcept;

struct Entry {
    contracts::Capability cap;
    std::string env_fingerprint;  // dogrulandigi ortam
};

class Registry {
public:
    explicit Registry(std::vector<FeatureDef> features) : features_(std::move(features)) {}

    // Saglayici (adaptor) sonucu kaydeder. Bilinmeyen feature_id reddedilir.
    contracts::Status record(const contracts::Capability& cap, const std::string& env_fp);
    void add_evidence(const contracts::SupportEvidence& e) { evidence_.push_back(e); }

    // Sorgu: kayit yoksa Unknown("not queried"); ortam degistiyse Unknown("stale ...");
    // Excluded grubu daima Unsupported("excluded by policy").
    contracts::Capability query(std::string_view feature_id, std::string_view device_scope,
                                const std::string& current_env_fp) const;

    // Yalniz gercek cihaz kaniti + katalog ve kanit kapisi gecerse true. Excluded asla.
    bool release_enabled(std::string_view feature_id) const;

    // Olay: ilgili kayitlarin parmak izi silinir (bir sonraki sorgu bayat sayar).
    std::size_t invalidate(InvalidationEvent ev);

    const std::vector<FeatureDef>& features() const noexcept { return features_; }
    const FeatureDef* feature(std::string_view id) const noexcept;

    // Destek matrisi: her ozellik icin durum ve release kapisi.
    json::Value support_matrix(const std::string& current_env_fp) const;

    // Onbellek kaliciligi (journal disi, salt yerel veri)
    json::Value save() const;
    contracts::Status load(const json::Value& v);

private:
    std::vector<FeatureDef> features_;
    std::map<std::string, Entry> entries_;  // anahtar: feature_id + "|" + device_scope
    std::vector<contracts::SupportEvidence> evidence_;
};

}  // namespace ef::core::capabilities
