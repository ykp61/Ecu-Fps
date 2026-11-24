#include "core/perf/power_plan.h"

#include <algorithm>

namespace ef::core::perf {

namespace cw = adapters::windows;
using contracts::OperationId;
using contracts::OperationScope;
using contracts::SettingOperation;

std::vector<PlanItem> power_plan_items(cw::PowerApi& api) {
    std::vector<PlanItem> out;
    const auto scheme = api.get_active_scheme();
    if (!scheme.ok()) return out;
    if (scheme.value() == cw::kSchemeBalanced) {
        const auto mode = api.get_ac_mode();
        if (mode.ok()) {
            // Gap 11: pilli cihazda (dizustu/el konsolu) DC modu da ayri, journal'li bir adimdir.
            // Yalniz Unsupported "pil modu degistirilemiyor" iddiasini destekler; diger okuma hatalari
            // veya bilinmeyen GUID "pil modu okunamadi" olarak ayri gosterilir (desteklenmiyor != veri yok).
            const auto battery = api.has_battery();
            const bool has_battery = battery.ok() && battery.value();
            const auto dc = has_battery ? api.get_dc_mode() : contracts::Result<std::string>::fail(contracts::ErrorCode::Unsupported, "no battery");
            const bool dc_ok = has_battery && dc.ok() && cw::is_known_power_mode(dc.value());
            const bool dc_unsupported = has_battery && !dc.ok() && dc.code() == contracts::ErrorCode::Unsupported;
            if (mode.value() != cw::kModeBestPerformance) {
                PlanItem i;
                // Guc modu yonetici gerektirmez (kullanici yapilandirmasi); etkisi tum makinede.
                i.op = SettingOperation{OperationId::PowerModeSet, OperationScope::Machine, std::string(cw::kTargetPowerModeAc),
                                        std::string(cw::kModeBestPerformance), false, false};
                i.title_key = "plan.power_mode";
                if (has_battery)
                    i.field_key = dc_ok            ? "plan.field.power_mode_ac"
                                  : dc_unsupported ? "plan.field.power_mode_ac_only"
                                                   : "plan.field.power_mode_ac_dc_unknown";
                out.push_back(std::move(i));
            }
            if (dc_ok && dc.value() != cw::kModeBestPerformance) {
                PlanItem i;
                i.op = SettingOperation{OperationId::PowerModeSet, OperationScope::Machine, std::string(cw::kTargetPowerModeDc),
                                        std::string(cw::kModeBestPerformance), false, false};
                i.title_key = "plan.power_mode";
                i.field_key = "plan.field.power_mode_dc";
                out.push_back(std::move(i));
            }
            return out;  // bos: zaten en iyi performans
        }
    }
    if (scheme.value() == kSchemeHighPerformance) return out;
    const auto list = api.list_schemes();
    if (!list.ok()) return out;
    if (std::find(list.value().begin(), list.value().end(), std::string(kSchemeHighPerformance)) == list.value().end())
        return out;  // plan yok: olusturulmaz
    // Kullanicinin ozel/ust duzey plani (Nihai performans vb.) varsa dokunulmaz: yalniz bilinen
    // Windows planlarindan (Dengeli, Guc tasarrufu) yukseltilir.
    static constexpr std::string_view kPowerSaver = "a1841308-3541-4fab-bc81-f71556f20b4a";
    if (scheme.value() != cw::kSchemeBalanced && scheme.value() != kPowerSaver) return out;
    PlanItem i;
    i.op = SettingOperation{OperationId::PowerSchemeSet, OperationScope::Machine, "power:scheme:active",
                            std::string(kSchemeHighPerformance), false, false};
    i.title_key = "plan.power_scheme";
    out.push_back(std::move(i));
    return out;
}

}  // namespace ef::core::perf
