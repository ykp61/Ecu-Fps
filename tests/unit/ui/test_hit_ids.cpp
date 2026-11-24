// RDY-01 (UI denetimi 2026-11-05): Hit kimlik bloklari cakismaz; etkinlestirme yonlendirmesi ve UIA
// AutomationId'leri kimlik -> blok eslemesinden (classify_hit) gelir. Win32 penceresi gerekmez.
#include <cstddef>
#include <functional>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "ef_test.h"
#include "ui/shell/hit_dispatch.h"
#include "ui/shell/screen.h"

using namespace ef::ui;

namespace {

// MainWindow'daki ayni adli geri cagrilarin kaydedici esdegeri.
struct FakeHandlers {
    std::vector<std::string> calls;
    std::function<void(std::size_t)> on_storage_select = [this](std::size_t k) { calls.push_back("storage:" + std::to_string(k)); };
    std::function<void(ef::core::cleanup::CleanupCategory, bool)> on_cleanup_select = [this](auto, bool) { calls.push_back("cleanup_select"); };
    std::function<void(const std::wstring&)> on_cleanup_volume = [this](const std::wstring& v) {
        std::string s = "cleanup_volume:";
        for (const wchar_t c : v) s.push_back(static_cast<char>(c));
        calls.push_back(s);
    };
    std::function<void(ef::core::cleanup::CleanupCategory)> on_cleanup_files = [this](auto) { calls.push_back("cleanup_files"); };
    std::function<void(std::size_t, bool)> on_check_select = [this](std::size_t k, bool) { calls.push_back("check:" + std::to_string(k)); };
    std::function<void(std::size_t, bool)> on_startup_select = [this](std::size_t k, bool) { calls.push_back("startup:" + std::to_string(k)); };
    std::function<void(std::size_t, bool)> on_startup_service_select = [this](std::size_t k, bool) { calls.push_back("service:" + std::to_string(k)); };
    std::function<void(const std::string&, bool)> on_group_select = [this](const std::string&, bool) { calls.push_back("group"); };
    std::function<void(std::size_t)> on_game_guide = [this](std::size_t k) { calls.push_back("guide:" + std::to_string(k)); };
    std::function<void(std::size_t)> on_game_nvidia;
    std::function<void(std::size_t)> on_game_manual_remove = [this](std::size_t k) { calls.push_back("manual_remove:" + std::to_string(k)); };
    std::function<void(std::size_t)> on_game_fullscreen = [this](std::size_t k) { calls.push_back("fs:" + std::to_string(k)); };
    std::function<void(std::size_t)> on_readiness_undo = [this](std::size_t k) { calls.push_back("undo:" + std::to_string(k)); };
    std::function<void(std::size_t)> on_readiness_keep = [this](std::size_t k) { calls.push_back("keep:" + std::to_string(k)); };
    std::function<void(std::size_t)> on_readiness_fix = [this](std::size_t k) { calls.push_back("fix:" + std::to_string(k)); };
    std::function<void(std::size_t)> on_history_select = [this](std::size_t k) { calls.push_back("history_select:" + std::to_string(k)); };
    std::function<void(const std::string&, bool)> on_consent_set = [this](const std::string&, bool) { calls.push_back("consent_set"); };
    std::function<void(const std::string&)> on_consent_reset = [this](const std::string&) { calls.push_back("consent_reset"); };
    std::function<void(const std::string&)> on_updates_link = [this](const std::string& u) { calls.push_back("updates_link:" + u); };
    std::function<void(std::size_t)> on_updates_install = [this](std::size_t k) { calls.push_back("updates_install:" + std::to_string(k)); };
    std::function<void(std::size_t)> on_updates_local = [this](std::size_t k) { calls.push_back("updates_local:" + std::to_string(k)); };
    void note_selection(Hit) {}  // TH-04: MainWindow UIA secim olayini yayinlar; testte yan etkisiz
};

// Temizlik sayfasi ziyaret edilmis (birimler + satirlar dolu) ve hazirlik bulgulari olan gorunum.
ViewModel cleanup_and_readiness_vm() {
    ViewModel vm;
    vm.cleanup.volumes = {L"C:\\", L"D:\\"};
    vm.cleanup.rows.resize(3);
    vm.readiness.checks.resize(3);
    return vm;
}

}  // namespace

