#include "core/roles.h"
#include "ef_test.h"

using namespace ef::core;
using ef::contracts::ErrorCode;

namespace {
const std::string kNonce = "0123456789abcdef0123456789abcdef";

class FakeLauncher final : public ElevationLauncher {
public:
    explicit FakeLauncher(ElevationOutcome o) : outcome(o) {}
    ElevationOutcome launch_elevated(const LaunchArgs& args, std::string& detail) override {
        ++calls;
        last = build_launch_args(args);
        detail = outcome == ElevationOutcome::Denied ? "ERROR_CANCELLED" : "";
        return outcome;
    }
    ElevationOutcome outcome;
    int calls = 0;
    std::wstring last;
};
}  // namespace

EF_TEST(role_args_default_and_simple_roles) {
    EF_CHECK(parse_launch_args(L"").value().role == Role::Ui);
    EF_CHECK(parse_launch_args(L"  ").value().role == Role::Ui);
    EF_CHECK(parse_launch_args(L"--smoke").value().role == Role::Smoke);
    EF_CHECK(parse_launch_args(L"--recovery").value().role == Role::Recovery);
    const auto tray = parse_launch_args(L"--tray");
    EF_CHECK(tray.ok() && tray.value().role == Role::Ui && tray.value().tray);
    EF_CHECK(build_launch_args(tray.value()) == L"--tray");
    EF_CHECK(!parse_launch_args(L"--tray --smoke").ok());
}

// Zorunlu guncelleme: yeni surum eski surumun numarasiyla Ui rolunde baslar; serbest metin/yol kabul edilmez.
EF_TEST(role_args_updated_from) {
    const auto u = parse_launch_args(L"--updated-from=0.1.0");
    EF_CHECK(u.ok() && u.value().role == Role::Ui && u.value().updated_from == "0.1.0" && !u.value().tray);
    EF_CHECK(build_launch_args(u.value()) == L"--updated-from=0.1.0");
    EF_CHECK(!parse_launch_args(L"--updated-from=").ok());
    EF_CHECK(!parse_launch_args(L"--updated-from=0.1.0;calc").ok());
    EF_CHECK(!parse_launch_args(L"--updated-from=C:\\x.exe").ok());
    EF_CHECK(!parse_launch_args(L"--updated-from=0.1.0 --tray").ok());
    EF_CHECK(!parse_launch_args(L"--updated-from=111111111111111111111111111111111").ok());  // 33 karakter
    EF_CHECK(parse_launch_args(L"").value().updated_from.empty());
}

EF_TEST(role_args_broker_strict) {
    auto ok = parse_launch_args(L"--role=broker --parent=1234 --nonce=0123456789abcdef0123456789abcdef");
    EF_CHECK(ok.ok());
    EF_CHECK(ok.value().role == Role::Broker);
    EF_CHECK(ok.value().parent_pid == 1234u);
    // Sira serbest
    EF_CHECK(parse_launch_args(L"--nonce=0123456789abcdef0123456789abcdef --role=bench --parent=7").ok());
    // Round trip
    const auto again = parse_launch_args(build_launch_args(ok.value()));
    EF_CHECK(again.ok() && again.value().nonce == kNonce);
}

EF_TEST(role_args_capture_worker_strict) {
    const auto ok = parse_launch_args(L"--role=capture --parent=1234 --nonce=0123456789abcdef0123456789abcdef");
    EF_CHECK(ok.ok());
    EF_CHECK(ok.value().role == Role::CaptureWorker);
    EF_CHECK(ok.value().parent_pid == 1234u);
    EF_CHECK(ok.value().nonce == kNonce);
    EF_CHECK(to_string(Role::CaptureWorker) == "capture");
    EF_CHECK(build_launch_args(ok.value()) ==
              L"--role=capture --parent=1234 --nonce=" + std::wstring(kNonce.begin(), kNonce.end()));
    // Round trip (sira serbest yazimda da ayni anlam)
    const auto swapped = parse_launch_args(L"--nonce=0123456789abcdef0123456789abcdef --role=capture --parent=1234");
    EF_CHECK(swapped.ok() && swapped.value().role == Role::CaptureWorker);
    const auto again = parse_launch_args(build_launch_args(swapped.value()));
    EF_CHECK(again.ok() && again.value().role == Role::CaptureWorker && again.value().parent_pid == 1234u);
    // Eksik parent/nonce, serbest arg, buyuk harf nonce reddedilir
    for (const wchar_t* bad : {
             L"--role=capture --nonce=0123456789abcdef0123456789abcdef",             // parent yok
             L"--role=capture --parent=1234",                                       // nonce yok
             L"--role=capture --parent=1234 --nonce=0123456789ABCDEF0123456789ABCDEF",
             L"--role=capture --parent=1234 --nonce=0123456789abcdef0123456789abcdef C:\\Windows\\x.exe",
             L"--role=capture --parent=1234 --nonce=0123456789abcdef0123456789abcdef --path=C:\\x",
             L"--role=capture --parent=1234 --nonce=0123456789abcdef0123456789abcdef --exec=cmd.exe",
         }) {
        EF_CHECK(!parse_launch_args(bad).ok());
    }
}

