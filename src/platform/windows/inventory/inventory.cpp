#include "platform/windows/inventory/inventory.h"

#include "platform/windows/inventory/cpu_mem.h"
#include "platform/windows/inventory/gpu_storage.h"
#include "platform/windows/inventory/health_win.h"
#include "platform/windows/inventory/windows_features.h"
#include "platform/windows/inventory/update_status.h"
#include "platform/windows/inventory/driver_info.h"
#include "core/inventory/disk_space.h"
#include "platform/windows/games/library_scan.h"
#include "platform/windows/inventory/field_json.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/inventory/power_security_win.h"

namespace ef::platform::inventory {

core::json::Value collect_all_json() {
    auto o = core::json::Value::object();
    o.set("schema_version", core::json::Value::integer(1));
    o.set("os", to_json(read_os()));
    o.set("user", to_json(read_user()));
    o.set("power", to_json(read_power()));
    o.set("policy", to_json(read_policy()));
    o.set("cpu", to_json(read_cpu()));
    o.set("memory", to_json(read_memory()));
    o.set("board", to_json(read_board()));
    o.set("power_security", to_json(read_power_security()));
    o.set("gpus", gpus_json(read_gpus()));
    o.set("disks", disks_json(read_disks()));
    o.set("volumes", volumes_json(read_volumes()));
    o.set("health", health_json());
    o.set("windows_features", windows_features_json());
    o.set("windows_update", update_status_json());
    o.set("update_check", update_check_json());
    {
        // DIAG-08: birim doluluk + kurulu oyunlara gore bos alan onerisi
        const auto games = games::scan_installed_games();
        auto arr = core::json::Value::array();
        for (const auto& a : core::inventory::advise_volumes(read_volumes(), read_disks(), games)) {
            auto e = core::json::Value::object();
            e.set("root", core::json::Value::string(a.root));
            e.set("status", core::json::Value::string(std::string(core::inventory::to_string(a.status))));
            e.set("free_gib", a.free_gib ? core::json::Value::integer(static_cast<std::int64_t>(*a.free_gib)) : core::json::Value::null());
            e.set("total_gib", a.total_gib ? core::json::Value::integer(static_cast<std::int64_t>(*a.total_gib)) : core::json::Value::null());
            e.set("games", core::json::Value::integer(static_cast<std::int64_t>(a.games)));
            e.set("largest_game", core::json::Value::string(a.largest_game));
            e.set("largest_game_gib", core::json::Value::integer(static_cast<std::int64_t>(a.largest_game_gib)));
            e.set("recommended_free_gib", a.recommended_free_gib ? core::json::Value::integer(static_cast<std::int64_t>(*a.recommended_free_gib)) : core::json::Value::null());
            e.set("games_on_hdd", core::json::Value::boolean(a.games_on_hdd));
            arr.push(std::move(e));
        }
        o.set("storage", std::move(arr));
        o.set("installed_games", core::json::Value::integer(static_cast<std::int64_t>(games.size())));
    }
    const auto vm = virtual_machine();
    o.set("virtual_machine", vm ? core::json::Value::boolean(*vm) : core::json::Value::null());
    return o;
}

}  // namespace ef::platform::inventory