EF_TEST(ui_hit_ranges_are_disjoint_and_cover_fixed_ids) {
    EF_CHECK(hit_ranges_disjoint());
    EF_CHECK(hit_fixed_ids_in_fixed_block());
    // Calisma aninda capraz denetim: her kimlik en fazla bir bloga duser.
    for (int v = -1; v < 5000; ++v) {
        int owners = 0;
        for (const auto& r : kHitRanges)
            if (v >= r.base && v < r.base + r.count) ++owners;
        EF_CHECK(owners <= 1);
    }
}

EF_TEST(ui_hit_of_and_classify_roundtrip_every_range) {
    for (const auto& r : kHitRanges) {
        for (int i = 0; i < r.count; ++i) {
            const Hit h = hit_of(r.kind, static_cast<std::size_t>(i));
            EF_CHECK(static_cast<int>(h) == r.base + i);
            EF_CHECK(classify_hit(h) == (HitSlot{r.kind, static_cast<std::size_t>(i)}));
        }
        EF_CHECK(hit_of(r.kind, static_cast<std::size_t>(r.count)) == Hit::None);  // blok disi satir kimliksiz
    }
    EF_CHECK(classify_hit(Hit::None).kind == HitKind::None);
    EF_CHECK(classify_hit(static_cast<Hit>(4000)).kind == HitKind::None);
}

EF_TEST(ui_hit_old_colliding_ids_no_longer_shared) {
    // Eski yerlesim: CleanVolume0 = ChkAction0 = 1900, CleanFiles0 = 1950 (1900..1998 icinde).
    EF_CHECK(classify_hit(Hit::ChkAction0).kind == HitKind::ChkAction);
    EF_CHECK(classify_hit(Hit::CleanVolume0).kind == HitKind::CleanVolume);
    EF_CHECK(classify_hit(Hit::CleanFiles0).kind == HitKind::CleanFiles);
    for (int k = 0; k < kMaxReadinessRows; ++k) {
        const auto kind = classify_hit(hit_of(HitKind::ChkAction, static_cast<std::size_t>(k))).kind;
        EF_CHECK(kind == HitKind::ChkAction);
    }
    // Eski cakisma kenarlari: baslangic satiri 300+ (CheckToggle) ve 400+ (StorageChoice).
    EF_CHECK(classify_hit(hit_of(HitKind::StartupToggle, 300)).kind == HitKind::StartupToggle);
    EF_CHECK(classify_hit(hit_of(HitKind::StartupToggle, 400)).kind == HitKind::StartupToggle);
    EF_CHECK(classify_hit(hit_of(HitKind::CleanToggle, 99)).kind == HitKind::CleanToggle);
}

EF_TEST(ui_dispatch_readiness_fix_row0_not_cleanup_volume_when_volumes_present) {
    auto vm = cleanup_and_readiness_vm();
    FakeHandlers on;
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::ChkAction, 0), vm, on));
    EF_CHECK(on.calls.size() == 1 && on.calls[0] == "fix:0");
    on.calls.clear();
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::ChkAction, 2), vm, on));
    EF_CHECK(on.calls.size() == 1 && on.calls[0] == "fix:2");
    // Satir 50+ eskiden CleanFiles blogundaydi; artik yine Duzelt'e gider (satir yoksa hicbir sey).
    on.calls.clear();
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::ChkAction, 50), vm, on));
    EF_CHECK(on.calls.empty());
    // Temizlik kimlikleri kendi islevlerine gider.
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::CleanVolume, 1), vm, on));
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::CleanFiles, 0), vm, on));
    EF_CHECK(on.calls.size() == 2 && on.calls[0] == "cleanup_volume:D:\\" && on.calls[1] == "cleanup_files");
    on.calls.clear();
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::ChkUndo, 1), vm, on));
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::ChkKeep, 2), vm, on));
    EF_CHECK(on.calls.size() == 2 && on.calls[0] == "undo:1" && on.calls[1] == "keep:2");
}

