// UI denetimi Track C (2026-11-05): Oyun Hazirligi satir kurallari — dugme yerlesimi (RDY-03), vurgu duzeyi
// (RDY-04), etiketler (RDY-07), durum metni (RDY-12), satir ici bildirim (RDY-08), mesgul odak (RDY-09) ve metinler
// (RDY-05/06/13). Saf mantik; cizim ui_snapshot'ta denetlenir.
#include <algorithm>
#include <string>

#include "core/i18n/catalog.h"
#include "ef_test.h"
#include "ui/consent/fix_dialogs.h"
#include "ui/shell/screen.h"

using namespace ef::ui;
namespace cr = ef::core::readiness;
namespace i18n = ef::core::i18n;
using i18n::StringId;

namespace {
bool has(const std::vector<Hit>& v, Hit h) { return std::find(v.begin(), v.end(), h) != v.end(); }
Hit action(int k) { return static_cast<Hit>(static_cast<int>(Hit::ChkAction0) + k); }
Hit undo(int k) { return static_cast<Hit>(static_cast<int>(Hit::ChkUndo0) + k); }
Hit keep(int k) { return static_cast<Hit>(static_cast<int>(Hit::ChkKeep0) + k); }
}  // namespace

EF_TEST(ui_readiness_buttons_move_below_only_when_text_gets_narrow) {
    EF_CHECK(!readiness_buttons_below(500.f, 0));   // dugme yok
    EF_CHECK(!readiness_buttons_below(500.f, 1));   // tek dugme, genis metin: yan yana
    EF_CHECK(!readiness_buttons_below(kReadinessMinTextW, 1));
    EF_CHECK(readiness_buttons_below(kReadinessMinTextW - 1.f, 1));  // metne yer kalmadi -> alta
    // Spec 2026-11-12 B3: bulgu satiri tek satirdir; ozel durum satiri (Geri al + Koru, 3 dugme) de dugmelerini satirin
    // saginda tutar. Alta inme yalniz dugmelerden sonra metne kalan genislige baglidir (dugme sayisina degil).
    EF_CHECK(!readiness_buttons_below(900.f, 2));
    EF_CHECK(!readiness_buttons_below(kReadinessMinTextW, 3));
    EF_CHECK(readiness_buttons_below(kReadinessMinTextW - 1.f, 2));
    EF_CHECK(readiness_buttons_below(100.f, 3));
    EF_CHECK(!readiness_buttons_below(0.f, 0));
}

EF_TEST(ui_readiness_button_hierarchy) {
    // Geri al / Yeniden dene onerilen (accent); Koru sakin + uyari; yalniz otomatik Duzelt onerilen.
    EF_CHECK(readiness_button_style(RowButton::Undo, cr::ActionKind::OpenSettings) == RowButtonStyle::Recommended);
    EF_CHECK(readiness_button_style(RowButton::Keep, cr::ActionKind::AutoFix) == RowButtonStyle::Caution);
    EF_CHECK(readiness_button_style(RowButton::Action, cr::ActionKind::AutoFix) == RowButtonStyle::Recommended);
    EF_CHECK(readiness_button_style(RowButton::Action, cr::ActionKind::OpenSettings) == RowButtonStyle::Neutral);
    EF_CHECK(readiness_button_style(RowButton::Action, cr::ActionKind::OpenUrl) == RowButtonStyle::Neutral);
    EF_CHECK(readiness_button_style(RowButton::Action, cr::ActionKind::RunNetworkTest) == RowButtonStyle::Neutral);
}

EF_TEST(ui_readiness_undo_labels_and_state_text) {
    // RDY-07: "Yeniden dene" yalniz dogrulanamamis onceki geri almada.
    EF_CHECK(readiness_undo_label(cr::UndoState::Undo) == StringId::ChkActionUndo);
    EF_CHECK(readiness_undo_label(cr::UndoState::Interrupted) == StringId::ChkActionUndo);
    EF_CHECK(readiness_undo_label(cr::UndoState::Conflict) == StringId::ChkActionUndo);
    EF_CHECK(readiness_undo_label(cr::UndoState::Retry) == StringId::ChkActionRetryUndo);
    // RDY-12: durum metni ayri (HelpText); Undo/None icin yok.
    EF_CHECK(!readiness_undo_state_text(cr::UndoState::None) && !readiness_undo_state_text(cr::UndoState::Undo));
    EF_CHECK(readiness_undo_state_text(cr::UndoState::Retry) == StringId::ChkUndoStateRecovery);
    EF_CHECK(readiness_undo_state_text(cr::UndoState::Interrupted) == StringId::ChkUndoStateInterrupted);
    EF_CHECK(readiness_undo_state_text(cr::UndoState::Conflict) == StringId::ChkUndoStateConflict);
}

