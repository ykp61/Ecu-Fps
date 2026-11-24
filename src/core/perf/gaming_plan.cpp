#include "core/perf/gaming_plan.h"

namespace ef::core::perf {

namespace {
struct Want {
    const char* target;
    const char* value;
    const char* group;
    const char* if_missing;  // deger yoksa varsayilan ("1"/"0"); nullptr = bilinmiyor -> yaz
};
constexpr Want kWants[] = {
    {"gaming:game_mode", "1", "plan.gaming", "1"},
    {"gaming:dvr_background", "0", "plan.gaming", "0"},
    // Yakalama anahtarlari ve Game Bar ipucu paneli Windows varsayilani olarak acik; deger yoksa da kapatilir.
    {"gaming:app_capture", "0", "plan.gaming", "1"},
    {"gaming:dvr_enabled", "0", "plan.gaming", "1"},
    {"gaming:gamebar_startup", "0", "plan.gaming", "1"},
    {"gaming:windowed_opt", "1", "plan.windowed", nullptr},
    {"gaming:windows_vrr", "1", "plan.windowed", nullptr},
};
}  // namespace

std::vector<PlanItem> gaming_plan_items(adapters::windows::GamingToggleAdapter& a) {
    std::vector<PlanItem> out;
    for (const auto& w : kWants) {
        const contracts::SettingOperation op{contracts::OperationId::GamingToggleSet, contracts::OperationScope::CurrentUser, w.target, w.value,
                                             false, false};
        const auto cur = a.read_back(op);
        if (!cur.ok()) continue;  // okunamayan hedef plana girmez
        const std::string effective = cur.value() ? *cur.value() : (w.if_missing ? w.if_missing : "");
        if (effective == w.value) continue;  // zaten uygun
        PlanItem i;
        i.op = op;
        i.title_key = w.group;
        i.group_key = w.group;
        out.push_back(std::move(i));
    }
    return out;
}

}  // namespace ef::core::perf