EF_TEST(role_args_cleanup_worker_strict) {
    const auto ok = parse_launch_args(L"--role=cleanup --parent=1234 --nonce=0123456789abcdef0123456789abcdef");
    EF_CHECK(ok.ok());
    EF_CHECK(ok.value().role == Role::CleanupWorker);
    EF_CHECK(ok.value().parent_pid == 1234u);
    EF_CHECK(ok.value().nonce == kNonce);
    EF_CHECK(to_string(Role::CleanupWorker) == "cleanup");
    EF_CHECK(build_launch_args(ok.value()) ==
              L"--role=cleanup --parent=1234 --nonce=" + std::wstring(kNonce.begin(), kNonce.end()));
    // Sira serbest + round trip
    const auto swapped = parse_launch_args(L"--nonce=0123456789abcdef0123456789abcdef --role=cleanup --parent=1234");
    EF_CHECK(swapped.ok() && swapped.value().role == Role::CleanupWorker);
    const auto again = parse_launch_args(build_launch_args(swapped.value()));
    EF_CHECK(again.ok() && again.value().role == Role::CleanupWorker && again.value().parent_pid == 1234u);
    // Eksik alan, serbest yol/komut, gecersiz nonce reddedilir
    for (const wchar_t* bad : {
             L"--role=cleanup --nonce=0123456789abcdef0123456789abcdef",  // parent yok
             L"--role=cleanup --parent=1234",                            // nonce yok
             L"--role=cleanup --parent=0 --nonce=0123456789abcdef0123456789abcdef",
             L"--role=cleanup --parent=1234 --nonce=0123456789ABCDEF0123456789ABCDEF",
             L"--role=cleanup --parent=1234 --nonce=0123456789abcdef0123456789abcdef --path=C:\\x",
             L"--role=cleanup --parent=1234 --nonce=0123456789abcdef0123456789abcdef C:\\Windows\\Temp",
         }) {
        EF_CHECK(!parse_launch_args(bad).ok());
    }
}

EF_TEST(role_args_reject_free_commands) {
    for (const wchar_t* bad : {
             L"--role=broker",                                                            // eksik
             L"--role=admin --parent=1 --nonce=0123456789abcdef0123456789abcdef",         // bilinmeyen rol
             L"--role=broker --parent=1 --nonce=0123456789ABCDEF0123456789ABCDEF",        // buyuk harf nonce
             L"--role=broker --parent=1 --nonce=0123",                                    // kisa nonce
             L"--role=broker --parent=0 --nonce=0123456789abcdef0123456789abcdef",        // pid 0
             L"--role=broker --parent=99999999999 --nonce=0123456789abcdef0123456789abcdef",
             L"--role=broker --parent=1 --nonce=0123456789abcdef0123456789abcdef --exec=cmd.exe",
             L"--role=broker --role=broker --parent=1 --nonce=0123456789abcdef0123456789abcdef",
             L"--role=broker --parent=1 --nonce=0123456789abcdef0123456789abcdef C:\\Windows\\x.reg",
             L"--smoke --recovery",
             L"--elevated",
             L"--role=broker --parent=1 --nonce=0123456789abcdef0123456789abcdeğ",
         }) {
        EF_CHECK(!parse_launch_args(bad).ok());
    }
    EF_CHECK(parse_launch_args(std::wstring(300, L'a')).code() == ErrorCode::ParseLimit);
}