EF_TEST(ui_readiness_notice_row_matches_check_id) {
    ViewModel::ReadinessView rv;
    rv.checks = {{"display.0.refresh", cr::Section::Display, cr::Level::Problem, "x", {}, cr::ActionKind::AutoFix, ""},
                 {"gfx.hags", cr::Section::Graphics, cr::Level::Advice, "y", {}, cr::ActionKind::AutoFix, "fix.hags"}};
    EF_CHECK(!readiness_notice_row(rv, 2));  // bildirim yok
    rv.notice_key = "chk.notice.undo_failed";
    EF_CHECK(!readiness_notice_row(rv, 2));  // genel bildirim: yalniz ozet karti
    rv.notice_check_id = "gfx.hags";
    EF_CHECK(readiness_notice_row(rv, 2) == std::optional<std::size_t>(1));
    EF_CHECK(!readiness_notice_row(rv, 1));  // cizilen satir disinda
    rv.notice_check_id = "net.none";          // yeniden taramada satir kalkti
    EF_CHECK(!readiness_notice_row(rv, 2));
}

EF_TEST(ui_readiness_busy_buttons_disabled_but_focus_stable) {
    // RDY-09: is surerken Yeniden tara + satir dugmeleri niyet sirasinda (kaybolmaz), gercek Tab sirasinda degil.
    UiState s;
    s.page = Page::Readiness;
    s.readiness_busy = true;
    s.readiness_net = true;
    s.readiness_actions = {true, true, false};
    s.readiness_undos = {false, false, true};
    s.readiness_keeps = {false, false, true};
    const auto intent = focus_order(s);
    EF_CHECK(has(intent, Hit::ChkRescan) && has(intent, Hit::ChkNetTest));
    EF_CHECK(has(intent, action(1)) && has(intent, undo(2)) && has(intent, keep(2)));
    HitMap hits;
    for (std::size_t i = 0; i < kPageCount; ++i) hits.add(nav_hit(i), {0, 40.f * static_cast<float>(i), 100, 40}, L"nav", AccRole::TabItem);
    for (Hit h : {Hit::ChkRescan, Hit::ChkNetTest, action(0), action(1), undo(2), keep(2)})
        hits.add(h, {300, 0, 80, 32}, L"btn", AccRole::Button, false, -1, false);
    const auto real = focus_order(s, hits);
    for (Hit h : {Hit::ChkRescan, Hit::ChkNetTest, action(0), action(1), undo(2), keep(2)}) {
        EF_CHECK(!has(real, h));
        EF_CHECK(hits.meta.count(h) == 1 && !hits.activatable(h));  // UIA'da IsEnabled=false olarak kalir
    }
    // Odak devre disi dugmede kalir; Tab niyet sirasindaki yerinden devam eder (basa atlamaz).
    hits.meta[keep(2)].enabled = true;
    const auto real2 = focus_order(s, hits);
    EF_CHECK(focus_step(intent, real2, action(1), 1) == keep(2));
    EF_CHECK(focus_step(intent, real2, action(1), -1) == real2[real2.size() - 2]);  // son kenar cubugu ogesi
    EF_CHECK(focus_step(intent, real2, keep(2), 1) == real2.front());                 // sarar
    EF_CHECK(focus_step(intent, real2, Hit::None, 1) == real2.front());
    EF_CHECK(focus_step(intent, real2, Hit::None, -1) == real2.back());
    EF_CHECK(focus_step(intent, {}, keep(2), 1) == Hit::None);
}

