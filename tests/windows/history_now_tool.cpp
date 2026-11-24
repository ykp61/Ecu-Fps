// REC-01/02 salt okuma araci (ctest disi): bu PC'deki gercek journal kayitlarini Islem Gecmisi sayfasinin
// kullandigi build_history ile ozetler. Pencere acmaz, journal'a yazmaz, ayar degistirmez.
#include <cstdio>

#include "app/adapter_set.h"
#include "core/recovery/history_view.h"
#include "platform/windows/recovery_host.h"

int main() {
    ef::app::AdapterSet adapters;
    auto host = ef::platform::open_recovery_host(adapters.registry);
    if (!host) {
        std::printf("history_now: journal acilamadi\n");
        return 3;
    }
    std::vector<std::pair<std::string, ef::core::recovery::JournalStore::Listing>> ls;
    for (const auto& sc : host->scopes())
        if (sc.store) ls.emplace_back(sc.name, sc.store->scan());
    const auto v = ef::core::recovery::build_history(ls);
    std::printf("history_now: scopes=%zu rows=%zu total=%zu corrupt=%zu foreign=%zu newer=%zu\n", ls.size(), v.rows.size(), v.total,
                v.corrupt, v.foreign, v.newer);
    for (const auto& r : v.rows) {
        std::printf("  %s %lld status=%d steps=%zu", r.scope.c_str(), static_cast<long long>(r.created_unix_ms), static_cast<int>(r.status),
                    r.steps.size());
        if (!r.steps.empty())
            std::printf(" first=%s old=%s written=%s", std::string(ef::contracts::to_string(r.steps[0].op)).c_str(),
                        r.steps[0].old_value.substr(0, 40).c_str(), r.steps[0].written.substr(0, 40).c_str());
        std::printf("\n");
    }
    return 0;
}
