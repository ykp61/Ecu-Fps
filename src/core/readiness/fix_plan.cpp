#include "core/readiness/fix_plan.h"

#include <algorithm>

#include "core/adapters/windows/display_process.h"
#include "core/maintenance/image_health.h"
#include "core/maintenance/standby_purge.h"
#include "core/perf/admin_plan.h"

namespace ef::core::readiness {

namespace {
contracts::SettingOperation gaming_op(std::string target, std::string value) {
    return {contracts::OperationId::GamingToggleSet, contracts::OperationScope::CurrentUser, std::move(target), std::move(value), false, false};
}
}  // namespace

std::optional<FixItem> auto_fix_item(const Check& c) {
    if (c.action != ActionKind::AutoFix) return std::nullopt;
    FixItem it;
    it.check_id = c.id;
    it.action_arg = c.action_arg;
    const auto& k = c.action_arg;
    if (k == "fix.game_mode") {
        it.ops.push_back(gaming_op("gaming:game_mode", "1"));
    } else if (k == "fix.game_dvr") {
        // F-73: arka plan kaydinin uc anahtari tek islemde kapanir; "Geri al" hepsini birlikte geri alir.
        it.ops.push_back(gaming_op("gaming:dvr_background", "0"));
        it.ops.push_back(gaming_op("gaming:app_capture", "0"));
        it.ops.push_back(gaming_op("gaming:dvr_enabled", "0"));
    } else if (k == "fix.windowed_opt") {
        it.ops.push_back(gaming_op("gaming:windowed_opt", "1"));
    } else if (k == "fix.windows_vrr") {
        it.ops.push_back(gaming_op("gaming:windows_vrr", "1"));
    } else if (k == "fix.refresh_rate") {
        const auto idx = c.args.find("index");
        const auto mx = c.args.find("max");
        const auto dev = c.args.find("device");  // GDI aygit adi zorunlu: sira numarasi baska ekrani hedefleyebilir
        if (idx == c.args.end() || mx == c.args.end() || dev == c.args.end() || idx->second.empty() || mx->second.empty()) return std::nullopt;
        std::uint32_t n = 0;
        for (const char ch : idx->second) {
            if (ch < '0' || ch > '9' || n > 9) return std::nullopt;  // en fazla 2 hane (hedef bicimi)
            n = n * 10 + static_cast<std::uint32_t>(ch - '0');
        }
        auto target = adapters::windows::format_display_target(n, dev->second);
        if (target.empty()) return std::nullopt;
        it.ops.push_back({contracts::OperationId::RefreshRateSet, contracts::OperationScope::CurrentUser, std::move(target), mx->second, false, false});
        it.display = true;
    } else if (k == "fix.hags") {
        it.ops.push_back(perf::hags_on_operation());
        it.admin = true;
        it.restart = true;
    } else if (k == "fix.fast_startup") {
        it.ops.push_back(perf::fast_startup_off_operation());
        it.admin = true;
    } else {
        return std::nullopt;  // fix.power, fix.dns (secim), fix.restore_point, fix.image_check: toplu duzeltme degil
    }
    return it;
}

bool is_auto_fixable(const Check& c) { return auto_fix_item(c).has_value(); }

std::optional<contracts::SettingOperation> maintenance_job_operation(const Check& c) {
    if (c.action != ActionKind::AutoFix || c.action_arg != kStandbyFixArg) return std::nullopt;
    return contracts::SettingOperation{contracts::OperationId::MaintenanceJob, contracts::OperationScope::Machine,
                                       maintenance::kStandbyPurgeTarget, maintenance::kMaintenanceValue, false, false};
}

bool row_action_shown(const Check& c) {
    switch (c.action) {
        case ActionKind::None:
        case ActionKind::OpenSettings:
        case ActionKind::OpenUrl: return false;  // yonlendirme: dugme degil
        case ActionKind::AutoFix:
            // Gercekten yapilan isler: otomatik duzeltme, DNS (kullanici hazir listeden secer), istege bagli Evet/Hayir isleri.
            return is_auto_fixable(c) || c.action_arg == "fix.dns" || c.action_arg == "fix.restore_point" || c.action_arg == "fix.image_check" ||
                   maintenance_job_operation(c).has_value();
        case ActionKind::RunNetworkTest:
        case ActionKind::IncidentWindow:
        case ActionKind::MeasureMouse:
        case ActionKind::RunDnsBench:
        case ActionKind::RunHealthTest: break;  // uygulamanin kendi calistirdigi is
    }
    return true;
}

std::size_t FixAllPlan::user_count() const {
    return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [](const FixItem& i) { return !i.admin; }));
}
std::size_t FixAllPlan::admin_count() const {
    return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [](const FixItem& i) { return i.admin; }));
}
bool FixAllPlan::has_display() const {
    return std::any_of(items.begin(), items.end(), [](const FixItem& i) { return i.display; });
}
bool FixAllPlan::has_restart() const {
    return std::any_of(items.begin(), items.end(), [](const FixItem& i) { return i.restart; });
}

FixAllPlan plan_fix_all(const std::vector<Check>& checks, const std::vector<UndoState>& undo) {
    FixAllPlan p;
    for (std::size_t k = 0; k < checks.size(); ++k) {
        const auto& c = checks[k];
        if (c.level != Level::Advice && c.level != Level::Problem) continue;
        if (k < undo.size() && undo[k] != UndoState::None) continue;  // hedefte geri alinmamis kayit: yeni yazma reddedilir
        if (auto it = auto_fix_item(c)) p.items.push_back(std::move(*it));
    }
    return p;
}

contracts::Result<recovery::JournalTx> apply_fix_item(recovery::TransactionEngine& engine, const FixItem& item,
                                                      const std::function<recovery::JournalTx()>& new_tx) {
    recovery::JournalTx tx = new_tx();
    for (const auto& op : item.ops) {
        recovery::JournalStep st;
        st.op = op;
        tx.steps.push_back(std::move(st));
    }
    return engine.apply(std::move(tx));
}

FixAllNotice fix_all_notice(int applied, int failed, bool restart, std::string_view admin_key) {
    // Yonetici adimi basarisizsa nedeni (UAC reddi, kayit engeli, bilinmiyor ...) hicbir ayar duzelmediyse oldugu gibi soylenir.
    const bool admin_failed = !admin_key.empty() && admin_key != "chk.notice.fix_reboot";
    if (applied <= 0) return {admin_failed ? std::string(admin_key) : std::string("chk.notice.fix_failed"), true};
    if (failed > 0) return {"chk.notice.fix_all_partial", true};
    return {restart ? "chk.notice.fix_all_reboot" : "chk.notice.fix_all_done", false};
}

}  // namespace ef::core::readiness