EF_TEST(ui_readiness_focus_falls_back_within_row) {
    HitMap hits;
    hits.add(Hit::ChkRescan, {0, 0, 80, 32}, L"tara", AccRole::Button);
    hits.add(action(3), {0, 40, 80, 32}, L"Duzelt", AccRole::Button);
    // Geri al bitti -> satirda yalniz Duzelt kaldi: odak ayni satirda.
    EF_CHECK(readiness_focus_fallback(undo(3), hits) == action(3));
    EF_CHECK(readiness_focus_fallback(keep(3), hits) == action(3));
    EF_CHECK(readiness_focus_fallback(action(3), hits) == action(3));  // hala var
    EF_CHECK(readiness_focus_fallback(undo(5), hits) == Hit::ChkRescan);  // satir dugmesiz
    EF_CHECK(readiness_focus_fallback(Hit::Toggle, hits) == Hit::Toggle);  // baska oge: dokunulmaz
    EF_CHECK(readiness_focus_fallback(Hit::None, hits) == Hit::None);
    hits.add(undo(5), {0, 80, 80, 32}, L"Geri al", AccRole::Button);
    EF_CHECK(readiness_focus_fallback(keep(5), hits) == undo(5));  // onerilen eylem once
}

EF_TEST(ui_readiness_help_text_separate_from_name) {
    HitMap hits;
    hits.add(undo(0), {0, 0, 80, 32}, L"Geri al: Ekran 60 Hz", AccRole::Button);
    hits.meta[undo(0)].help = L"Önceki işlem yarım kaldı";
    EF_CHECK(hits.meta.at(undo(0)).name.find(L"yarım") == std::wstring::npos);
    EF_CHECK(!hits.meta.at(undo(0)).help.empty());
    hits.add(action(0), {0, 40, 80, 32}, L"Düzelt", AccRole::Button);
    EF_CHECK(hits.meta.at(action(0)).help.empty());  // varsayilan yok
}

EF_TEST(ui_readiness_keep_strings_honest_in_both_locales) {
    for (auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        auto cat = i18n::Catalog::embedded(loc);
        EF_CHECK(cat.ok());
        if (!cat.ok()) continue;
        const auto& c = cat.value();
        const std::string admin(c.get(StringId::ChkKeepDetailAdmin));
        const std::string user(c.get(StringId::ChkKeepDetail));
        // RDY-05: yonetici metni de geri donussuzlugu ilk cumlede soyler (kullanici metnindeki cumleyle ayni).
        const std::string sentence = loc == i18n::Locale::TrTR ? "Bu kayıtla önceki değere artık dönülemez." :
                                                                 "The previous value can no longer be restored from this record.";
        EF_CHECK(admin.rfind(sentence, 0) == 0);
        EF_CHECK(user.find(sentence) != std::string::npos);
        // RDY-06: Koru icin ayri "bulunamadi" metni (geri alma metni degil)
        const std::string kn(c.get(StringId::ChkNoticeKeepNothing));
        EF_CHECK(!kn.empty() && kn != std::string(c.get(StringId::ChkNoticeNothing)));
        EF_CHECK(i18n::id_of("chk.notice.keep_nothing") == StringId::ChkNoticeKeepNothing);
        // RDY-13: keep_failed sonraki adimi soyler (Yeniden tara + tekrar dene)
        const std::string kf(c.get(StringId::ChkNoticeKeepFailed));
        EF_CHECK(kf.find(std::string(c.get(StringId::ChkRescan))) != std::string::npos);
    }
}

EF_TEST(ui_readiness_incident_window_label_and_style) {
    cr::Check c{"hardware.incidents", cr::Section::Hardware, cr::Level::Good, "chk.incident.none", {{"days", "7"}}, cr::ActionKind::IncidentWindow, "30"};
    EF_CHECK(readiness_action_label(c) == StringId::ChkActionIncident30);
    c.action_arg = "7";
    EF_CHECK(readiness_action_label(c) == StringId::ChkActionIncident7);
    c.action = cr::ActionKind::AutoFix;
    EF_CHECK(readiness_action_label(c) == StringId::ChkActionFix);
    EF_CHECK(readiness_button_style(RowButton::Action, cr::ActionKind::IncidentWindow) == RowButtonStyle::Neutral);
    for (auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        auto cat = i18n::Catalog::embedded(loc);
        EF_CHECK(cat.ok());
        if (!cat.ok()) continue;
        const std::string l30(cat.value().get(StringId::ChkActionIncident30)), l7(cat.value().get(StringId::ChkActionIncident7));
        EF_CHECK(!l30.empty() && !l7.empty() && l30 != l7 && l30.find("30") != std::string::npos && l7.find('7') != std::string::npos);
    }
}