EF_TEST(ui_dispatch_game_rows_guide_and_fullscreen) {
    ViewModel vm;
    vm.games.resize(2);
    FakeHandlers on;
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::GameGuide, 1), vm, on));
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::GameFullscreen, 0), vm, on));
    EF_CHECK(on.calls.size() == 2 && on.calls[0] == "guide:1" && on.calls[1] == "fs:0");
    on.calls.clear();
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::GameFullscreen, 2), vm, on));  // satir yok: cagri yok
    EF_CHECK(on.calls.empty());
}

EF_TEST(ui_dispatch_game_manual_remove_row) {
    ViewModel vm;
    vm.games.resize(2);
    FakeHandlers on;
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::GameManualRemove, 1), vm, on));
    EF_CHECK(on.calls.size() == 1 && on.calls[0] == "manual_remove:1");
    on.calls.clear();
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::GameManualRemove, 2), vm, on));
    EF_CHECK(on.calls.empty());
    EF_CHECK(hit_automation_id(hit_of(HitKind::GameManualRemove, 3)) == L"games.remove.3");
    EF_CHECK(hit_automation_id(Hit::GamesAddExe) == L"games.add_exe");
}

EF_TEST(ui_dispatch_history_rows_and_fixed_export) {
    auto vm = cleanup_and_readiness_vm();
    vm.history.tests.resize(3);  // R16: yalniz var olan test satiri secilebilir
    FakeHandlers on;
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::HistorySelect, 1), vm, on));
    EF_CHECK(on.calls.size() == 1 && on.calls[0] == "history_select:1");
    on.calls.clear();
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::HistorySelect, 3), vm, on));
    EF_CHECK(on.calls.empty());
    EF_CHECK(!dispatch_row_hit(Hit::HistoryExport, vm, on));  // sabit kimlik cagirana kalir
    // Journal satiri ac/kapa: yalniz arayuz durumu (MainWindow isler); uygulamaya geri cagri gitmez.
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::HistoryRow, 0), vm, on));
    EF_CHECK(on.calls.empty());
}

// WIN-10: hizmet secimi yalniz var olan + calisan (editable) satirda geri cagri uretir.
EF_TEST(ui_dispatch_startup_service_toggle_only_when_editable) {
    ViewModel vm;
    vm.startup.services.resize(3);
    vm.startup.services[0].editable = true;
    vm.startup.services[1].editable = false;  // durmus/var olmayan: anahtar cizilmez, cagri yok
    FakeHandlers on;
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::StartupServiceToggle, 0), vm, on));
    EF_CHECK(on.calls.size() == 1 && on.calls[0] == "service:0");
    on.calls.clear();
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::StartupServiceToggle, 1), vm, on));
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::StartupServiceToggle, 3), vm, on));  // satir yok
    EF_CHECK(on.calls.empty());
}

EF_TEST(ui_dispatch_fixed_ids_are_left_to_caller) {
    auto vm = cleanup_and_readiness_vm();
    FakeHandlers on;
    EF_CHECK(!dispatch_row_hit(Hit::Toggle, vm, on));
    EF_CHECK(!dispatch_row_hit(Hit::CleanScan, vm, on));
    EF_CHECK(!dispatch_row_hit(nav_hit(1), vm, on));
    EF_CHECK(!dispatch_row_hit(Hit::None, vm, on));
    EF_CHECK(dispatch_row_hit(Hit::Progress0, vm, on));  // salt okunur blok: islenir ama cagri yok
    EF_CHECK(on.calls.empty());
}

