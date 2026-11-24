// ADR-030 / spec 2026-11-12 G: Windows ile baslatma kurallari. Sahte depo; gercek kayit defterine dokunulmaz.
#include <map>
#include <string>

#include "core/settings/autostart.h"
#include "ef_test.h"

using namespace ef::core::settings;
using namespace ef::contracts;

namespace {
class FakeRunKey final : public RunKeyStore {
public:
    Result<std::optional<std::wstring>> read(std::wstring_view name) override {
        ++reads;
        if (fail_read) return Result<std::optional<std::wstring>>::fail(ErrorCode::IoError, "read");
        const auto it = values.find(std::wstring(name));
        if (it == values.end()) return std::optional<std::wstring>{};
        return std::optional<std::wstring>{it->second};
    }
    Status write(std::wstring_view name, std::wstring_view command) override {
        ++writes;
        if (fail_write) return Status::fail(ErrorCode::IoError, "write");
        values[std::wstring(name)] = corrupt_write ? std::wstring(L"bozuk") : std::wstring(command);
        return ok_status();
    }
    Status remove(std::wstring_view name) override {
        ++removes;
        if (fail_remove) return Status::fail(ErrorCode::IoError, "remove");
        values.erase(std::wstring(name));
        return ok_status();
    }
    bool has(std::wstring_view name) const { return values.count(std::wstring(name)) != 0; }
    std::wstring at(std::wstring_view name) const { return values.at(std::wstring(name)); }

    std::map<std::wstring, std::wstring> values;
    bool fail_read = false, fail_write = false, fail_remove = false, corrupt_write = false;
    int reads = 0, writes = 0, removes = 0;
};

const std::wstring kExe = autostart_command(L"C:\\Oyun Araclari\\EcuFps.exe");
const std::wstring kMoved = autostart_command(L"D:\\Yeni Yer\\EcuFps.exe");
}  // namespace

EF_TEST(autostart_command_quotes_exe_and_adds_tray) {
    EF_CHECK(kExe == L"\"C:\\Oyun Araclari\\EcuFps.exe\" --tray");
    EF_CHECK(kAutostartValueName == L"EcuFps");
}

EF_TEST(autostart_first_run_writes_entry_default_on_with_notice) {
    FakeRunKey k;
    Preferences p;  // dosya yok / ozellikten onceki dosya
    const auto r = autostart_on_launch(k, p, kExe);
    EF_CHECK(r.state.on && r.state.known && r.state.failure == AutostartFailure::None);
    EF_CHECK(r.prefs_changed && p.autostart_initialized && p.start_with_windows && p.autostart_notice_pending);
    EF_CHECK(k.at(kAutostartValueName) == kExe);
    // Ikinci acilis: kayit zaten dogru; yazma yok, bildirim yeniden kurulmaz.
    p.autostart_notice_pending = false;
    const int writes = k.writes;
    const auto r2 = autostart_on_launch(k, p, kExe);
    EF_CHECK(r2.state.on && !r2.prefs_changed && !p.autostart_notice_pending && k.writes == writes);
}

EF_TEST(autostart_first_run_replaces_legacy_value_name) {
    FakeRunKey k;
    k.values[std::wstring(kLegacyAutostartValueName)] = kExe;
    Preferences p;
    const auto r = autostart_on_launch(k, p, kExe);
    EF_CHECK(r.state.on && !k.has(kLegacyAutostartValueName) && k.at(kAutostartValueName) == kExe);
}

EF_TEST(autostart_toggle_off_removes_and_is_never_rewritten) {
    FakeRunKey k;
    Preferences p;
    (void)autostart_on_launch(k, p, kExe);
    const auto off = autostart_set(k, p, false, kExe);
    EF_CHECK(!off.on && off.failure == AutostartFailure::None && !k.has(kAutostartValueName));
    EF_CHECK(!p.start_with_windows && !p.autostart_notice_pending);
    // Sonraki acilislar kaydi yazmaz.
    for (int i = 0; i < 3; ++i) {
        const auto r = autostart_on_launch(k, p, kExe);
        EF_CHECK(!r.state.on && !k.has(kAutostartValueName));
    }
    // Kapaliyken disaridan geri gelen kayit acilista silinir.
    k.values[std::wstring(kAutostartValueName)] = kExe;
    EF_CHECK(!autostart_on_launch(k, p, kExe).state.on && !k.has(kAutostartValueName));
    // Yeniden acmak kaydi yazar ve dogrular.
    const auto on = autostart_set(k, p, true, kExe);
    EF_CHECK(on.on && p.start_with_windows && k.at(kAutostartValueName) == kExe);
}