// Zehir yesili (spec §5): skor karti ve Panel ozeti ayni sayimi kullanir. Oneri = Sorun + Oneri; Bilgi sayilmaz.
EF_TEST(readiness_score_counts) {
    ViewModel::ReadinessView rv;
    const auto mk = [](const char* id, cr::Level l) { return cr::Check{id, cr::Section::Display, l, "x", {}, cr::ActionKind::None, ""}; };
    rv.checks = {mk("a", cr::Level::Good), mk("b", cr::Level::Advice), mk("c", cr::Level::Good), mk("d", cr::Level::Unknown),
                 mk("e", cr::Level::Problem), mk("f", cr::Level::Good)};
    const auto sc = readiness_score(rv);
    EF_CHECK(sc.good == 3 && sc.advice == 2 && sc.unmeasured == 1 && sc.total == 6);
    EF_CHECK(sc.problem == 1);  // B3 sayim satiri: "1 sorun · 1 oneri" (oneri = advice - problem)
    rv.checks.push_back(mk("g", cr::Level::Info));  // bilgi satiri skora girmez
    const auto si = readiness_score(rv);
    EF_CHECK(si.good == 3 && si.advice == 2 && si.unmeasured == 1 && si.total == 6);
    EF_CHECK(readiness_score(ViewModel::ReadinessView{}).total == 0);  // sonuc yok: sayi uretilmez
}

EF_TEST(readiness_display_order_groups_advice_first) {
    ViewModel::ReadinessView rv;
    const auto mk = [](const char* id, cr::Level l) { return cr::Check{id, cr::Section::Display, l, "x", {}, cr::ActionKind::None, ""}; };
    rv.checks = {mk("g0", cr::Level::Good), mk("u1", cr::Level::Unknown), mk("a2", cr::Level::Advice), mk("i3", cr::Level::Info),
                 mk("p4", cr::Level::Problem), mk("g5", cr::Level::Good)};
    const auto o = readiness_display_order(rv, rv.checks.size());
    EF_CHECK((o == std::vector<std::size_t>{2, 4, 1, 0, 5, 3}));
    EF_CHECK(readiness_advice_group(cr::Level::Problem) && readiness_advice_group(cr::Level::Unknown));
    EF_CHECK(!readiness_advice_group(cr::Level::Good) && !readiness_advice_group(cr::Level::Info));
    EF_CHECK(readiness_display_order(rv, 2) == (std::vector<std::size_t>{1, 0}));  // cizilen satir siniri
    // Tab sirasi gorunen sirayi izler (kimlik bulgu indeksiyle kalir).
    UiState s;
    s.page = Page::Readiness;
    s.readiness_actions = {true, true, true, true, true, true};
    s.readiness_order = o;
    const auto ord = focus_order(s);
    const auto at = [&](Hit h) { return std::find(ord.begin(), ord.end(), h) - ord.begin(); };
    EF_CHECK(at(action(2)) < at(action(4)) && at(action(4)) < at(action(1)) && at(action(1)) < at(action(0)) &&
              at(action(0)) < at(action(5)) && at(action(5)) < at(action(3)));
}

// Spec 2026-11-12 B3: ana (dolu) dugme "Sorun" satirlarindadir (eski kural: yalniz ilk oneri). Oneri cerceveli;
// olculmeyen / bilgi / iyi satirlarinda mevcut eylem ikincil kalir.
EF_TEST(readiness_primary_button_only_on_problem_rows) {
    using L = cr::Level;
    EF_CHECK(readiness_action_primary(L::Problem));
    for (const L l : {L::Advice, L::Unknown, L::Good, L::Info}) EF_CHECK(!readiness_action_primary(l));
    // Cerceveli oneri: otomatik Duzelt accent cerceve (Recommended), digerleri notr; Geri al / Koru degismedi.
    EF_CHECK(readiness_button_style(RowButton::Action, cr::ActionKind::AutoFix) == RowButtonStyle::Recommended);
    EF_CHECK(readiness_button_style(RowButton::Action, cr::ActionKind::RunNetworkTest) == RowButtonStyle::Neutral);
}