// ADR-032 (eski spec H "uretici sayfasi" dugmesinin yerine): uretici satirlari disari yonlendirmez. Hicbir satir
// UpdatesLink almaz (kimlik korunur, cagri yok); ekran karti satiri "Dosyadan kur" (UpdatesLocal = gpus indeksi) alir,
// yonga seti ve BIOS almaz. Windows Update onerisi ekran kartiyla eslesirse satir "Indir ve kur" (UpdatesInstall) olur.
// Satir metni salt okunurdur.
EF_TEST(ui_dispatch_updates_vendor_rows_never_push_out) {
    ViewModel vm;
    vm.system_loading = false;
    vm.updates.gpus = {{"NVIDIA GeForce RTX 5080", 0x10DE, "32.0.15.6094"}, {"Ornek GPU", 0x1234, ""}};
    vm.updates.cpu_vendor = "AuthenticAMD";
    vm.updates.board_vendor = "Gigabyte Technology Co., Ltd.";
    FakeHandlers on;
    for (std::size_t k = 0; k < 3; ++k) EF_CHECK(dispatch_row_hit(hit_of(HitKind::UpdatesLink, k), vm, on));
    EF_CHECK(on.calls.empty());  // hicbir resmi sayfa / tarayici acilmaz
    const auto rows = updates_vendor_rows(vm);
    EF_CHECK(rows.size() == 4);
    for (const auto& r : rows) EF_CHECK(!r.link);
    EF_CHECK(rows.size() == 4 && rows[0].local == std::optional<std::size_t>(0) && !rows[0].wu);
    EF_CHECK(rows.size() == 4 && !rows[1].local);  // taninmayan uretici: resmi paket yolu yok
    EF_CHECK(rows.size() == 4 && !rows[2].local && rows[2].kind == UpdatesVendorKind::Chipset);
    EF_CHECK(rows.size() == 4 && !rows[3].local && !rows[3].wu && rows[3].kind == UpdatesVendorKind::Bios);
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::UpdatesLocal, 0), vm, on));
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::UpdatesLocal, 5), vm, on));  // liste disi
    EF_CHECK(on.calls.size() == 1 && on.calls[0] == "updates_local:0");
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::UpdatesRow, 0), vm, on));  // salt okunur
    EF_CHECK(on.calls.size() == 1);
    EF_CHECK(hit_automation_id(hit_of(HitKind::UpdatesLink, 1)) == L"updates.link.1");
    EF_CHECK(hit_automation_id(hit_of(HitKind::UpdatesLocal, 1)) == L"updates.local.1");
    EF_CHECK(hit_automation_id(hit_of(HitKind::UpdatesRow, 3)) == L"updates.row.3");
    EF_CHECK(hit_automation_id(Hit::UpdatesCheck) == L"updates.check");
    EF_CHECK(hit_automation_id(nav_hit(static_cast<std::size_t>(Page::Updates))) == L"nav.updates");
}

// ADR-032: ekran karti icin Windows Update onerisi (ayni PCI ureticisi) satira eslenir; o oneri ayri satir olmaz ve
// "Dosyadan kur" yerine "Indir ve kur" (UpdatesInstall, scan.updates indeksi) sunulur. Firmware ve baska ureticinin
// onerisi eslenmez.
EF_TEST(ui_updates_gpu_row_takes_matching_windows_update_offer) {
    using namespace ef::core::drivers;
    ViewModel vm;
    vm.system_loading = false;
    vm.updates.gpus = {{"NVIDIA GeForce RTX 5080", 0x10DE, "32.0.15.6094"}};
    vm.updates.cpu_vendor = "AuthenticAMD";
    vm.updates.scan.state = ScanState::Found;
    DriverUpdate amd;
    amd.title = "Advanced Micro Devices, Inc. - Display - 32.0.21025.10016";
    amd.driver_class = "Display";
    amd.hardware_id = "PCI\\VEN_1002&DEV_13C0";
    amd.update_id = "0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6";
    amd.revision = 1;
    DriverUpdate nv = amd;
    nv.title = "NVIDIA - Display - 32.0.15.7000";
    nv.hardware_id = "pci\\ven_10de&dev_2c02";
    nv.update_id = "1f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6";
    vm.updates.scan.updates = {amd, nv};
    const auto rows = updates_vendor_rows(vm);
    EF_CHECK(!rows.empty() && rows[0].wu == std::optional<std::size_t>(1) && !rows[0].local);
    FakeHandlers on;
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::UpdatesInstall, 1), vm, on));
    EF_CHECK(on.calls.size() == 1 && on.calls[0] == "updates_install:1");
}

