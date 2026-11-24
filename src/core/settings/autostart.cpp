#include "core/settings/autostart.h"

#include "core/legacy/legacy_names.h"

namespace ef::core::settings {

namespace {
using contracts::ErrorCode;

// Yaz + geri oku. Basarisizsa yarim / yanlis kayit birakilmaz (en iyi caba silme).
AutostartFailure write_verified(RunKeyStore& store, std::wstring_view command) {
    AutostartFailure f = AutostartFailure::None;
    if (!store.write(kAutostartValueName, command).ok()) {
        f = AutostartFailure::WriteFailed;
    } else {
        const auto back = store.read(kAutostartValueName);
        if (!back.ok() || !back.value() || *back.value() != command) f = AutostartFailure::VerifyFailed;
    }
    if (f != AutostartFailure::None) (void)store.remove(kAutostartValueName);
    return f;
}

// Sil + geri oku. Donus: kayit gercekten yok mu.
bool remove_verified(RunKeyStore& store, std::wstring_view name) {
    if (!store.remove(name).ok()) return false;
    const auto back = store.read(name);
    return back.ok() && !back.value();
}
}  // namespace

std::string_view to_string(AutostartBranch b) noexcept {
    switch (b) {
        case AutostartBranch::Matched: return "matched";
        case AutostartBranch::FirstRunWritten: return "first_run_written";
        case AutostartBranch::Rewritten: return "rewritten_moved_exe";
        case AutostartBranch::RemovedOutside: return "removed_outside_pref_off";
        case AutostartBranch::PrefOffRemoved: return "pref_off_removed";
        case AutostartBranch::PrefOffAbsent: return "pref_off_absent";
        case AutostartBranch::ReadFailed: return "read_failed";
        case AutostartBranch::WriteFailed: return "write_failed_pref_off";
        case AutostartBranch::RemoveFailed: return "remove_failed";
    }
    return "?";
}

std::string_view to_string(RunValueSeen v) noexcept {
    switch (v) {
        case RunValueSeen::Unreadable: return "unreadable";
        case RunValueSeen::Absent: return "absent";
        case RunValueSeen::ThisExe: return "this_exe";
        case RunValueSeen::OtherExe: return "other_exe";
    }
    return "?";
}

namespace {
std::string_view failure_text(AutostartFailure f) noexcept {
    switch (f) {
        case AutostartFailure::None: return "none";
        case AutostartFailure::ReadFailed: return "read";
        case AutostartFailure::WriteFailed: return "write";
        case AutostartFailure::VerifyFailed: return "verify";
        case AutostartFailure::RemoveFailed: return "remove";
    }
    return "?";
}
}  // namespace

std::string autostart_launch_trace(const AutostartLaunch& r, const Preferences& after) {
    std::string s = "launch: seen=";
    s += to_string(r.seen);
    s += " first_run=";
    s += r.first_run ? "1" : "0";
    s += " branch=";
    s += to_string(r.branch);
    s += " on=";
    s += r.state.on ? "1" : "0";
    s += " failure=";
    s += failure_text(r.state.failure);
    s += " pref_start_with_windows=";
    s += after.start_with_windows ? "1" : "0";
    return s;
}

std::string autostart_set_trace(std::string_view who, bool requested, const AutostartState& s) {
    std::string out(who);
    out += ": set requested=";
    out += requested ? "on" : "off";
    out += " on=";
    out += s.on ? "1" : "0";
    out += " known=";
    out += s.known ? "1" : "0";
    out += " failure=";
    out += failure_text(s.failure);
    return out;
}

std::wstring autostart_command(std::wstring_view exe_path) {
    std::wstring c = L"\"";
    c += exe_path;
    c += L"\" --tray";
    return c;
}

AutostartState autostart_read(RunKeyStore& store, std::wstring_view command) {
    AutostartState s;
    const auto v = store.read(kAutostartValueName);
    if (!v.ok()) {
        s.known = false;
        s.failure = AutostartFailure::ReadFailed;
        return s;
    }
    // Baska konumdaki eski EXE'yi gosteren kayit "acik" sayilmaz.
    s.on = v.value() && *v.value() == command;
    return s;
}

AutostartLaunch autostart_on_launch(RunKeyStore& store, Preferences& prefs, std::wstring_view command) {
    AutostartLaunch r;
    // Eski adli kayit (SEC-05) bu surumde yeni adla degisir; okunabiliyorsa ve varsa silinir.
    if (const auto legacy = store.read(kLegacyAutostartValueName); legacy.ok() && legacy.value())
        (void)store.remove(kLegacyAutostartValueName);
    // R-F1 marka degisikligi: onceki urun adiyla yazilmis Run degerleri de bizimdir (gocle gelen tercih onlari yazdi).
    // Bu acilista eski ad varsa yeni degerin yoklugu "disaridan silindi" SAYILMAZ: tercih aciksa kayit yeni ad + bu EXE
    // ile yazilir (geri okunarak), eski adlar silinir (geri okunarak). Tercih kapaliysa yalniz eski adlar silinir.
    bool old_brand_value = false;
    for (const auto name : {legacy::kOldAutostartValueName, legacy::kOldAutostartLegacyValueName})
        if (const auto v = store.read(name); v.ok() && v.value()) {
            old_brand_value = true;
            (void)remove_verified(store, name);
        }
    const bool first_run = !prefs.autostart_initialized;
    r.first_run = first_run;  // tani satiri okuma hatasinda da gercek ilk calistirma bilgisini gostersin
    const auto cur = store.read(kAutostartValueName);
    if (!cur.ok()) {  // okunamayan kayda kor yazma yok; anahtar devre disi
        // Ilk calistirma isaretlenmez: aksi halde sonraki acilista "kayit yok" disaridan silme sayilir ve varsayilan
        // acik hic uygulanmazdi.
        r.state.known = false;
        r.state.failure = AutostartFailure::ReadFailed;
        r.branch = AutostartBranch::ReadFailed;
        return r;
    }
    r.seen = !cur.value() ? RunValueSeen::Absent : *cur.value() == command ? RunValueSeen::ThisExe : RunValueSeen::OtherExe;
    if (first_run) {
        prefs.autostart_initialized = true;
        prefs.start_with_windows = true;  // varsayilan acik (ADR-030)
        r.prefs_changed = true;
    }
    const auto& value = cur.value();
    if (prefs.start_with_windows) {
        if (value && *value == command) {
            r.state.on = true;
            r.branch = AutostartBranch::Matched;
            return r;
        }
        if (!value && !first_run && !old_brand_value) {
            // Kayit uygulama disinda silindi: kullanicinin karari sayilir, yeniden yazilmaz.
            prefs.start_with_windows = false;
            r.prefs_changed = true;
            r.branch = AutostartBranch::RemovedOutside;
            return r;
        }
        // Ilk calistirma ya da tasinmis EXE: kayit bu yola (yeniden) yazilir.
        r.state.failure = write_verified(store, command);
        r.state.on = r.state.failure == AutostartFailure::None;
        r.branch = !r.state.on ? AutostartBranch::WriteFailed : first_run ? AutostartBranch::FirstRunWritten : AutostartBranch::Rewritten;
        if (r.state.on) {
            if (first_run) prefs.autostart_notice_pending = true;
        } else {
            prefs.start_with_windows = false;  // anahtar kapali gorunur; neden yazilir
            r.prefs_changed = true;
        }
        return r;
    }
    r.branch = value ? AutostartBranch::PrefOffRemoved : AutostartBranch::PrefOffAbsent;
    if (value) {
        if (!remove_verified(store, kAutostartValueName)) {
            r.state.failure = AutostartFailure::RemoveFailed;
            r.state.on = *value == command;
            r.branch = AutostartBranch::RemoveFailed;
        }
    }
    return r;
}

AutostartState autostart_set(RunKeyStore& store, Preferences& prefs, bool on, std::wstring_view command) {
    prefs.autostart_initialized = true;
    prefs.autostart_notice_pending = false;
    AutostartState s;
    if (on) {
        s.failure = write_verified(store, command);
        s.on = s.failure == AutostartFailure::None;
        prefs.start_with_windows = s.on;
        return s;
    }
    prefs.start_with_windows = false;
    if (!remove_verified(store, kAutostartValueName)) {
        s.failure = AutostartFailure::RemoveFailed;
        const auto back = store.read(kAutostartValueName);
        s.known = back.ok();
        s.on = back.ok() && back.value() && *back.value() == command;
    }
    return s;
}

contracts::Status autostart_remove_all(RunKeyStore& store) {
    const bool cur = remove_verified(store, kAutostartValueName);
    const bool legacy = remove_verified(store, kLegacyAutostartValueName);
    // R-F1: onceki urun adiyla yazilmis degerler de kaldirmada silinir.
    const bool old_a = remove_verified(store, legacy::kOldAutostartValueName);
    const bool old_b = remove_verified(store, legacy::kOldAutostartLegacyValueName);
    if (cur && legacy && old_a && old_b) return contracts::ok_status();
    return contracts::Status::fail(ErrorCode::IoError, "Run value remove");
}

}  // namespace ef::core::settings