// Spec 2026-11-12 B3: bulgu satiri = kisa baslik + soluk kisa ek (bulgu cumlesinin ilk / kalan kismi).
EF_TEST(readiness_split_title_and_extra) {
    auto sp = readiness_split(L"DELL: 60 Hz'de çalışıyor, bu çözünürlükte 165 Hz destekleniyor.");
    EF_CHECK(sp.title == L"DELL: 60 Hz'de çalışıyor" && sp.extra == L"bu çözünürlükte 165 Hz destekleniyor");
    sp = readiness_split(L"Oyun Modu kapalı. Açık olması oyun sırasında arka plan işlerini sınırlar.");
    EF_CHECK(sp.title == L"Oyun Modu kapalı" && sp.extra == L"Açık olması oyun sırasında arka plan işlerini sınırlar");
    sp = readiness_split(L"Oyun Modu açık.");
    EF_CHECK(sp.title == L"Oyun Modu açık" && sp.extra.empty());
    // Parantez ici ayirici sayilmaz; ondalik / kisaltma noktasi (bosluksuz) ayirmaz.
    sp = readiness_split(L"Bağlantı kararlı (ortalama 12 ms, kayıp %0); sorun yok.");
    EF_CHECK(sp.title == L"Bağlantı kararlı (ortalama 12 ms, kayıp %0)" && sp.extra == L"sorun yok");
    sp = readiness_split(L"Havuz 1.5 GiB");
    EF_CHECK(sp.title == L"Havuz 1.5 GiB" && sp.extra.empty());
    sp = readiness_split(L"");
    EF_CHECK(sp.title.empty() && sp.extra.empty());
    sp = readiness_split(L", x");  // bastaki ayirici baslik uretmez
    EF_CHECK(sp.title == L", x" && sp.extra.empty());
}

// Gate P3b: satirin soluk gerekcesi ozneden arindirilir ve " · " ile birlesir; ad (alan) ayri cizilir.
EF_TEST(readiness_reason_drops_subject_and_joins) {
    EF_CHECK(readiness_reason(L"DELL: 60 Hz'de çalışıyor, bu çözünürlükte 165 Hz destekleniyor.", L"DELL") ==
              L"60 Hz'de çalışıyor · bu çözünürlükte 165 Hz destekleniyor");
    EF_CHECK(readiness_reason(L"Oyun Modu açık.", L"") == L"Oyun Modu açık");
    // Ozne cumlenin basinda degilse ya da ardinda ": " yoksa cumle aynen bolunur.
    EF_CHECK(readiness_reason(L"DELLX: açık.", L"DELL") == L"DELLX: açık");
    EF_CHECK(readiness_reason(L"DELL", L"DELL") == L"DELL");
    EF_CHECK(readiness_reason(L"", L"DELL").empty());
}

// Spec 2026-11-14 B11: dugmeli / onerili bulgu kendi kutucugudur; dugmesiz iyi / bilgi bulgusu ayni alanin kutucuguna
// katlanir, alanin kutucugu yoksa yeni kutucuk acar. Hicbir bulgu dusmez.
EF_TEST(readiness_tiles_fold_quiet_checks_into_their_area) {
    ViewModel::ReadinessView rv;
    rv.checks = {{"d.refresh", cr::Section::Display, cr::Level::Problem, "k", {}, cr::ActionKind::AutoFix, ""},
                 {"d.hdr", cr::Section::Display, cr::Level::Info, "k", {}, cr::ActionKind::None, ""},
                 {"g.dx", cr::Section::Graphics, cr::Level::Good, "k", {}, cr::ActionKind::None, ""},
                 {"w.vbs", cr::Section::Windows, cr::Level::Info, "k", {}, cr::ActionKind::OpenSettings, ""},
                 {"g.dx9", cr::Section::Graphics, cr::Level::Good, "k", {}, cr::ActionKind::None, ""}};
    const std::vector<std::size_t> order = {0, 3, 1, 2, 4};
    const std::vector<bool> loud = {true, false, false, true, false};
    const auto tiles = readiness_tiles(rv, order, loud);
    EF_CHECK(tiles.size() == 3);
    EF_CHECK(tiles[0].k == 0 && tiles[0].extra == std::vector<std::size_t>{1});  // HDR bilgisi Ekran kutucugunda
    EF_CHECK(tiles[1].k == 3 && tiles[1].extra.empty());
    EF_CHECK(tiles[2].k == 2 && tiles[2].extra == std::vector<std::size_t>{4});  // Grafik: yeni kutucuk, ikinci iyi katlanir
    std::size_t total = 0;
    for (const auto& tl : tiles) total += 1 + tl.extra.size();
    EF_CHECK(total == rv.checks.size());
    EF_CHECK(readiness_tiles(rv, {}, loud).empty());
}