// ADR-033: kurulum/hizmet/kaldirma rolleri; her bayrak tek basina, serbest deger yok.
EF_TEST(role_args_setup_service_uninstall_exclusive) {
    const struct { const wchar_t* flag; Role role; const char* name; } cases[] = {
        {L"--setup", Role::Setup, "setup"},
        {L"--uninstall", Role::Uninstall, "uninstall"},
        {L"--service", Role::Service, "service"},
    };
    for (const auto& c : cases) {
        const auto r = parse_launch_args(c.flag);
        EF_CHECK(r.ok() && r.value().role == c.role && !r.value().bare && !r.value().tray);
        EF_CHECK(to_string(c.role) == c.name);
        EF_CHECK(build_launch_args(r.value()) == c.flag);
        const auto again = parse_launch_args(build_launch_args(r.value()));
        EF_CHECK(again.ok() && again.value().role == c.role);
        // Tek basina olmali: tekrar, birlesim, deger veya ek arguman reddedilir
        const std::wstring f = c.flag;
        for (const std::wstring& bad : {f + L" " + f, f + L" --tray", L"--smoke " + f, f + L"=1", f + L" C:\\x.exe",
                                        f + L" --parent=1", f + L" --role=setup --parent=1 --nonce=" + std::wstring(kNonce.begin(), kNonce.end())}) {
            EF_CHECK(!parse_launch_args(bad).ok());
        }
    }
    EF_CHECK(!parse_launch_args(L"--setup --uninstall").ok());
    EF_CHECK(!parse_launch_args(L"--service --setup").ok());
    EF_CHECK(!parse_launch_args(L"--SETUP").ok());
    EF_CHECK(!parse_launch_args(L"--install").ok());
}

EF_TEST(role_args_setup_worker_strict) {
    const auto ok = parse_launch_args(L"--role=setup --parent=1234 --nonce=0123456789abcdef0123456789abcdef");
    EF_CHECK(ok.ok());
    EF_CHECK(ok.value().role == Role::SetupWorker && ok.value().parent_pid == 1234u && ok.value().nonce == kNonce);
    EF_CHECK(!ok.value().bare);
    EF_CHECK(to_string(Role::SetupWorker) == "setup-worker");
    EF_CHECK(build_launch_args(ok.value()) ==
              L"--role=setup --parent=1234 --nonce=" + std::wstring(kNonce.begin(), kNonce.end()));
    const auto swapped = parse_launch_args(L"--parent=1234 --nonce=0123456789abcdef0123456789abcdef --role=setup");
    EF_CHECK(swapped.ok() && swapped.value().role == Role::SetupWorker);
    const auto again = parse_launch_args(build_launch_args(swapped.value()));
    EF_CHECK(again.ok() && again.value().role == Role::SetupWorker && again.value().parent_pid == 1234u &&
              again.value().nonce == kNonce);
    for (const wchar_t* bad : {
             L"--role=setup",
             L"--role=setup --nonce=0123456789abcdef0123456789abcdef",                     // parent yok
             L"--role=setup --parent=1234",                                               // nonce yok
             L"--role=setup --parent=0 --nonce=0123456789abcdef0123456789abcdef",
             L"--role=setup --parent=1234 --nonce=0123456789ABCDEF0123456789ABCDEF",
             L"--role=setup --role=setup --parent=1234 --nonce=0123456789abcdef0123456789abcdef",
             L"--role=setup --parent=1 --parent=1 --nonce=0123456789abcdef0123456789abcdef",
             L"--role=setup --parent=1234 --nonce=0123456789abcdef0123456789abcdef --dir=C:\\x",
             L"--role=setup --parent=1234 --nonce=0123456789abcdef0123456789abcdef C:\\Windows\\x.exe",
             L"--role=setup-worker --parent=1234 --nonce=0123456789abcdef0123456789abcdef",
             L"--role=service --parent=1234 --nonce=0123456789abcdef0123456789abcdef",
             L"--role=uninstall --parent=1234 --nonce=0123456789abcdef0123456789abcdef",
         }) {
        EF_CHECK(!parse_launch_args(bad).ok());
    }
}