EF_TEST(autostart_moved_exe_rewrites_entry_on_launch) {
    FakeRunKey k;
    Preferences p;
    (void)autostart_on_launch(k, p, kExe);
    p.autostart_notice_pending = false;
    // Ayni kayit, EXE baska yere tasindiktan sonra: eski yolu gosteriyor -> "acik" sayilmaz, yeni yola yazilir.
    EF_CHECK(!autostart_read(k, kMoved).on);
    const auto r = autostart_on_launch(k, p, kMoved);
    EF_CHECK(r.state.on && k.at(kAutostartValueName) == kMoved && p.start_with_windows && !p.autostart_notice_pending);
}

EF_TEST(autostart_external_removal_is_respected) {
    FakeRunKey k;
    Preferences p;
    (void)autostart_on_launch(k, p, kExe);
    k.values.clear();  // kullanici kaydi uygulama disinda sildi
    const auto r = autostart_on_launch(k, p, kExe);
    EF_CHECK(!r.state.on && r.prefs_changed && !p.start_with_windows && !k.has(kAutostartValueName));
}

EF_TEST(autostart_write_failure_shows_off_with_reason) {
    FakeRunKey k;
    k.fail_write = true;
    Preferences p;
    const auto r = autostart_on_launch(k, p, kExe);
    EF_CHECK(!r.state.on && r.state.known && r.state.failure == AutostartFailure::WriteFailed);
    EF_CHECK(!p.start_with_windows && !p.autostart_notice_pending && p.autostart_initialized);
    // Sonraki acilis yeniden zorlamaz (anahtar kapali); kullanici yeniden acarsa denenir.
    k.fail_write = false;
    EF_CHECK(!autostart_on_launch(k, p, kExe).state.on && !k.has(kAutostartValueName));
    k.fail_write = true;
    const auto s = autostart_set(k, p, true, kExe);
    EF_CHECK(!s.on && s.failure == AutostartFailure::WriteFailed && !p.start_with_windows);
}

EF_TEST(autostart_readback_mismatch_is_verify_failure_and_cleaned) {
    FakeRunKey k;
    k.corrupt_write = true;  // yazma "basarili" ama geri okunan deger farkli
    Preferences p;
    const auto s = autostart_set(k, p, true, kExe);
    EF_CHECK(!s.on && s.failure == AutostartFailure::VerifyFailed && !p.start_with_windows && !k.has(kAutostartValueName));
}

EF_TEST(autostart_read_failure_writes_nothing) {
    FakeRunKey k;
    k.fail_read = true;
    Preferences p;
    const auto r = autostart_on_launch(k, p, kExe);
    EF_CHECK(!r.state.known && !r.state.on && r.state.failure == AutostartFailure::ReadFailed);
    EF_CHECK(k.writes == 0 && k.removes == 0 && !p.autostart_notice_pending);
    // Okuma hatasi ilk calistirmayi tuketmez: sonraki acilista kayit okunabiliyorsa varsayilan acik uygulanir
    // (bos kayit disaridan silme sayilmaz).
    EF_CHECK(!p.autostart_initialized && !r.prefs_changed);
    k.fail_read = false;
    const auto r2 = autostart_on_launch(k, p, kExe);
    EF_CHECK(r2.state.on && p.autostart_initialized && p.start_with_windows && p.autostart_notice_pending);
    EF_CHECK(k.at(kAutostartValueName) == kExe);
}

EF_TEST(autostart_remove_failure_reports_entry_still_on) {
    FakeRunKey k;
    Preferences p;
    (void)autostart_on_launch(k, p, kExe);
    k.fail_remove = true;
    const auto s = autostart_set(k, p, false, kExe);
    EF_CHECK(s.on && s.failure == AutostartFailure::RemoveFailed && !p.start_with_windows);
}

EF_TEST(autostart_uninstall_removes_current_and_legacy_values) {
    FakeRunKey k;
    k.values[std::wstring(kAutostartValueName)] = kExe;
    k.values[std::wstring(kLegacyAutostartValueName)] = kExe;
    k.values[L"BaskaUygulama"] = L"x.exe";
    EF_CHECK(autostart_remove_all(k).ok());
    EF_CHECK(!k.has(kAutostartValueName) && !k.has(kLegacyAutostartValueName) && k.has(L"BaskaUygulama"));
    EF_CHECK(autostart_remove_all(k).ok());  // yokken de basarili
    k.values[std::wstring(kAutostartValueName)] = kExe;
    k.fail_remove = true;
    EF_CHECK(!autostart_remove_all(k).ok());
}