// Spec 2026-11-14 B11: "Sistem kontrolu" bulgu gruplari gercek bulgu anahtarlarindan; grup kutucugu kimlikleri ayri blokta.
EF_TEST(readiness_finding_groups_follow_finding_sources) {
    EF_CHECK(finding_group("diag.on_battery") == FindingGroup::Power);
    EF_CHECK(finding_group("diag.power_saver_plan") == FindingGroup::Power);
    EF_CHECK(finding_group("diag.vbs_running") == FindingGroup::Security);
    EF_CHECK(finding_group("diag.hvci_running") == FindingGroup::Security);
    EF_CHECK(finding_group("health.low_disk") == FindingGroup::Storage);
    EF_CHECK(finding_group("health.xmp_off") == FindingGroup::Hardware);
    EF_CHECK(finding_group("update.reboot_pending") == FindingGroup::Updates);
    EF_CHECK(finding_group("diag.gate.no_real_holdout") == FindingGroup::Other);
    EF_CHECK(hit_automation_id(hit_of(HitKind::ChkGroup, 2)) == L"readiness.group.2");
    EF_CHECK(hit_automation_id(hit_of(HitKind::ChkDetail, 7)) == L"readiness.detail.7");
    EF_CHECK(hit_of(HitKind::ChkGroup, static_cast<std::size_t>(kFindingGroupCount)) == Hit::None);
    UiState s;
    EF_CHECK(s.readiness_detail_open.empty() && s.readiness_group_open == 0);
}

// Gate P3b: "Sistem kontrolu" ac/kapa sabit kimlik ve UIA kimligi tasir; varsayilan kapali.
EF_TEST(readiness_checkup_toggle_id_and_default_closed) {
    EF_CHECK(hit_automation_id(Hit::ChkCheckup) == L"readiness.checkup");
    UiState s;
    EF_CHECK(!s.readiness_checkup_open);
}

// Spec 2026-11-12 B3: ozel durum satiri tek kisa uyari satiri tasir; tam cumle HelpText'te kalir (RDY-12).
EF_TEST(readiness_special_states_have_one_short_warning) {
    EF_CHECK(!readiness_undo_warning(cr::UndoState::None) && !readiness_undo_warning(cr::UndoState::Undo));
    EF_CHECK(readiness_undo_warning(cr::UndoState::Retry) == StringId::ReadinessWarnRecovery);
    EF_CHECK(readiness_undo_warning(cr::UndoState::Interrupted) == StringId::ReadinessWarnInterrupted);
    EF_CHECK(readiness_undo_warning(cr::UndoState::Conflict) == StringId::ReadinessWarnConflict);
    for (auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        auto cat = i18n::Catalog::embedded(loc);
        EF_CHECK(cat.ok());
        if (!cat.ok()) continue;
        for (auto u : {cr::UndoState::Retry, cr::UndoState::Interrupted, cr::UndoState::Conflict}) {
            const std::string brief(cat.value().get(*readiness_undo_warning(u))), full(cat.value().get(*readiness_undo_state_text(u)));
            EF_CHECK(!brief.empty() && brief.size() <= 40 && brief.size() < full.size());
        }
    }
}