// Spec H2: "Kur" yalniz listedeki Windows Update onerisi icin cagrilir (indeks scan.updates'tir); liste disi indeks yok sayilir.
EF_TEST(ui_dispatch_updates_install_only_for_listed_update) {
    ViewModel vm;
    vm.updates.scan.state = ef::core::drivers::ScanState::Found;
    vm.updates.scan.updates.resize(2);
    FakeHandlers on;
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::UpdatesInstall, 1), vm, on));
    EF_CHECK(dispatch_row_hit(hit_of(HitKind::UpdatesInstall, 2), vm, on));  // liste disi
    EF_CHECK(on.calls.size() == 1 && on.calls[0] == "updates_install:1");
    EF_CHECK(hit_automation_id(hit_of(HitKind::UpdatesInstall, 4)) == L"updates.install.4");
    EF_CHECK(classify_hit(hit_of(HitKind::UpdatesInstall, 0)).kind == HitKind::UpdatesInstall);
}

// Spec H2 (review): kurulum bitip liste yeniden aranirken "Kur"daki odak kaybolmaz; ust karttaki dugmeye gecer.
EF_TEST(ui_updates_focus_leaves_vanished_install_button) {
    using ef::core::drivers::ScanState;
    ViewModel::UpdatesView v;
    v.scan.state = ScanState::Found;
    v.scan.updates.resize(2);
    const Hit k1 = hit_of(HitKind::UpdatesInstall, 1);
    EF_CHECK(updates_focus_after(k1, v) == k1);  // satir duruyor: odak yerinde
    EF_CHECK(updates_focus_after(hit_of(HitKind::UpdatesInstall, 2), v) == Hit::UpdatesCheck);  // satir yok
    EF_CHECK(updates_focus_after(Hit::UpdatesCancel, v) == Hit::UpdatesCheck);
    v.scan.state = ScanState::Checking;
    EF_CHECK(updates_focus_after(k1, v) == Hit::UpdatesCancel);
    EF_CHECK(updates_focus_after(Hit::UpdatesCheck, v) == Hit::UpdatesCancel);
    EF_CHECK(updates_focus_after(hit_of(HitKind::UpdatesLink, 0), v) == hit_of(HitKind::UpdatesLink, 0));
}