EF_TEST(role_args_bare_flag) {
    EF_CHECK(parse_launch_args(L"").value().bare);
    EF_CHECK(parse_launch_args(L"   \t ").value().bare);
    EF_CHECK(!parse_launch_args(L"--tray").value().bare);
    EF_CHECK(!parse_launch_args(L"--updated-from=0.2.1").value().bare);
    EF_CHECK(!parse_launch_args(L"--smoke").value().bare);
    EF_CHECK(!parse_launch_args(L"--recovery").value().bare);
    // Bos Ui round trip: yine bos ve bare
    const auto bare = parse_launch_args(L"").value();
    EF_CHECK(build_launch_args(bare).empty());
    EF_CHECK(parse_launch_args(build_launch_args(bare)).value().bare);
}

EF_TEST(role_effective_role_table) {
    const auto p = [](const wchar_t* s) { return parse_launch_args(s).value(); };
    // Bos acilis: kurulum klasoru disinda Setup, icinde Ui
    EF_CHECK(effective_role(p(L""), false) == Role::Setup);
    EF_CHECK(effective_role(p(L""), true) == Role::Ui);
    // --tray / --updated-from her yerde Ui
    EF_CHECK(effective_role(p(L"--tray"), false) == Role::Ui);
    EF_CHECK(effective_role(p(L"--tray"), true) == Role::Ui);
    EF_CHECK(effective_role(p(L"--updated-from=0.2.1"), false) == Role::Ui);
    EF_CHECK(effective_role(p(L"--updated-from=0.2.1"), true) == Role::Ui);
    // Acik roller degismez
    for (bool inside : {false, true}) {
        EF_CHECK(effective_role(p(L"--smoke"), inside) == Role::Smoke);
        EF_CHECK(effective_role(p(L"--recovery"), inside) == Role::Recovery);
        EF_CHECK(effective_role(p(L"--setup"), inside) == Role::Setup);
        EF_CHECK(effective_role(p(L"--service"), inside) == Role::Service);
        EF_CHECK(effective_role(p(L"--uninstall"), inside) == Role::Uninstall);
        EF_CHECK(effective_role(p(L"--role=broker --parent=1 --nonce=0123456789abcdef0123456789abcdef"), inside) == Role::Broker);
        EF_CHECK(effective_role(p(L"--role=setup --parent=1 --nonce=0123456789abcdef0123456789abcdef"), inside) ==
                  Role::SetupWorker);
    }
    // Elle kurulan Ui (bare=false) yonlendirilmez
    LaunchArgs ui;
    EF_CHECK(effective_role(ui, false) == Role::Ui);
}

EF_TEST(uac_denied_writes_nothing) {
    FakeLauncher denied(ElevationOutcome::Denied);
    int writes = 0;
    const auto r = run_elevated_request(denied, 42, kNonce, [&] { ++writes; return true; });
    EF_CHECK(r.outcome == ElevationOutcome::Denied);
    EF_CHECK(!r.applied);
    EF_CHECK(writes == 0);
    EF_CHECK(denied.calls == 1);

    FakeLauncher failed(ElevationOutcome::Failed);
    EF_CHECK(!run_elevated_request(failed, 42, kNonce, [&] { ++writes; return true; }).applied);
    EF_CHECK(writes == 0);
}

EF_TEST(uac_accepted_runs_apply_once_with_strict_args) {
    FakeLauncher ok(ElevationOutcome::Started);
    int writes = 0;
    const auto r = run_elevated_request(ok, 42, kNonce, [&] { ++writes; return true; });
    EF_CHECK(r.outcome == ElevationOutcome::Started && r.applied && writes == 1);
    EF_CHECK(ok.last == L"--role=broker --parent=42 --nonce=" + std::wstring(kNonce.begin(), kNonce.end()));
    // Gecersiz nonce ile baslatici hic cagrilmaz
    FakeLauncher never(ElevationOutcome::Started);
    EF_CHECK(!run_elevated_request(never, 42, "short", [&] { ++writes; return true; }).applied);
    EF_CHECK(never.calls == 0);
}
