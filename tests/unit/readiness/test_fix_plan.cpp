// P12-B1: "Duzelt" gercekten duzeltir. Saf kurallar (hangi bulgu otomatik duzeltilebilir, hangisinde dugme yok), toplu
// plan, tek Evet/Hayir kip penceresi ve SAHTE (MOCK) adaptorle uygulama -> geri okuma -> geri alma. Gercek Windows
// ayari bu testlerde hicbir zaman yazilmaz; adaptor bellek ici FakeAdapter'dir.
#include <functional>
#include <string>
#include <vector>

#include "core/broker/admin.h"
#include "core/i18n/catalog.h"
#include "core/readiness/fix_plan.h"
#include "core/recovery/engine.h"
#include "core/recovery/journal.h"
#include "ef_test.h"
#include "ui/consent/fix_dialogs.h"
#include "ui/shell/modal.h"

using namespace ef;
namespace cr = core::readiness;
namespace rec = core::recovery;
using contracts::OperationId;
using contracts::TxState;

namespace {
cr::Check make(const char* id, cr::Level l, cr::ActionKind a, const char* arg) {
    cr::Check c;
    c.id = id;
    c.section = cr::Section::Windows;
    c.level = l;
    c.text_key = "k";
    c.action = a;
    c.action_arg = arg;
    return c;
}

// MOCK: bellek ici journal + FakeAdapter; gercek kayit defteri / Windows ayari yok.
struct MockEnv {
    rec::MemoryJournalFs fs;
    rec::JournalStore store{fs, "mock-fixes"};
    rec::FakeAdapter adapter;  // MOCK
    rec::AdapterRegistry reg;
    rec::TransactionEngine engine{store, reg};
    int seq = 0;
    MockEnv() { reg.add(OperationId::GamingToggleSet, &adapter); }
    rec::JournalTx new_tx() {
        rec::JournalTx tx;
        char buf[33];
        std::snprintf(buf, sizeof buf, "%032d", ++seq);
        tx.id = buf;
        tx.actor_sid = "S-MOCK";
        tx.target_sid = "S-MOCK";
        tx.created_unix_ms = 1;
        return tx;
    }
    std::function<rec::JournalTx()> maker() {
        return [this] { return new_tx(); };
    }
};
}  // namespace

