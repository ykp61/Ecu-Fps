// Zorunlu guncelleme karti ve seridi (spec 2026-11-11-zorunlu-guncelleme-design.md §1): sabit kimlikler, UIA adlari,
// kart odak sirasi, "Onceki ayarlara don" gorunurlugu ve metin anahtarlari.
#include <algorithm>
#include <iterator>
#include <string>

#include "core/i18n/catalog.h"
#include "ef_test.h"
#include "ui/shell/screen.h"

using namespace ef::ui;
using ef::core::update::UpdateOutcome;

EF_TEST(update_hits_are_fixed_named_controls) {
    for (const Hit h : {Hit::UpdateStatus, Hit::UpdateNow, Hit::UpdateQuit, Hit::UpdateRestore, Hit::UpdateOpenPage}) {
        EF_CHECK(classify_hit(h).kind == HitKind::Fixed);
        EF_CHECK(std::find(std::begin(kFixedHits), std::end(kFixedHits), h) != std::end(kFixedHits));
    }
    EF_CHECK(hit_automation_id(Hit::UpdateNow) == L"update.now");
    EF_CHECK(hit_automation_id(Hit::UpdateQuit) == L"update.quit");
    EF_CHECK(hit_automation_id(Hit::UpdateRestore) == L"update.restore");
    EF_CHECK(hit_automation_id(Hit::UpdateOpenPage) == L"update.open_page");
    EF_CHECK(hit_automation_id(Hit::UpdateStatus) == L"update.status");
    EF_CHECK(update_gate_hit(Hit::UpdateNow) && !update_gate_hit(Hit::UpdateStatus) && !update_gate_hit(nav_hit(0)) && !update_gate_hit(Hit::Toggle));
}

// Kart yalniz Mandatory/NotWritable'da; kontrol suruyor/edilemedi/guncel iken uygulama normal (serit veya hicbir sey).
EF_TEST(update_gate_only_for_mandatory) {
    ViewModel vm;
    EF_CHECK(vm.update.outcome == UpdateOutcome::UpToDate);  // varsayilan: serit/kart yok
    for (const auto o : {UpdateOutcome::UpToDate, UpdateOutcome::Checking, UpdateOutcome::CheckFailed, UpdateOutcome::VerifyFailed}) {
        vm.update.outcome = o;
        EF_CHECK(!update_gate_shown(vm));
    }
    vm.update.outcome = UpdateOutcome::Mandatory;
    EF_CHECK(update_gate_shown(vm));
    vm.update.outcome = UpdateOutcome::NotWritable;
    EF_CHECK(update_gate_shown(vm));
}

// "Onceki ayarlara don" Performans acikken veya etkin degisiklik kaydi varken gorunur; aksi halde yok.
EF_TEST(update_gate_shows_restore_when_active) {
    using ef::core::perf::Phase;
    ViewModel vm;
    vm.update.outcome = UpdateOutcome::Mandatory;
    EF_CHECK(!show_restore(vm));
    vm.perf.phase = Phase::On;
    vm.perf.switch_on = true;
    EF_CHECK(show_restore(vm));
    vm.perf = PerfView{};
    vm.perf.active = 3;  // etkin journal kaydi
    EF_CHECK(show_restore(vm));
    vm.perf = PerfView{};
    vm.perf.phase = Phase::RecoveryNeeded;
    EF_CHECK(show_restore(vm));
    vm.perf = PerfView{};
    vm.update.rollback_available = true;
    EF_CHECK(show_restore(vm));
}

// Kartta odak sirasi: ana eylem, [Onceki ayarlara don], Cik. Sekmeler ve sayfa ogeleri yok.
EF_TEST(update_gate_focus_order) {
    UiState s;
    s.page = Page::Measurements;
    s.speed_can_start = true;
    s.update_gate = true;
    EF_CHECK((focus_order(s) == std::vector<Hit>{Hit::UpdateNow, Hit::UpdateQuit}));
    s.update_restore = true;
    EF_CHECK((focus_order(s) == std::vector<Hit>{Hit::UpdateNow, Hit::UpdateRestore, Hit::UpdateQuit}));
    s.update_not_writable = true;
    EF_CHECK((focus_order(s) == std::vector<Hit>{Hit::UpdateOpenPage, Hit::UpdateRestore, Hit::UpdateQuit}));
    s.update_not_writable = false;
    s.update_busy = true;  // indirme surerken ana eylem devre disi: Tab almaz
    EF_CHECK((focus_order(s) == std::vector<Hit>{Hit::UpdateRestore, Hit::UpdateQuit}));
    s = UiState{};
    const auto normal = focus_order(s);
    EF_CHECK(std::find(normal.begin(), normal.end(), Hit::UpdateNow) == normal.end());
    EF_CHECK(!normal.empty() && normal.front() == nav_hit(0));
}

EF_TEST(update_strings_are_cataloged) {
    for (const char* k : {"update.checking", "update.failed", "update.verify_failed", "update.gate_title", "update.gate_body", "update.now",
                          "update.quit", "update.restore", "update.downloading", "update.installing", "update.not_writable",
                          "update.open_page", "update.rolled_back", "update.updated", "update.download_failed", "update.restore_question",
                          "update.restore_detail", "update.progress"})
        EF_CHECK(ef::core::i18n::id_of(k).has_value());
}

// K27: kurulu surumde standart hesap NotWritable alir; govde "eskisinin yerine koy" DEGIL (Program Files'a elle
// yazamaz), indirip kurulumu calistir der. Tasinabilir surumde eski metin; Mandatory'de normal govde.
EF_TEST(update_gate_body_installed_not_writable) {
    ViewModel vm;
    vm.update.outcome = UpdateOutcome::NotWritable;
    EF_CHECK(std::string(update_gate_body_key(vm)) == "update.not_writable");
    vm.update.installed = true;
    EF_CHECK(std::string(update_gate_body_key(vm)) == "update.not_writable_installed");
    EF_CHECK(ef::core::i18n::id_of("update.not_writable_installed").has_value());
    vm.update.outcome = UpdateOutcome::Mandatory;
    EF_CHECK(std::string(update_gate_body_key(vm)) == "update.gate_body");
}

// ADR-033 / spec 7: kurulu surumde hizmet yok/bozuk -> ana eylem "Yardimci hizmeti onar" (sabit kimlik, kart dugmesi).
EF_TEST(update_gate_repair_service_hit) {
    EF_CHECK(classify_hit(Hit::UpdateRepairService).kind == HitKind::Fixed);
    EF_CHECK(std::find(std::begin(kFixedHits), std::end(kFixedHits), Hit::UpdateRepairService) != std::end(kFixedHits));
    EF_CHECK(hit_automation_id(Hit::UpdateRepairService) == L"update.repair_service");
    EF_CHECK(update_gate_hit(Hit::UpdateRepairService));
    UiState s;
    s.update_gate = true;
    s.update_repair = true;
    EF_CHECK((focus_order(s) == std::vector<Hit>{Hit::UpdateRepairService, Hit::UpdateQuit}));
    s.update_busy = true;  // onarim suruyor
    EF_CHECK((focus_order(s) == std::vector<Hit>{Hit::UpdateQuit}));
    for (const char* k : {"update.repair_service", "update.service_unavailable", "update.service_failed", "update.repairing", "update.repair_failed",
                          "update.repair_question", "update.repair_detail"})
        EF_CHECK(ef::core::i18n::id_of(k).has_value());
}
