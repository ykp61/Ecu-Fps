#include "core/recovery/uninstall.h"

#include "core/recovery/conflicts.h"

namespace ef::core::recovery {

using contracts::ErrorCode;
using contracts::TxState;

namespace {
// Geri yukle + dogrula. true: hepsi geri alindi (silmeye gecilebilir).
bool restore_and_verify(const std::vector<JournalScope>& scopes, UninstallResult& r) {
    // 1) Yarim kalmislari uzlastir, sonra tum aktifleri geri al.
    const auto startup = run_startup_recovery(scopes);
    if (startup.writes_blocked) {
        r.notes.push_back("journal has an unknown newer schema; nothing removed");
        return false;
    }
    (void)restore_all_active(scopes);
    // Dis degisiklik: kullanici/baska uygulama degeri degistirmis; kaldirma onu ezmez, kaydi mevcut degerle kapatir.
    bool kept_external = false;
    for (const auto& sc : scopes) {
        if (!sc.store) continue;
        for (auto& tx : sc.store->scan().transactions)
            if (has_external_conflict(tx) && keep_external_values(std::move(tx), *sc.store).ok()) kept_external = true;
    }
    if (kept_external) r.notes.push_back("externally changed values were kept");
    // 2) Dogrula: geri alinmamis veya bozuk kayit kaldi mi? (tek kaynak: yeniden tarama; cift sayim yok)
    for (const auto& sc : scopes) {
        if (!sc.store) continue;
        const auto l = sc.store->scan();
        for (const auto& tx : l.transactions)
            if (tx.state != TxState::Restored) r.unrestored.push_back(tx.id);
        for (const auto& c : l.corrupt) r.unrestored.push_back(c);
    }
    if (!r.unrestored.empty()) {
        r.notes.push_back("some changes could not be restored; data kept so they can be retried");
        return false;  // veri SILINMEZ
    }
    r.restored_all = true;
    return true;
}
}  // namespace

UninstallResult run_uninstall(const std::vector<JournalScope>& scopes, DataRemover& remover) {
    return run_full_uninstall(scopes, remover, nullptr);
}

UninstallResult run_full_uninstall(const std::vector<JournalScope>& scopes, DataRemover& remover, ProgramRemover* program) {
    UninstallResult r;
    if (!restore_and_verify(scopes, r)) return r;

    if (program) {
        // ADR-033: kurulu program. Isci hizmet/ARP/kisayolu geri alinabilir sirayla, ProgramData'yi yalniz makine
        // journal'lari temizken, dosyalari en son siler. Basarisiz/reddedilirse Run kaydi ve kullanici verisi kalir.
        r.stopped_at = UninstallStage::RemoveProgram;
        const auto o = program->remove_program();
        if (o.denied) {
            r.notes.push_back(kNoteProgramDenied);
            return r;
        }
        // Isci baslatilamadi (UAC reddi degil) ya da kaldiramadi: "izin verilmedi" denmez, basarisiz sayilir.
        if (!o.attempted || !o.removed) {
            r.notes.push_back(kNoteProgramFailed);
            return r;
        }
        r.reboot_pending = o.reboot_pending;
        if (o.reboot_pending) r.notes.push_back(kNoteProgramRebootPending);
        r.stopped_at = UninstallStage::RemoveAutostart;
        if (auto s = remover.remove_autostart(); !s.ok()) {
            r.notes.push_back("autostart: " + s.error().detail);
            return r;
        }
        r.stopped_at = UninstallStage::RemoveUserData;
        if (auto s = remover.remove_user_data(); !s.ok()) {
            r.notes.push_back("user data: " + s.error().detail);
            return r;
        }
        r.stopped_at = UninstallStage::Done;
        r.data_removed = true;
        return r;
    }

    // 3) Tasinabilir kopya: autostart -> makine -> kullanici
    r.stopped_at = UninstallStage::RemoveAutostart;
    if (auto s = remover.remove_autostart(); !s.ok()) {
        r.notes.push_back("autostart: " + s.error().detail);
        return r;
    }
    r.stopped_at = UninstallStage::RemoveMachineData;
    if (auto s = remover.remove_machine_data(); !s.ok()) {
        // Makine verisi yonetici ister: kullanici verisini yine sil ama acikca bildir.
        if (s.code() != ErrorCode::Denied) {
            r.notes.push_back("machine data: " + s.error().detail);
            return r;
        }
        r.notes.push_back("machine data needs administrator rights and was left in place");
    }
    r.stopped_at = UninstallStage::RemoveUserData;
    if (auto s = remover.remove_user_data(); !s.ok()) {
        r.notes.push_back("user data: " + s.error().detail);
        return r;
    }
    r.stopped_at = UninstallStage::Done;
    r.data_removed = true;
    return r;
}

}  // namespace ef::core::recovery
