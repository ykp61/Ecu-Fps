#include "core/perf/admin_plan.h"

#include <algorithm>

#include "core/adapters/windows/dns.h"
#include "core/broker/admin.h"

namespace ef::core::perf {

namespace nv = adapters::nvidia;

contracts::SettingOperation hags_on_operation() {
    return contracts::SettingOperation{contracts::OperationId::HagsSet, contracts::OperationScope::Machine,
                                       std::string(adapters::windows::kHagsTarget), "2", true, false};
}

contracts::SettingOperation fast_startup_off_operation() {
    return contracts::SettingOperation{contracts::OperationId::FastStartupSet, contracts::OperationScope::Machine,
                                       std::string(adapters::windows::kFastStartupTarget), "0", false, false};
}

bool admin_fix_target(std::string_view target) noexcept {
    return target == adapters::windows::kHagsTarget || target == adapters::windows::kFastStartupTarget ||
           adapters::windows::parse_dns_target(target).has_value();
}

std::vector<PlanItem> hags_plan_items(adapters::windows::HagsApi& api) {
    std::vector<PlanItem> out;
    if (api.supported() != std::optional<bool>(true)) return out;  // destek dogrulanmadan yazma onerilmez
    const auto cur = api.read_pref();
    if (!cur.ok()) return out;
    if (cur.value() == std::optional<std::uint32_t>(adapters::windows::kHagsOn)) return out;  // zaten acik
    PlanItem i;
    i.op = hags_on_operation();
    i.title_key = "plan.hags";
    i.group_key = "plan.hags";
    i.needs_admin = true;
    out.push_back(std::move(i));
    return out;
}

std::vector<PlanItem> power_subs_plan_items(adapters::windows::PowerSubApi& api, bool scheme_change_planned) {
    std::vector<PlanItem> out;
    if (scheme_change_planned) return out;  // anlik goruntu/yazma plani ayni olmali (bkz. baslik)
    struct Want {
        std::string_view target;
        std::string_view group;
        std::uint32_t desired;
    };
    const auto wants = {
        // Boost: yalniz "Efficient*" (3/4) performans yonunde "Enabled"a cekilir; 0/1/2/5/6 ezilmez.
        Want{adapters::windows::kPowerBoostAcTarget, "plan.power_cpu", adapters::windows::kPowerBoostEnabled},
        Want{adapters::windows::kPowerParkingAcTarget, "plan.power_cpu", adapters::windows::kPowerParkingDisabled},
        Want{adapters::windows::kPowerAspmAcTarget, "plan.power_pcie", adapters::windows::kPowerAspmOff},
        // B1: USB secici askiya alma, yalniz AC; hedeften farkliysa (Enabled=1 -> 0). Varsayilan Hayir (controller.cpp).
        Want{adapters::windows::kPowerUsbSelectiveSuspendAcTarget, "plan.power_usb", adapters::windows::kPowerUsbSuspendDisabled},
    };
    for (const auto& w : wants) {
        auto cur = api.read_active_ac(w.target);
        if (!cur.ok()) continue;  // okunamayan ayar plana girmez (uydurma yok)
        const auto v = cur.value().ac_index;
        bool change = false;
        if (w.target == adapters::windows::kPowerBoostAcTarget) change = v == 3 || v == 4;
        else change = v != w.desired;  // parking ve ASPM: hedeften farkliysa
        if (!change) continue;
        PlanItem i;
        i.op = contracts::SettingOperation{contracts::OperationId::PowerSettingSet, contracts::OperationScope::Machine,
                                           std::string(w.target), std::to_string(w.desired), false, false};
        i.title_key = std::string(w.group);
        i.group_key = std::string(w.group);
        i.needs_admin = true;
        out.push_back(std::move(i));
    }
    return out;
}

std::vector<PlanItem> service_stop_plan_items(adapters::startup::ServiceApi& api,
                                              adapters::windows::ServiceBusyProbe& probe,
                                              const std::vector<std::string>& selected) {
    std::vector<PlanItem> out;
    for (const auto& name : selected) {
        const auto* entry = adapters::windows::find_session_stoppable(name);
        if (!entry) continue;  // izin listesi disi ad plana girmez (broker da reddeder)
        auto s = api.get(std::string(entry->name));
        if (!s.ok() || !s.value().running) continue;  // yok/zaten durmus: yapacak is yok
        // Durdurma onkosullari (Microsoft ikilisi, calisan bagimli, is kuyrugu) saglanmiyorsa plana girmez.
        if (!adapters::windows::check_session_stop(api, probe, s.value()).ok()) continue;
        PlanItem i;
        i.op = contracts::SettingOperation{contracts::OperationId::ServiceSessionStop, contracts::OperationScope::Machine,
                                           adapters::windows::format_session_stop_target(entry->name), "stopped", false, false};
        i.title_key = "plan.services";
        i.group_key = "plan.services";
        i.needs_admin = true;
        out.push_back(std::move(i));
    }
    return out;
}

NvidiaGate nvidia_adapter_gate(const std::vector<inventory::GpuAdapter>& gpus, std::optional<std::uint32_t> target_adapter_index,
                               bool nvapi_runtime_loaded) {
    using inventory::GpuKind;
    using vendors::Vendor;
    NvidiaGate g;
    const auto fail = [&g](GpuGateState st, const char* key) {
        g.state = st;
        g.reason_key = key;
        g.adapter_index.reset();
        return g;
    };
    const auto is_hw = [](const inventory::GpuAdapter& a) { return a.kind != GpuKind::Software && a.kind != GpuKind::Remote; };
    std::size_t hw = 0;
    bool vendor_unread = false;
    bool any_nvidia = false;
    const inventory::GpuAdapter* only_hw = nullptr;
    for (const auto& a : gpus) {
        if (!is_hw(a)) continue;
        ++hw;
        only_hw = &a;
        if (!a.vendor_id.has()) vendor_unread = true;
        else if (vendors::vendor_of(*a.vendor_id.value) == Vendor::Nvidia) any_nvidia = true;
    }
    if (gpus.empty()) return fail(GpuGateState::Insufficient, "plan.nvidia.gate.adapters_unread");
    if (hw == 0) return fail(GpuGateState::Unsupported, "plan.nvidia.gate.no_hardware_gpu");
    if (!any_nvidia)
        return vendor_unread ? fail(GpuGateState::Insufficient, "plan.nvidia.gate.vendor_unread")
                             : fail(GpuGateState::Unsupported, "plan.nvidia.gate.no_nvidia_adapter");
    // Hedef: bilinen indeks, yoksa tek donanim GPU'su. Birden cok donanim GPU'su + bilinmeyen hedef = dogrulanamadi.
    std::uint32_t target = 0;
    if (target_adapter_index) target = *target_adapter_index;
    else if (hw == 1 && only_hw) target = only_hw->index;
    else return fail(GpuGateState::Insufficient, "plan.nvidia.gate.hybrid_target_unknown");
    const auto it = std::find_if(gpus.begin(), gpus.end(), [&](const inventory::GpuAdapter& a) { return a.index == target; });
    if (it == gpus.end()) return fail(GpuGateState::Insufficient, "plan.nvidia.gate.target_not_found");
    if (!is_hw(*it)) return fail(GpuGateState::Unsupported, "plan.nvidia.gate.target_not_hardware");
    if (!it->vendor_id.has()) return fail(GpuGateState::Insufficient, "plan.nvidia.gate.vendor_unread");
    if (vendors::adapter_vendor(gpus, target) != Vendor::Nvidia) return fail(GpuGateState::Unsupported, "plan.nvidia.gate.target_not_nvidia");
    // Uretici x ozellik kapisi (fail-closed): rota yalniz NVAPI yuklu ise Supported; yazma satiri tablodan gelir.
    vendors::RuntimeInfo rt;
    rt.vendor = Vendor::Nvidia;
    rt.state = nvapi_runtime_loaded ? vendors::RuntimeState::Loaded : vendors::RuntimeState::Missing;
    const auto routes = vendors::route_adapters({*it}, {rt});
    if (routes.empty()) return fail(GpuGateState::Insufficient, "plan.nvidia.gate.target_not_found");
    if (routes[0].support != contracts::SupportState::Supported) return fail(GpuGateState::Unsupported, "plan.nvidia.gate.driver_runtime");
    for (const auto setting : {nv::DrsSetting::PowerMode, nv::DrsSetting::MaxPrerendered})
        if (!vendors::vendor_driver_write_allowed(routes[0], nv::catalog_id(setting)))
            return fail(GpuGateState::Unsupported, "plan.nvidia.gate.write_not_supported");
    g.state = GpuGateState::Allowed;
    g.reason_key.clear();
    g.adapter_index = target;
    return g;
}

std::optional<PlanOmission> nvidia_omission(const NvidiaGate& g) {
    if (g.allowed()) return std::nullopt;
    return PlanOmission{"plan.nvidia", g.state == GpuGateState::Unsupported ? OmissionKind::Unsupported : OmissionKind::Insufficient,
                        g.reason_key};
}

std::vector<PlanItem> nvidia_plan_items(nv::DrsApi* drs, const NvidiaGate& gate) {
    return nvidia_plan_items(drs, gate, NvidiaExtraFlags{broker::kThreadedOptimizationItemEnabled, broker::kFxaaOffItemEnabled});
}

std::vector<PlanItem> nvidia_plan_items(nv::DrsApi* drs, const NvidiaGate& gate, const NvidiaExtraFlags& extra) {
    std::vector<PlanItem> out;
    if (!drs) return out;
    if (!gate.allowed()) return out;  // A3: hedef adaptor dogrulanmadan NVIDIA parcasi plana girmez
    struct Want {
        nv::DrsSetting setting;
        std::uint32_t value;
        bool at_least;  // true: mevcut >= hedef ise dokunma (shader onbellegi asla kucultulmez)
        const char* field;  // EF-X25 T03: onayda gorunen alan
    };
    constexpr Want kWants[] = {
        {nv::DrsSetting::PowerMode, 1, false, "plan.field.nv_power"},
        {nv::DrsSetting::MaxPrerendered, 1, false, "plan.field.nv_queue"},
        {nv::DrsSetting::ShaderCacheSize, broker::kShaderCacheTargetMb, true, "plan.field.nv_shader"},
    };
    for (const auto& w : kWants) {
        // M3: shader onbellek ogesi, birimi ve 0 anlami resmi NVIDIA belgesiyle dogrulanana kadar kapali
        // (broker::kShaderCacheItemEnabled; backlog OPT-10 notu). Kod korunur.
        if (w.setting == nv::DrsSetting::ShaderCacheSize && !broker::kShaderCacheItemEnabled) continue;
        nv::DrsTarget t;
        t.global = true;
        t.setting = static_cast<std::uint32_t>(w.setting);
        const auto cur = drs->get(t);
        if (!cur.ok()) continue;  // okunamayan ayar plana girmez
        const auto v = cur.value().value;
        if (w.at_least ? v >= w.value : v == w.value) continue;
        // 0'in anlami (kapali/surucu varsayilani) resmi belgede tanimli degil: dokunulmaz.
        if (w.at_least && v == 0) continue;
        PlanItem i;
        i.op = contracts::SettingOperation{contracts::OperationId::VendorProfileValue, contracts::OperationScope::Machine,
                                           nv::format_drs_target(t), nv::DrsAdapter::encode(w.value), false, false};
        i.title_key = "plan.nvidia";
        i.group_key = "plan.nvidia";
        i.field_key = w.field;
        i.needs_admin = true;
        out.push_back(std::move(i));
    }
    // B6 (ADR-040 m.8): Gelismis grup, varsayilan Hayir (controller.cpp), ayar basina kapali bayrak. Ayni kapi yukarida.
    struct Extra {
        nv::DrsSetting setting;
        std::uint32_t value;
        bool enabled;
        bool only_from_on;  // FXAA: yalniz mevcut deger 1 (acik) ise kapat; baska/bilinmeyen deger dokunulmaz
        const char* field;
    };
    const Extra kExtras[] = {
        {nv::DrsSetting::ThreadedOptimization, broker::kThreadedOptimizationTarget, extra.threaded_optimization, false,
         "plan.field.nv_threaded"},
        {nv::DrsSetting::FxaaEnable, broker::kFxaaOffTarget, extra.fxaa_off, true, "plan.field.nv_fxaa"},
    };
    for (const auto& e : kExtras) {
        if (!e.enabled) continue;
        nv::DrsTarget t;
        t.global = true;
        t.setting = static_cast<std::uint32_t>(e.setting);
        const auto cur = drs->get(t);
        if (!cur.ok()) continue;  // okunamayan ayar plana girmez
        const auto v = cur.value().value;
        if (v == e.value) continue;
        if (e.only_from_on && v != 1) continue;
        PlanItem i;
        i.op = contracts::SettingOperation{contracts::OperationId::VendorProfileValue, contracts::OperationScope::Machine,
                                           nv::format_drs_target(t), nv::DrsAdapter::encode(e.value), false, false};
        i.title_key = "plan.nvidia_extra";
        i.group_key = "plan.nvidia_extra";
        i.field_key = e.field;
        i.needs_admin = true;
        out.push_back(std::move(i));
    }
    return out;
}

contracts::SettingOperation restore_point_operation() {
    return {contracts::OperationId::RestorePointCreate, contracts::OperationScope::Machine, recovery::kRestorePointTarget, recovery::kRestorePointValue, false, false};
}

bool is_restore_point_item(const PlanItem& i) noexcept { return i.op.id == contracts::OperationId::RestorePointCreate; }

std::optional<PlanItem> restore_point_plan_item(bool plan_has_admin_items, std::optional<bool> system_restore_enabled,
                                                std::optional<std::int64_t> last_restore_point_age_s) {
    if (!plan_has_admin_items) return std::nullopt;
    if (system_restore_enabled == std::optional<bool>(false)) return std::nullopt;
    if (last_restore_point_age_s && *last_restore_point_age_s >= 0 && *last_restore_point_age_s < kRestorePointMinAgeSeconds) return std::nullopt;
    PlanItem i;
    i.op = restore_point_operation();
    i.title_key = "plan.restore_point";
    i.group_key = "plan.restore_point";
    i.needs_admin = true;
    return i;
}

void drop_lone_restore_point(std::vector<PlanItem>& items) {
    const bool other_admin = std::any_of(items.begin(), items.end(), [](const PlanItem& i) { return i.needs_admin && !is_restore_point_item(i); });
    if (other_admin) return;
    items.erase(std::remove_if(items.begin(), items.end(), is_restore_point_item), items.end());
}

void restore_point_first(std::vector<PlanItem>& admin_items) {
    std::stable_partition(admin_items.begin(), admin_items.end(), is_restore_point_item);
}

std::vector<contracts::SettingOperation> nvidia_game_profile_ops(const std::string& exe_name) {
    namespace nv = core::adapters::nvidia;
    std::vector<contracts::SettingOperation> out;
    for (const auto setting : {nv::DrsSetting::PowerMode, nv::DrsSetting::MaxPrerendered}) {
        nv::DrsTarget t;
        t.global = false;
        t.app = exe_name;
        t.setting = static_cast<std::uint32_t>(setting);
        out.push_back({contracts::OperationId::VendorProfileValue, contracts::OperationScope::Machine, nv::format_drs_target(t),
                       nv::DrsAdapter::encode(1), false, false});
    }
    return out;
}

GameProfileRecord game_profile_record(const std::string& exe_name, const std::vector<std::string>& active,
                                      const std::vector<std::string>& recovery, const std::vector<std::string>& interrupted,
                                      const std::vector<std::string>& conflict) {
    const auto listed = [](const std::vector<std::string>& v, const std::string& t) { return std::find(v.begin(), v.end(), t) != v.end(); };
    const auto ops = nvidia_game_profile_ops(exe_name);
    for (const auto& op : ops)
        if (listed(conflict, op.target)) return GameProfileRecord::Conflict;
    std::size_t in_active = 0;
    for (const auto& op : ops) {
        if (listed(recovery, op.target) || listed(interrupted, op.target)) return GameProfileRecord::Attention;
        if (listed(active, op.target)) ++in_active;
    }
    if (in_active == 0) return GameProfileRecord::None;
    return in_active == ops.size() ? GameProfileRecord::Active : GameProfileRecord::Partial;
}

}  // namespace ef::core::perf