EF_TEST(autostart_preferences_roundtrip) {
    Preferences p;
    EF_CHECK(!p.autostart_initialized && p.start_with_windows && !p.autostart_notice_pending);
    // Baslatilmamis tercih yazilmaz: eski surumle ayni dosya.
    EF_CHECK(p.serialize().find("start_with_windows") == std::string::npos);
    p.autostart_initialized = true;
    p.start_with_windows = false;
    p.autostart_notice_pending = true;
    const auto back = Preferences::parse(p.serialize());
    EF_CHECK(back.ok() && back.value().autostart_initialized && !back.value().start_with_windows && back.value().autostart_notice_pending);
    // Ozellikten onceki dosya: ilk calistirma kurali uygulanir.
    const auto old = Preferences::parse(R"({"schema_version":1,"welcome_seen":true})");
    EF_CHECK(old.ok() && !old.value().autostart_initialized && old.value().start_with_windows);
}

EF_TEST(autostart_launch_branch_and_trace_for_each_path) {
    // Tani satiri: hangi yol izlendi ve ne goruldu (sahip kurulumu 2026-11-13; yol / kullanici adi icermez).
    {  // ilk calistirma
        FakeRunKey k;
        Preferences p;
        const auto r = autostart_on_launch(k, p, kExe);
        EF_CHECK(r.branch == AutostartBranch::FirstRunWritten && r.seen == RunValueSeen::Absent && r.first_run);
        EF_CHECK(autostart_launch_trace(r, p) ==
                  "launch: seen=absent first_run=1 branch=first_run_written on=1 failure=none pref_start_with_windows=1");
        // ikinci acilis: eslesir
        const auto r2 = autostart_on_launch(k, p, kExe);
        EF_CHECK(r2.branch == AutostartBranch::Matched && r2.seen == RunValueSeen::ThisExe && !r2.first_run);
        // tasinmis EXE
        const auto r3 = autostart_on_launch(k, p, kMoved);
        EF_CHECK(r3.branch == AutostartBranch::Rewritten && r3.seen == RunValueSeen::OtherExe && k.at(kAutostartValueName) == kMoved);
        // disaridan silindi: kural degismedi (tercih kapanir, yeniden yazilmaz) ve satirda gorunur
        k.values.clear();
        const auto r4 = autostart_on_launch(k, p, kMoved);
        EF_CHECK(r4.branch == AutostartBranch::RemovedOutside && r4.seen == RunValueSeen::Absent && !p.start_with_windows);
        EF_CHECK(!k.has(kAutostartValueName));
        EF_CHECK(autostart_launch_trace(r4, p).find("branch=removed_outside_pref_off") != std::string::npos);
        EF_CHECK(autostart_on_launch(k, p, kMoved).branch == AutostartBranch::PrefOffAbsent);
        k.values[std::wstring(kAutostartValueName)] = kMoved;
        EF_CHECK(autostart_on_launch(k, p, kMoved).branch == AutostartBranch::PrefOffRemoved && !k.has(kAutostartValueName));
    }
    {
        FakeRunKey k;
        k.fail_read = true;
        Preferences p;
        const auto r = autostart_on_launch(k, p, kExe);
        EF_CHECK(r.branch == AutostartBranch::ReadFailed && r.seen == RunValueSeen::Unreadable && r.first_run);
        EF_CHECK(!p.autostart_initialized);  // okuma hatasi ilk calistirmayi isaretlemez
        EF_CHECK(autostart_launch_trace(r, p).find("seen=unreadable") != std::string::npos);
    }
    {
        FakeRunKey k;
        k.corrupt_write = true;
        Preferences p;
        const auto r = autostart_on_launch(k, p, kExe);
        EF_CHECK(r.branch == AutostartBranch::WriteFailed && r.state.failure == AutostartFailure::VerifyFailed);
    }
    AutostartState s;
    s.on = true;
    EF_CHECK(autostart_set_trace("setup done", true, s) == "setup done: set requested=on on=1 known=1 failure=none");
    s.on = false;
    s.failure = AutostartFailure::WriteFailed;
    EF_CHECK(autostart_set_trace("app settings toggle", true, s) ==
              "app settings toggle: set requested=on on=0 known=1 failure=write");
}
