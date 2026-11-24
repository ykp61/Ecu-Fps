#include "core/games/adapter.h"

#include <algorithm>

namespace ef::core::games {

GameIdentity identity_from(const inventory::GameInstall& g) {
    GameIdentity id;
    id.store = g.launcher;
    id.app_id = g.app_id;
    id.name = g.name;
    id.install_dir = g.install_dir;
    id.version = g.version;
    return id;
}

std::string_view to_string(Invalidation i) noexcept {
    switch (i) {
        case Invalidation::None: return "none";
        case Invalidation::VersionChanged: return "version_changed";
        case Invalidation::ExeChanged: return "exe_changed";
        case Invalidation::VersionUnknown: return "version_unknown";
        case Invalidation::CloudSyncActive: return "cloud_sync_active";
        case Invalidation::NoAdapter: return "no_adapter";
    }
    return "none";
}

EffectiveCapabilities effective(const GameIdentity& g, const std::vector<AdapterRecord>& registry, bool cloud_sync,
                                std::string_view known_exe_version) {
    EffectiveCapabilities e;  // varsayilan: yalniz gozlem
    const auto it = std::find_if(registry.begin(), registry.end(), [&](const AdapterRecord& r) {
        return !g.app_id.empty() && r.store == g.store && r.app_id == g.app_id;
    });
    if (it == registry.end()) {
        e.reasons.push_back(Invalidation::NoAdapter);
        return e;
    }
    if (g.version.empty()) {
        e.reasons.push_back(Invalidation::VersionUnknown);
        return e;
    }
    if (std::find(it->verified_versions.begin(), it->verified_versions.end(), g.version) == it->verified_versions.end()) {
        e.reasons.push_back(Invalidation::VersionChanged);  // yeni surum: yeniden dogrulanana kadar gozlem
        return e;
    }
    if (!known_exe_version.empty() && g.exe_version != known_exe_version) {
        e.reasons.push_back(Invalidation::ExeChanged);
        return e;
    }
    e.caps = it->capabilities | static_cast<std::uint32_t>(Capability::Observation);
    if (cloud_sync && e.has(Capability::ConfigWrite)) {
        e.caps &= ~static_cast<std::uint32_t>(Capability::ConfigWrite);
        e.reasons.push_back(Invalidation::CloudSyncActive);
    }
    return e;
}

}  // namespace ef::core::games