EF_TEST(ui_automation_ids_stable_and_unique) {
    EF_CHECK(hit_automation_id(hit_of(HitKind::ChkAction, 0)) == L"readiness.action.0");
    EF_CHECK(hit_automation_id(hit_of(HitKind::ChkAction, 7)) == L"readiness.action.7");
    EF_CHECK(hit_automation_id(hit_of(HitKind::ChkUndo, 3)) == L"readiness.undo.3");
    EF_CHECK(hit_automation_id(hit_of(HitKind::ChkKeep, 3)) == L"readiness.keep.3");
    EF_CHECK(hit_automation_id(hit_of(HitKind::CleanVolume, 0)) == L"cleanup.volume.0");
    EF_CHECK(hit_automation_id(hit_of(HitKind::CleanVolume, 1)) == L"cleanup.volume.1");
    EF_CHECK(hit_automation_id(hit_of(HitKind::CleanFiles, 2)) == L"cleanup.files.2");
    EF_CHECK(hit_automation_id(hit_of(HitKind::CleanToggle, 40)) == L"cleanup.category.40");
    EF_CHECK(hit_automation_id(hit_of(HitKind::StartupToggle, 300)) == L"startup.item.300");
    EF_CHECK(hit_automation_id(hit_of(HitKind::StartupServiceToggle, 3)) == L"startup.service.3");  // WIN-10
    EF_CHECK(hit_automation_id(hit_of(HitKind::CheckToggle, 0)) == L"check.item.0");
    EF_CHECK(hit_automation_id(hit_of(HitKind::StorageChoice, 8)) == L"measure.storage.8");
    EF_CHECK(hit_automation_id(hit_of(HitKind::GameGuide, 5)) == L"games.pro.5");
    EF_CHECK(hit_automation_id(hit_of(HitKind::GameFullscreen, 5)) == L"games.fs.5");
    EF_CHECK(hit_automation_id(hit_of(HitKind::HistorySelect, 19)) == L"history.test.19");
    EF_CHECK(hit_automation_id(Hit::HistoryExport) == L"history.export");
    EF_CHECK(hit_automation_id(hit_of(HitKind::HistoryRow, 12)) == L"history.op.12");
    EF_CHECK(hit_automation_id(consent_toggle_hit(4)) == L"settings.consent.4");
    EF_CHECK(hit_automation_id(consent_reset_hit(4)) == L"settings.reset.4");
    EF_CHECK(hit_automation_id(nav_hit(0)) == L"nav.overview");
    EF_CHECK(hit_automation_id(nav_hit(7)) == L"nav.settings");
    EF_CHECK(hit_automation_id(nav_hit(5)) == L"nav.startup");  // Sistem araclari > Baslangic alt sekmesi
    EF_CHECK(hit_automation_id(Hit::SysTabCleanup) == L"systab.cleanup");
    EF_CHECK(hit_automation_id(Hit::SettingsStrip) == L"settings.strip");
    EF_CHECK(hit_automation_id(Hit::SettingsCleanupHint) == L"settings.cleanup_hint");  // R-C2
    EF_CHECK(hit_automation_id(Hit::TopbarStatus) == L"topbar.status");
    EF_CHECK(hit_automation_id(Hit::OpenReadiness) == L"overview.open_readiness");
    EF_CHECK(hit_automation_id(Hit::GamesWhy) == L"games.why");
    EF_CHECK(hit_automation_id(Hit::HistoryFilterAll) == L"history.filter.all");
    EF_CHECK(hit_automation_id(Hit::HistoryFilterPerf) == L"history.filter.perf");
    EF_CHECK(hit_automation_id(Hit::HistoryFilterFixes) == L"history.filter.fixes");
    EF_CHECK(hit_automation_id(Hit::HistoryFilterStartup) == L"history.filter.startup");
    // UI-11/R33 + R21 + R20: yeni sabit kimliklerin UIA AutomationId'leri sabit ve benzersiz.
    EF_CHECK(hit_automation_id(Hit::AppAnimations) == L"settings.animations");
    EF_CHECK(hit_automation_id(Hit::SupportDumps) == L"settings.support.dumps");
    EF_CHECK(hit_automation_id(Hit::SupportCreate) == L"settings.support.create");
    EF_CHECK(hit_automation_id(Hit::SettingsUpdateCheck) == L"settings.update.check");
    EF_CHECK(hit_automation_id(Hit::SettingsLanguage) == L"settings.language");
    EF_CHECK(hit_automation_id(Hit::Toggle) == L"perf.toggle");
    EF_CHECK(hit_automation_id(Hit::ChkRescan) == L"readiness.rescan");
    EF_CHECK(hit_automation_id(Hit::CleanScanCancel) == L"cleanup.scan_cancel");
    EF_CHECK(hit_automation_id(Hit::CleanStorageSettings) == L"cleanup.storage_settings");
    EF_CHECK(hit_automation_id(Hit::CleanYes) == L"cleanup.yes");
    EF_CHECK(hit_automation_id(Hit::None) == L"unknown");
    EF_CHECK(hit_automation_id(static_cast<Hit>(199)) == L"unknown");  // sabit blokta adsiz kimlik
    // Tum dinamik kimliklerin AutomationId'leri birbirinden farkli (ayni ad iki ogeye gitmez).
    std::set<std::wstring> seen;
    std::size_t n = 0;
    for (const auto& r : kHitRanges) {
        if (!r.uia_prefix) continue;
        for (int i = 0; i < r.count; ++i, ++n) seen.insert(hit_automation_id(hit_of(r.kind, static_cast<std::size_t>(i))));
    }
    EF_CHECK(seen.size() == n);
}