// Dugme kurali: yonlendirme dugmesi yok; "Duzelt" adini yalniz gercekten duzelten eylem tasir.
EF_TEST(fix_plan_row_actions_only_for_real_work) {
    EF_CHECK(!cr::row_action_shown(make("a", cr::Level::Info, cr::ActionKind::OpenSettings, "ms-settings:display-advanced")));
    EF_CHECK(!cr::row_action_shown(make("a", cr::Level::Info, cr::ActionKind::OpenUrl, "https://www.nvidia.com/Download/index.aspx")));
    EF_CHECK(!cr::row_action_shown(make("a", cr::Level::Advice, cr::ActionKind::None, "")));
    // Uygulamanin yazamadigi AutoFix (guc plani): dugme yok, otomatik duzeltilebilir de degil.
    const auto power = make("windows.power", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.power");
    EF_CHECK(!cr::row_action_shown(power) && !cr::is_auto_fixable(power));
    EF_CHECK(!cr::row_action_shown(make("a", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.unknown")));
    for (const char* arg : {"fix.game_mode", "fix.game_dvr", "fix.windowed_opt", "fix.windows_vrr", "fix.hags", "fix.fast_startup"}) {
        const auto c = make("a", cr::Level::Advice, cr::ActionKind::AutoFix, arg);
        EF_CHECK(cr::row_action_shown(c) && cr::is_auto_fixable(c));
    }
    // Kullanici secimli / istege bagli Evet/Hayir isleri satir dugmesi olarak kalir ama toplu duzeltmeye girmez.
    for (const char* arg : {"fix.dns", "fix.restore_point", "fix.image_check"}) {
        const auto c = make("a", cr::Level::Info, cr::ActionKind::AutoFix, arg);
        EF_CHECK(cr::row_action_shown(c) && !cr::is_auto_fixable(c));
    }
    for (auto a : {cr::ActionKind::RunNetworkTest, cr::ActionKind::MeasureMouse, cr::ActionKind::RunDnsBench, cr::ActionKind::RunHealthTest,
                   cr::ActionKind::IncidentWindow})
        EF_CHECK(cr::row_action_shown(make("a", cr::Level::Info, a, "")));
}

// Yenileme hizi: aygit adi / sira / en yuksek deger eksikse otomatik duzeltilemez (yanlis ekrana yazilmaz).
EF_TEST(fix_plan_refresh_rate_needs_device_and_values) {
    auto c = make("display.0.refresh", cr::Level::Problem, cr::ActionKind::AutoFix, "fix.refresh_rate");
    EF_CHECK(!cr::is_auto_fixable(c) && !cr::row_action_shown(c));
    c.args = {{"index", "0"}, {"max", "165"}, {"device", "DISPLAY1"}};
    const auto it = cr::auto_fix_item(c);
    EF_CHECK(it && it->display && !it->admin && it->ops.size() == 1 && it->ops[0].id == OperationId::RefreshRateSet);
    c.args["index"] = "123";  // en fazla 2 hane
    EF_CHECK(!cr::is_auto_fixable(c));
    c.args["index"] = "x";
    EF_CHECK(!cr::is_auto_fixable(c));
}

// Yonetici duzeltmeleri tek islemlik; HAGS yeniden baslatma ister, hizli baslatma istemez.
EF_TEST(fix_plan_admin_items_flags) {
    const auto hags = cr::auto_fix_item(make("windows.hags", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.hags"));
    const auto fast = cr::auto_fix_item(make("windows.fast_startup", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.fast_startup"));
    EF_CHECK(hags && hags->admin && hags->restart && hags->ops.size() == 1);
    EF_CHECK(fast && fast->admin && !fast->restart && fast->ops.size() == 1);
    const auto dvr = cr::auto_fix_item(make("windows.game_dvr", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.game_dvr"));
    EF_CHECK(dvr && !dvr->admin && dvr->ops.size() == 3);  // arka plan kaydi: uc anahtar tek islemde
}

// Toplu plan: yalniz Advice/Problem + gercekten duzeltilebilir + hedefinde geri alinmamis kayit yok.
EF_TEST(fix_plan_all_collects_only_fixable_findings) {
    std::vector<cr::Check> v = {
        make("windows.game_mode", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.game_mode"),
        make("windows.power", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.power"),            // yonlendirme: girmez
        make("display.1.max", cr::Level::Advice, cr::ActionKind::OpenSettings, "ms-settings:display-advanced"),  // girmez
        make("windows.game_dvr", cr::Level::Good, cr::ActionKind::AutoFix, "fix.game_dvr"),         // iyi: girmez
        make("windows.hags", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.hags"),
        make("windows.windowed_opt", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.windowed_opt"),
        make("windows.restore_point", cr::Level::Info, cr::ActionKind::AutoFix, "fix.restore_point"),  // istege bagli is: girmez
    };
    auto p = cr::plan_fix_all(v, {});
    EF_CHECK(p.items.size() == 3 && p.user_count() == 2 && p.admin_count() == 1 && p.has_restart() && !p.has_display());
    EF_CHECK(p.items[0].check_id == "windows.game_mode" && p.items[1].check_id == "windows.hags" && p.items[2].check_id == "windows.windowed_opt");
    // Hedefte zaten etkin kayit olan bulguya ikinci yazma planlanmaz.
    std::vector<cr::UndoState> undo(v.size(), cr::UndoState::None);
    undo[0] = cr::UndoState::Undo;
    undo[4] = cr::UndoState::Retry;
    p = cr::plan_fix_all(v, undo);
    EF_CHECK(p.items.size() == 1 && p.items[0].check_id == "windows.windowed_opt");
    // Duzeltilecek bir sey yoksa plan bos (Panel "Duzelt" devre disi).
    EF_CHECK(cr::plan_fix_all({make("a", cr::Level::Advice, cr::ActionKind::OpenUrl, "https://x")}, {}).empty());
    EF_CHECK(cr::plan_fix_all({}, {}).empty());
}

// Gercek kurallardan: guc plani Builtin'de bile yalniz yonlendirme; Oyun Modu kapali ise duzeltilebilir.
EF_TEST(fix_plan_from_real_windows_rules) {
    cr::WindowsGamingInfo w;
    w.game_mode = false;
    w.power_mode = "balanced";
    w.power_plan_kind = cr::WindowsGamingInfo::PowerPlanKind::Builtin;
    const auto checks = cr::windows_checks(w);
    const auto power = std::find_if(checks.begin(), checks.end(), [](const cr::Check& c) { return c.id == "windows.power"; });
    EF_CHECK(power != checks.end() && power->action == cr::ActionKind::AutoFix && !cr::row_action_shown(*power));
    const auto p = cr::plan_fix_all(checks, {});
    EF_CHECK(p.items.size() == 1 && p.items[0].check_id == "windows.game_mode");
}

// MOCK: Duzelt -> (Evet) uygula -> geri okuma -> Geri al -> eski deger. Tum otomatik duzeltmeler ayni akista.
EF_TEST(fix_plan_mock_apply_readback_and_undo) {
    MockEnv e;  // MOCK adaptor: gercek ayar yazilmaz
    e.adapter.values["gaming:game_mode"] = "0";
    e.adapter.values["gaming:dvr_background"] = "1";
    e.adapter.values["gaming:app_capture"] = "1";
    e.adapter.values["gaming:dvr_enabled"] = "1";
    // windowed_opt anahtari yok (Windows varsayilani): geri almada yine yok olmali.
    const std::vector<cr::Check> v = {
        make("windows.game_mode", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.game_mode"),
        make("windows.game_dvr", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.game_dvr"),
        make("windows.windowed_opt", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.windowed_opt"),
    };
    const auto plan = cr::plan_fix_all(v, {});
    EF_CHECK(plan.items.size() == 3 && plan.user_count() == 3);
    // "Evet" oncesi hicbir sey yazilmamistir.
    EF_CHECK(e.adapter.applies == 0 && e.adapter.values["gaming:game_mode"] == "0");
    int applied = 0;
    for (const auto& it : plan.items) {
        auto r = cr::apply_fix_item(e.engine, it, e.maker());
        EF_CHECK(r.ok() && r.value().state == TxState::Active);
        if (r.ok() && r.value().state == TxState::Active) ++applied;
    }
    EF_CHECK(applied == 3);
    // Geri okuma: gercek hedefler yazilan degerde.
    EF_CHECK(e.adapter.values["gaming:game_mode"] == "1");
    EF_CHECK(e.adapter.values["gaming:dvr_background"] == "0" && e.adapter.values["gaming:app_capture"] == "0" &&
              e.adapter.values["gaming:dvr_enabled"] == "0");
    EF_CHECK(e.adapter.values["gaming:windowed_opt"] == "1");
    // Kalici kayit: uc islem Active.
    const auto scan = e.store.scan();
    EF_CHECK(scan.transactions.size() == 3);
    for (const auto& t : scan.transactions) EF_CHECK(t.state == TxState::Active);
    // Sonuc cumlesi: "N ayar duzeltildi".
    const auto n = cr::fix_all_notice(applied, 0, false, "");
    EF_CHECK(n.key == "chk.notice.fix_all_done" && !n.warning);
    // Geri al: bulgunun TUM hedefleri (arka plan kaydinin uc anahtari) birlikte, geri okumayla dogrulanarak.
    for (const auto& c : v) {
        const auto targets = cr::fix_targets_for_check(c);
        EF_CHECK(!targets.empty());
        const auto st = core::broker::restore_matching(
            &e.store, &e.engine,
            [&targets](const contracts::SettingOperation& o) {
                for (const auto& t : targets)
                    if (cr::fix_target_matches(t, o.target)) return true;
                return false;
            },
            targets.front());
        EF_CHECK(st.state == core::broker::StepState::Restored);
    }
    EF_CHECK(e.adapter.values["gaming:game_mode"] == "0");
    EF_CHECK(e.adapter.values["gaming:dvr_background"] == "1" && e.adapter.values["gaming:app_capture"] == "1" &&
              e.adapter.values["gaming:dvr_enabled"] == "1");
    EF_CHECK(e.adapter.values.count("gaming:windowed_opt") == 0);  // eskiden yoktu: yine yok
}

// MOCK: yazma basarisiz olursa motor geri alir; duzeltildi sayilmaz.
EF_TEST(fix_plan_mock_failed_apply_is_not_counted) {
    MockEnv e;
    e.adapter.values["gaming:game_mode"] = "0";
    e.adapter.fail_apply_target = "gaming:game_mode";
    const auto it = cr::auto_fix_item(make("windows.game_mode", cr::Level::Advice, cr::ActionKind::AutoFix, "fix.game_mode"));
    EF_CHECK(it.has_value());
    if (!it) return;
    const auto r = cr::apply_fix_item(e.engine, *it, e.maker());
    EF_CHECK(!r.ok() || r.value().state != TxState::Active);
    EF_CHECK(e.adapter.values["gaming:game_mode"] == "0");  // eski deger korundu
}

// Sonuc cumlesi: dogrulanmamis basari yazilmaz; UAC reddi hicbir ayar duzelmediyse oldugu gibi soylenir.
EF_TEST(fix_plan_all_notice_is_honest) {
    EF_CHECK(cr::fix_all_notice(2, 0, false, "").key == "chk.notice.fix_all_done");
    EF_CHECK(cr::fix_all_notice(2, 0, true, "chk.notice.fix_reboot").key == "chk.notice.fix_all_reboot");
    const auto part = cr::fix_all_notice(1, 1, false, "");
    EF_CHECK(part.key == "chk.notice.fix_all_partial" && part.warning);
    const auto denied = cr::fix_all_notice(0, 1, false, "chk.notice.fix_denied");
    EF_CHECK(denied.key == "chk.notice.fix_denied" && denied.warning);
    EF_CHECK(cr::fix_all_notice(0, 2, false, "").key == "chk.notice.fix_failed");
    EF_CHECK(cr::fix_all_notice(1, 1, false, "chk.notice.fix_denied").key == "chk.notice.fix_all_partial");
}

// Tek Evet/Hayir: baslik "Uygulansin mi?", her ayar bir madde, "Geri alinabilir." + ayrinti, ilk odak Hayir.
EF_TEST(fix_plan_all_modal_is_one_question_with_no_focused) {
    auto cat = core::i18n::Catalog::embedded(core::i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    if (!cat.ok()) return;
    const auto m = ui::fix_all_modal_spec(cat.value(), {L"Oyun Modu: Kapalı → Açık", L"Pencereli oyun iyileştirmesi: Kapalı → Açık"}, L"Ayrinti.");
    EF_CHECK(m.title == L"Uygulansın mı?");
    EF_CHECK(m.list.size() == 2 && m.body.size() == 1 && m.body[0] == L"Geri alınabilir. Ayrinti.");
    EF_CHECK(m.yes == L"Evet" && m.no == L"Hayır" && !m.destructive && m.choices.empty());
    EF_CHECK(ui::modal_open(m).focus.item == ui::ModalItem::No);  // yanlislikla Enter hicbir sey yazmaz
}
