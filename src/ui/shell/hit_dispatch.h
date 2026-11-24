#pragma once
// RDY-01: dinamik (satir/indeks) kimliklerin etkinlestirme yonlendirmesi. Kimlik -> blok eslemesi
// classify_hit (kHitRanges, cakismasiz) ile yapilir; sayfa veya deneme sirasina bagli degildir.
// Saf sablon: MainWindow kendini, birim testleri sahte geri cagrilari verir (Win32 penceresi gerekmez).
//
// Handlers: MainWindow'daki ayni adli std::function uyeleri (on_readiness_fix, on_cleanup_volume, ...).

#include <cstddef>

#include "core/measure/view.h"
#include "ui/shell/screen.h"
#include "ui/shell/view_model.h"

namespace ef::ui {

// Dinamik bloktaki kimligi isler; true: kimlik bir satir blogundaydi (islem yapilmasa da).
// false: Nav/Fixed/None (cagiran sabit kimlik dalini isler).
template <class Handlers>
bool dispatch_row_hit(Hit h, ViewModel& vm, Handlers& on) {
    const HitSlot slot = classify_hit(h);
    const std::size_t k = slot.index;
    switch (slot.kind) {
        case HitKind::None:
        case HitKind::Nav:
        case HitKind::Fixed: return false;
        case HitKind::StorageChoice:
            // M05: Durduruluyor dahil test islerken birim secimi degistirilemez.
            if (!core::measure::busy(vm.measure) && k <= vm.measure.storage_roots.size()) {
                if (vm.measure.storage_choice != k) on.note_selection(h);  // TH-04: UIA RadioButton secimi
                vm.measure.storage_choice = k;
                if (on.on_storage_select) on.on_storage_select(k);
            }
            return true;
        case HitKind::CleanToggle:
            if (k < vm.cleanup.rows.size() && on.on_cleanup_select) on.on_cleanup_select(vm.cleanup.rows[k].category, !vm.cleanup.rows[k].selected);
            return true;
        case HitKind::CleanVolume:
            if (k < vm.cleanup.volumes.size() && on.on_cleanup_volume) {
                if (vm.cleanup.volume != vm.cleanup.volumes[k]) on.note_selection(h);  // TH-04: UIA RadioButton secimi
                on.on_cleanup_volume(vm.cleanup.volumes[k]);
            }
            return true;
        case HitKind::CleanFiles:
            if (k < vm.cleanup.rows.size() && on.on_cleanup_files) on.on_cleanup_files(vm.cleanup.rows[k].category);
            return true;
        case HitKind::CheckToggle:
            if (k < vm.check.rows.size() && vm.check.rows[k].automatic && on.on_check_select) on.on_check_select(k, !vm.check.rows[k].selected);
            return true;
        case HitKind::StartupToggle:
            if (k < vm.startup.rows.size() && vm.startup.rows[k].editable && on.on_startup_select) on.on_startup_select(k, !vm.startup.rows[k].desired);
            return true;
        case HitKind::StartupServiceToggle:
            if (k < vm.startup.services.size() && vm.startup.services[k].editable && on.on_startup_service_select)
                on.on_startup_service_select(k, !vm.startup.services[k].selected);
            return true;
        case HitKind::Group:
            if (k < vm.perf.groups.size() && on.on_group_select) on.on_group_select(vm.perf.groups[k].key, !vm.perf.groups[k].selected);
            return true;
        case HitKind::GameGuide:
            if (k < vm.games.size() && on.on_game_guide) on.on_game_guide(k);
            return true;
        case HitKind::GameNvidia:
            if (k < vm.games.size() && on.on_game_nvidia) on.on_game_nvidia(k);
            return true;
        case HitKind::GameFullscreen:
            if (k < vm.games.size() && on.on_game_fullscreen) on.on_game_fullscreen(k);
            return true;
        case HitKind::GameManualRemove:
            if (k < vm.games.size() && on.on_game_manual_remove) on.on_game_manual_remove(k);
            return true;
        case HitKind::ChkUndo:
            if (k < vm.readiness.checks.size() && on.on_readiness_undo) on.on_readiness_undo(k);
            return true;
        case HitKind::ChkKeep:
            if (k < vm.readiness.checks.size() && on.on_readiness_keep) on.on_readiness_keep(k);
            return true;
        case HitKind::ChkAction:
            if (k < vm.readiness.checks.size() && on.on_readiness_fix) on.on_readiness_fix(k);
            return true;
        case HitKind::ConsentToggle:
            if (k < vm.settings.games.size() && on.on_consent_set) on.on_consent_set(vm.settings.games[k].id, !vm.settings.games[k].allow);
            return true;
        case HitKind::ConsentReset:
            if (k < vm.settings.games.size() && on.on_consent_reset) on.on_consent_reset(vm.settings.games[k].id);
            return true;
        case HitKind::HistorySelect:
            // R16: secim durumu App'te tutulur ve gorunum yeniden gonderilir (burada vm degistirilmez).
            if (k < vm.history.tests.size() && on.on_history_select) on.on_history_select(k);
            return true;
        case HitKind::HistoryRow: return true;  // yalniz arayuz durumu (satir ayrintisi ac/kapa): MainWindow isler
        case HitKind::HistoryGo: return true;   // sayfa gecisi (spec 2026-11-12 B6): MainWindow isler
        case HitKind::GameCard: return true;    // yalniz arayuz durumu (secili oyun; spec 2026-11-12 B2): MainWindow isler
        case HitKind::MeasureRecent: return true;  // yalniz arayuz durumu (test ayrintisi ac/kapa; spec 2026-11-12 C): MainWindow isler
        case HitKind::ChkDetail:
        case HitKind::ChkGroup: return true;  // yalniz arayuz durumu (kutucuk ayrintisi / bulgu grubu ac/kapa; B11): MainWindow isler
        case HitKind::ModalList:
        case HitKind::ModalChoice: return true;  // kip penceresi ogeleri: MainWindow kip durum makinesine yonlendirir
        case HitKind::UpdatesLink: {
            // Spec 2026-11-12 H: resmi uretici sayfasi (yalniz kullanici tiklayinca; App URL izin listesini uygular).
            const auto rows = updates_vendor_rows(vm);
            for (const auto& r : rows)
                if (r.link && *r.link == k && on.on_updates_link) on.on_updates_link(std::string(r.page.url));
            return true;
        }
        case HitKind::UpdatesInstall:
            // Spec H2: Windows Update onerisini kur (App Evet/Hayir, oyun kapisi ve tek kurulum kuralini uygular).
            if (k < vm.updates.scan.updates.size() && on.on_updates_install) on.on_updates_install(k);
            return true;
        case HitKind::UpdatesLocal:
            // ADR-032: ekran karti satirinin "Dosyadan kur"u (App dosya secimi, imza on denetimi, Evet/Hayir ve oyun
            // kapisini uygular); kopya surerken ayni dugme "Durdur"dur.
            if (k < vm.updates.gpus.size() && on.on_updates_local) on.on_updates_local(k);
            return true;
        case HitKind::Progress:
        case HitKind::Status:
        case HitKind::UpdatesRow: return true;  // salt okunur
    }
    return true;
}

}  // namespace ef::ui