// UI-16: kFixedHits envanteri sabit bloktaki adlandirilmis kontrollerin tam listesidir. Her ogenin UIA
// AutomationId'si vardir; 100..299 icinde UIA kimligi olan ama listede olmayan kimlik yoktur. Boylece yeni
// bir sabit kontrol sessizce envanter disinda kalamaz.
EF_TEST(ui_fixed_hit_inventory_matches_automation_ids) {
    static_assert(std::size(kFixedHits) > 0);
    std::set<std::wstring> ids;
    for (const Hit h : kFixedHits) {
        EF_CHECK(classify_hit(h).kind == HitKind::Fixed);
        const std::wstring id = hit_automation_id(h);
        EF_CHECK(id != L"unknown");
        ids.insert(id);
    }
    EF_CHECK(ids.size() == std::size(kFixedHits));
    static_assert(kHitRanges[1].kind == HitKind::Fixed && kHitRanges[1].base == 100 && kHitRanges[1].count == kFixedHitCount);
    for (int v = 100; v < 100 + kFixedHitCount; ++v) {
        const Hit h = static_cast<Hit>(v);
        if (hit_automation_id(h) == L"unknown") continue;
        bool listed = false;
        for (const Hit f : kFixedHits) listed = listed || f == h;
        EF_CHECK(listed);
    }
}

EF_TEST(ui_hitmap_ignores_out_of_block_rows) {
    HitMap m;
    m.add(hit_of(HitKind::ChkAction, static_cast<std::size_t>(kMaxReadinessRows)), {0, 0, 10, 10}, L"x", AccRole::Button);
    m.add_progress(hit_of(HitKind::Progress, 1000), {0, 0, 10, 10}, L"p", 50);
    EF_CHECK(m.items.empty() && m.meta.empty());
}

// Islem Gecmisi filtresi: journal kapsamina gore suzer, sirayi korur; bilinmeyen kapsam Performans sayilir.
EF_TEST(history_filter_by_scope) {
    ef::core::recovery::HistoryView v;
    for (const char* sc : {"user", "fixes", "startup", "machine", "machine_fixes", "startup"}) {
        ef::core::recovery::HistoryTxRow r;
        r.scope = sc;
        v.rows.push_back(r);
    }
    EF_CHECK((history_filter_rows(v, HistoryFilter::All) == std::vector<std::size_t>{0, 1, 2, 3, 4, 5}));
    EF_CHECK((history_filter_rows(v, HistoryFilter::Perf) == std::vector<std::size_t>{0, 3}));
    EF_CHECK((history_filter_rows(v, HistoryFilter::Fixes) == std::vector<std::size_t>{1, 4}));
    EF_CHECK((history_filter_rows(v, HistoryFilter::Startup) == std::vector<std::size_t>{2, 5}));
    EF_CHECK(history_filter_match(HistoryFilter::Perf, "unknown_scope"));
    EF_CHECK(history_filter_rows(ef::core::recovery::HistoryView{}, HistoryFilter::Fixes).empty());
    EF_CHECK(history_filter_hit(HistoryFilter::Startup) == Hit::HistoryFilterStartup);
}
