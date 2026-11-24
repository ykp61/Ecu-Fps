#include "core/perf/profile.h"

namespace ef::core::perf {

std::string_view to_string(ProfileStatus s) noexcept {
    switch (s) {
        case ProfileStatus::Valid: return "valid";
        case ProfileStatus::GameUpdated: return "game_updated";
        case ProfileStatus::DriverUpdated: return "driver_updated";
        case ProfileStatus::NoProfile: return "no_profile";
    }
    return "no_profile";
}

ProfileStatus profile_status(const std::optional<GameProfile>& p, const std::string& game_version, const std::string& driver_version) {
    if (!p) return ProfileStatus::NoProfile;
    if (p->game_version.empty() || game_version.empty() || p->game_version != game_version) return ProfileStatus::GameUpdated;
    if (p->driver_version.empty() || driver_version.empty() || p->driver_version != driver_version) return ProfileStatus::DriverUpdated;
    return ProfileStatus::Valid;
}

std::map<std::string, bool> effective_groups(const std::optional<GameProfile>& p, ProfileStatus st, const std::map<std::string, bool>& general) {
    if (!p || st != ProfileStatus::Valid) return general;  // yeniden dogrulanana kadar genel secim
    auto g = general;
    for (const auto& [k, v] : p->groups) g[k] = v;
    return g;
}

GameProfile update_profile(const GameProfile& old, const std::map<std::string, bool>& groups, const std::string& game_version,
                           const std::string& driver_version) {
    GameProfile p = old;
    ++p.revision;
    p.groups = groups;
    p.game_version = game_version;
    p.driver_version = driver_version;
    p.measured = false;
    return p;
}

DriftAction DriftGuard::observe(const std::string& target, const std::string& ours, const std::string& observed) {
    if (observed == ours) return DriftAction::None;
    const int n = ++conflicts_[target];
    return n >= 2 ? DriftAction::Ceasefire : DriftAction::WarnOnce;
}

bool DriftGuard::ceasefire(const std::string& target) const {
    const auto it = conflicts_.find(target);
    return it != conflicts_.end() && it->second >= 2;
}

std::vector<std::string> DriftGuard::ceasefire_targets() const {
    std::vector<std::string> v;
    for (const auto& [k, n] : conflicts_)
        if (n >= 2) v.push_back(k);
    return v;
}

}  // namespace ef::core::perf