// Spec 2026-11-12 B3: duzeltme onayi — "Uygulansin mi?" + "eski → yeni" + "Geri alinabilir." (+ mevcut ayrinti metni).
EF_TEST(readiness_fix_confirmation_shows_the_change) {
    auto cat = i18n::Catalog::embedded(i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    if (!cat.ok()) return;
    const auto& c = cat.value();
    cr::Check rr{"display.0.refresh", cr::Section::Display, cr::Level::Problem, "chk.display.below_max",
                 {{"name", "DELL"}, {"hz", "60"}, {"max", "165"}}, cr::ActionKind::AutoFix, "fix.refresh_rate"};
    EF_CHECK(fix_change_line(c, rr) == L"Yenileme hızı: 60 Hz → 165 Hz");
    rr.args.erase("hz");
    EF_CHECK(fix_change_line(c, rr).empty());  // eski deger bilinmiyor: uydurulmaz
    const auto fix = [](const char* arg) { return cr::Check{"x", cr::Section::Windows, cr::Level::Advice, "x", {}, cr::ActionKind::AutoFix, arg}; };
    EF_CHECK(fix_change_line(c, fix("fix.game_mode")) == L"Oyun Modu: Kapalı → Açık");
    EF_CHECK(fix_change_line(c, fix("fix.game_dvr")) == L"Arka planda kayıt: Açık → Kapalı");
    EF_CHECK(fix_change_line(c, fix("fix.fast_startup")) == L"Hızlı başlatma: Açık → Kapalı");
    EF_CHECK(fix_change_line(c, fix("fix.hags")).find(L"Kapalı → Açık") != std::wstring::npos);
    EF_CHECK(fix_change_line(c, fix("fix.dns")).empty() && fix_change_line(c, fix("fix.unknown")).empty());
    auto open_settings = fix("fix.game_mode");
    open_settings.action = cr::ActionKind::OpenSettings;
    EF_CHECK(fix_change_line(c, open_settings).empty());  // yalniz otomatik duzeltme

    const ModalSpec m = fix_apply_modal_spec(c, L"Oyun Modu: Kapalı → Açık", L"Oyun Modu kapalı.", L"Ayrinti.");
    EF_CHECK(m.title == L"Uygulansın mı?");
    EF_CHECK(m.body.size() == 2 && m.body[0] == L"Oyun Modu: Kapalı → Açık" && m.body[1] == L"Geri alınabilir. Ayrinti.");
    EF_CHECK(m.yes == L"Evet" && m.no == L"Hayır" && !m.destructive && m.choices.empty());
    EF_CHECK(m.caption.find(L"Oyun Modu kapalı.") != std::wstring::npos);  // bulgu cumlesi UIA HelpText'te
    EF_CHECK(modal_open(m).focus.item == ModalItem::No);                    // ilk odak Hayir
    // Degisiklik satiri yoksa (DNS: yeni deger listeden secilir) ilk govde satiri bulgu cumlesidir.
    const ModalSpec d = dns_modal_spec(c, L"DNS yavas.", L"Ayrinti.");
    EF_CHECK(d.title == L"Uygulansın mı?" && d.body.size() == 2 && d.body[0] == L"DNS yavas." && !d.choices.empty());
    EF_CHECK(fix_apply_modal_spec(c, L"", L"", L"").body.size() == 1);  // en az "Geri alinabilir."
}

// Spec 2026-11-12 B3 / F: hazirlik halkasinin yayi sayfaya her giriste dolar (motion.ring_fill = 600 ms); gecis
// suresince kare istenir, bitince istenmez. Baska sayfaya gecis halkayi sifirlamaz.
EF_TEST(readiness_ring_fills_on_page_entry) {
    auto cat = i18n::Catalog::embedded(i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    if (!cat.ok()) return;
    const auto& theme = default_theme();
    EF_CHECK(theme.motion.ring_fill == 600);
    Screen screen(theme, cat.value());
    UiState s;
    s.reduced_motion = true;
    s.readiness_ring_seen = true;
    screen.go(s, Page::Games, 1000);
    EF_CHECK(s.readiness_ring_seen);
    screen.go(s, Page::Readiness, 2000);
    EF_CHECK(!s.readiness_ring_seen);
    UiState n;
    EF_CHECK(!screen.animating(n, 5000));
    n.readiness_ring = Tween(0);
    n.readiness_ring.retarget(1.f, 5000, theme.motion.ring_fill);
    EF_CHECK(screen.animating(n, 5010, false) && !screen.live_animating(n, 5010));
    EF_CHECK(!screen.animating(n, 5000 + theme.motion.ring_fill + 1));
    // Hareket kapali: sure 0 -> yay aninda oraninda (gecis yok).
    n.readiness_ring = Tween(1.f);
    n.readiness_ring.retarget(1.f, 6000, effective_ms(theme.motion.ring_fill, true));
    EF_CHECK(!screen.animating(n, 6000) && n.readiness_ring.value(6000, theme.easing) == 1.f);
}
