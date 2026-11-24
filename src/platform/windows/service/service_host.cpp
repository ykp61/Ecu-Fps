#include "platform/windows/service/service_host.h"

#include <windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/logging/log.h"
#include "core/product_info.h"
#include "core/roles.h"
#include "core/service/client_check.h"
#include "core/service/service_session.h"
#include "core/setup/acl_rules.h"
#include "core/setup/layout.h"
#include "core/update/release_key.h"
#include "core/update/staged_update.h"
#include "core/update/update_check.h"
#include "platform/windows/broker_channel.h"
#include "platform/windows/catalog_signature.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/install/win_setup_ops.h"
#include "platform/windows/known_roots.h"
#include "platform/windows/legacy_upgrade_system.h"
#include "platform/windows/pipe.h"
#include "platform/windows/service/failure_actions.h"
#include "platform/windows/win_env.h"
#include "platform/windows/service/client_facts.h"
#include "platform/windows/service/update_store.h"
#include "platform/windows/win_clock.h"

namespace ef::platform::service {

namespace {

constexpr std::uint32_t kAcceptSliceMs = 500;
constexpr std::uint32_t kSendTimeoutMs = 5000;
constexpr DWORD kPollMs = 50;
constexpr unsigned kLockWaitMs = 10000;
// SCM'ye bildirilen hizmete ozgu cikis kodlari (ERROR_SERVICE_SPECIFIC_ERROR ile).
constexpr DWORD kStopWrongLocation = 1;
constexpr DWORD kStopPipeUnavailable = 2;
constexpr DWORD kStopNoEvent = 3;
// Spec 7: guncelleme degisimi. SCM hata eylemleri (WinSetupOps: sifir olmayan cikista da yeniden baslat) hizmeti
// kurulum yolundaki EXE ile yeniden baslatir: saglikli yeni surume (UiHealthy) veya geri alinmis eski surume.
constexpr DWORD kStopUpdateRestart = 4;
constexpr DWORD kStopUpdateRolledBack = 5;
constexpr DWORD kTickMs = 1000;

SERVICE_STATUS_HANDLE g_status = nullptr;
HANDLE g_stop = nullptr;
DWORD g_checkpoint = 0;

void set_status(DWORD state, DWORD specific = 0, DWORD wait_hint = 0) {
    if (!g_status) return;
    SERVICE_STATUS s{};
    s.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s.dwCurrentState = state;
    s.dwControlsAccepted = state == SERVICE_RUNNING ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN) : 0;
    s.dwWin32ExitCode = specific ? ERROR_SERVICE_SPECIFIC_ERROR : NO_ERROR;
    s.dwServiceSpecificExitCode = specific;
    s.dwWaitHint = wait_hint;
    s.dwCheckPoint = state == SERVICE_RUNNING || state == SERVICE_STOPPED ? 0 : ++g_checkpoint;
    (void)SetServiceStatus(g_status, &s);
}

DWORD WINAPI control_handler(DWORD control, DWORD, LPVOID, LPVOID) {
    switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
        case SERVICE_CONTROL_PRESHUTDOWN:
            set_status(SERVICE_STOP_PENDING, 0, 15000);
            if (g_stop) SetEvent(g_stop);
            return NO_ERROR;
        case SERVICE_CONTROL_INTERROGATE: return NO_ERROR;
        default: return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

bool stopping() { return g_stop && WaitForSingleObject(g_stop, 0) == WAIT_OBJECT_0; }

// Mevcut Logger; kullanici yok, cikti hata ayiklayici kanalina (dosya yazilmaz). Istemciye ayrinti gonderilmez.
class DebugSink final : public core::logging::Sink {
public:
    void write_line(const std::string& line) override { OutputDebugStringA((line + "\n").c_str()); }
};

// Hizmet genelinde tek guncelleme koordinatoru; baglanti is parcacigi ve saniyelik zamanlayici ayni kilitle kullanir.
struct UpdateRuntime {
    explicit UpdateRuntime(const core::setup::InstallLayout& layout)
        : sink(layout.updates_dir),
          host(layout.update_state, sink),
          coord(base_context(), release_keys(), release_verifier(), sink, host, win_swap_fs(), layout.swap) {}
    std::mutex mu;
    FileStageSink sink;
    FileUpdateHost host;
    core::update::UpdateCoordinator coord;
    std::atomic<bool> restart{false};

    static core::catalog::ReleaseContext base_context() {
        core::catalog::ReleaseContext c;
        const auto v = core::product_version();
        c.current_version = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);  // hizmetin KENDI surumu
        c.publisher = "Ecu Fps";
        const auto os = inventory::read_os();
        c.os_build = os.build.has() ? *os.build.value : 0;  // okunamazsa OsTooOld: degisim yapilmaz
        c.trusted_release_key = core::update::has_release_key();
        return c;
    }
    static core::catalog::KeyLookup release_keys() {
        return [](std::string_view domain, std::string_view key_id) {
            return domain == core::update::kReleaseDomain && key_id == core::update::kReleaseKeyId ? std::string(core::update::kReleasePublicBlob)
                                                                                                   : std::string();
        };
    }
    static core::catalog::RawVerifier release_verifier() {
        return [](std::string_view blob, std::string_view msg, std::string_view sig) { return verify_ecdsa_p256(blob, msg, sig); };
    }
};

// Tek baglanti: dogrulama -> makine kilidi -> yurutucu -> oturum basi uzlastirma -> Hello/istekler -> Bye/zaman asimi.
void serve_connection(PipeServer& pipe, const core::setup::InstallLayout& layout, core::logging::Logger& log, UpdateRuntime& upd) {
    auto client = gather_client(pipe);
    const auto self = gather_self(layout);  // her baglantida yeniden: kurulum yolundaki dosya + ACL
    const auto verdict = core::service::check_client(client.facts, self);
    if (verdict != core::service::ClientVerdict::Allow || !client.process) {
        log.warn("service", "client denied: " + std::string(core::service::to_string(verdict)));
        return;  // ayrinti istemciye gonderilmez; cagiran baglantiyi keser
    }
    const auto lock = MachineOpsLock::try_acquire(kLockWaitMs);
    AdminIdentity id;
    id.actor_sid = core::setup::kSidSystem;
    id.target_user_sid = client.facts.user_sid;  // oyun kapisi ve kitaplik bu kullanici icin
    id.client_process = client.process->handle();
    id.machine_lock_held = lock != nullptr;
    auto runtime = make_admin_runtime(id);
    (void)runtime->begin_session();
    // Yalniz AdminExecutor::handle (izin listesi) ve guncelleme cerceveleri (8..11; koordinator her seyi yeniden dogrular).
    // UpdateHealthy yalniz burada, yani check_client'tan gecmis kurulu istemciden gelebilir.
    // ADR-037: imzali "halt": true manifestli UpdateBegin koordinatore ulasmadan reddedilir (acil fren, savunma derinligi).
    core::service::ServiceSession sess(client.facts.pid, runtime->executor().handler(),
                                       core::service::halt_guarded(
                                           [&upd](const core::ipc::Frame& in) {
                                               const std::lock_guard<std::mutex> g(upd.mu);
                                               return upd.coord.on_frame(in);
                                           },
                                           UpdateRuntime::release_keys(), UpdateRuntime::release_verifier()));
    core::service::ConnectionTimer timer(GetTickCount64());
    while (!sess.closed() && !stopping() && !client.process->exited()) {
        if (timer.expired(GetTickCount64(), sess.ready())) {
            log.info("service", "connection timeout");
            break;
        }
        auto f = pipe.poll_frame();
        if (!f.ok()) break;
        if (!f.value()) {
            if (g_stop) (void)WaitForSingleObject(g_stop, kPollMs);
            continue;
        }
        const auto out = sess.on_frame(*f.value());
        const bool sent = pipe.write_frame(out, kSendTimeoutMs).ok();
        timer.frame(GetTickCount64());
        bool stop = false;
        {
            const std::lock_guard<std::mutex> g(upd.mu);
            stop = upd.coord.stop_requested();
        }
        if (stop || !sent) break;
    }
    bool stop = false;
    {
        const std::lock_guard<std::mutex> g(upd.mu);
        upd.coord.on_disconnect();  // yarim aktarim atilir
        stop = upd.coord.stop_requested();
    }
    // UiHealthy kalici yazildiysa yanit gonderilemese de durulur: aksi halde durum UiHealthy'de takili kalir.
    if (stop) {  // yeni surum UI'si saglikli: hizmet yeni EXE ile yeniden baslamak icin durur
        log.info("service", "update healthy: restarting on the new exe");
        upd.restart = true;
        if (g_stop) SetEvent(g_stop);
    }
    if (!sess.last_error().empty()) log.warn("service", "session closed: " + sess.last_error());
}

// Hizmet her acildiginda kendi SCM hata eylemlerini dogrular; sapma varsa onarir (UAC yok: LocalSystem kendi hizmeti).
// Boylece gunde 3. durus / eski kurulumdaki 2-RESTART politikasi hizmeti kalici durdurmaz. Journal yok; yalniz log.
void repair_own_failure_actions(core::logging::Logger& log) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return;
    const std::wstring name(core::setup::kServiceName);
    SC_HANDLE svc = OpenServiceW(scm, name.c_str(), SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG);
    if (svc) {
        bool repaired = false;
        DWORD err = 0;
        if (!ensure_failure_config(svc, &repaired, &err)) log.warn("service", "failure actions could not be verified (" + std::to_string(err) + ")");
        else if (repaired) log.info("service", "failure actions repaired (3x restart, reset 3600 s)");
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
}

void WINAPI service_main(DWORD, LPWSTR*) {
    const std::wstring name(core::setup::kServiceName);
    g_status = RegisterServiceCtrlHandlerExW(name.c_str(), control_handler, nullptr);
    if (!g_status) return;
    set_status(SERVICE_START_PENDING, 0, 10000);
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stop) {
        set_status(SERVICE_STOPPED, kStopNoEvent);
        return;
    }
    DebugSink sink;
    WinClock clock;
    core::logging::Logger log(sink, clock, {});
    const auto layout = core::setup::make_layout(known_roots());
    // Yalniz kurulum yolundaki EXE hizmet olarak calisir (kopyalanmis/tasinmis goruntu hizmet vermez).
    if (!core::setup::is_installed_location(normalized_self_exe(), layout)) {
        log.error("service", "service image is not the installed exe");
        set_status(SERVICE_STOPPED, kStopWrongLocation);
        CloseHandle(g_stop);
        g_stop = nullptr;
        return;
    }
    // Spec 7: degisim gozetmeni (ag yok). Swapped iken yeniden basladiysak (cokme/guc kaybi) eski EXE geri gelir ve
    // hizmet eski ikiliye yeniden baslamak icin sifir olmayan cikisla durur; UiHealthy ise yedek silinir (onay).
    repair_own_failure_actions(log);
    UpdateRuntime upd(layout);
    {
        core::update::SupervisorAction a;
        {
            const std::lock_guard<std::mutex> g(upd.mu);
            a = upd.coord.start();
        }
        if (a == core::update::SupervisorAction::RollBack) {
            log.warn("service", "update not confirmed healthy: old exe restored");
            set_status(SERVICE_STOPPED, kStopUpdateRolledBack);
            CloseHandle(g_stop);
            g_stop = nullptr;
            return;
        }
        if (a == core::update::SupervisorAction::Commit) log.info("service", "update committed");
    }
    // Guncelleme sonrasi: Uygulamalar listesindeki DisplayVersion yalniz kurulumda yazilirdi (bayat kalirdi). Hizmet yeni EXE ile
    // (UiHealthy -> yeniden baslama) acildiginda bizim ARP kaydinin yalniz DisplayVersion'u calisan surume cekilir; geri okunur.
    {
        const auto v = core::product_version();
        const std::wstring ver = std::to_wstring(v.major) + L"." + std::to_wstring(v.minor) + L"." + std::to_wstring(v.patch);
        bool changed = false;
        std::wstring prior;
        const auto st = platform::install::refresh_arp_display_version(layout, ver, &changed, &prior);
        if (!st) log.warn("service", "ARP DisplayVersion refresh failed");
        else if (changed) log.info("service", "ARP DisplayVersion refreshed: " + platform::to_utf8(prior) + " -> " + platform::to_utf8(ver));
    }
    // Spec 5: ACL guvensizse hizmet calisir ama her istegi reddeder (check_client her baglantida yeniden denetler).
    if (!gather_self(layout).install_dir_acl_ok) log.error("service", "install dir ACL insecure: all requests will be denied");
    {
        const AdminCom com;  // SRSetRestorePointW icin belgelenen COM guvenligi; bu is parcacigi boyunca
        auto srv = PipeServer::create_service(std::wstring(core::setup::kPipeName));
        if (!srv.ok()) {
            // Ad onceden alinmis (gasp) veya olusturulamadi: hizmet durur, istemci UAC broker yoluna doner.
            log.error("service", "pipe unavailable: " + srv.error().detail);
            set_status(SERVICE_STOPPED, kStopPipeUnavailable);
            CloseHandle(g_stop);
            g_stop = nullptr;
            return;
        }
        auto& pipe = *srv.value();
        set_status(SERVICE_RUNNING);
        HANDLE stop_ev = g_stop;
        std::thread ticker([&upd, &log, stop_ev] {
            while (WaitForSingleObject(stop_ev, kTickMs) == WAIT_TIMEOUT) {
                const std::lock_guard<std::mutex> g(upd.mu);
                if (upd.coord.tick() == core::update::SupervisorAction::RollBack) log.warn("service", "update health timeout: old exe restored");
            }
        });
        // T3b (ADR-043 ek): onceki urundan yukseltme sonrasi eski artefakt temizligi (yalniz kalici "healthy" kaydiyla;
        // kayit yoksa hemen biter). Pipe sunumunu bekletmez; durdurma olayinda yeniden denemeler biter.
        std::thread legacy_cleanup([&log, stop_ev] { run_legacy_cleanup(stop_ev, [&log](const std::string& l) { log.info("service", l); }); });
        while (!stopping()) {
            const auto acc = pipe.accept(kAcceptSliceMs);
            if (!acc.ok()) {
                pipe.disconnect();  // istemci erken kapandiysa (ERROR_NO_DATA) ornek yeniden kullanilabilir olsun
                if (acc.error().detail != "accept timeout") (void)WaitForSingleObject(g_stop, 200);
                continue;
            }
            serve_connection(pipe, layout, log, upd);  // baglantilar sirayla
            pipe.disconnect();
        }
        SetEvent(stop_ev);
        ticker.join();
        legacy_cleanup.join();
    }
    set_status(SERVICE_STOPPED, upd.restart ? kStopUpdateRestart : 0);
    CloseHandle(g_stop);
    g_stop = nullptr;
}

}  // namespace

int run_service() {
    // T3b (ADR-043 ek): ham goruntu yolu ONCEKI urunun kurulu EXE'si ise (eski hizmet adiyla SCM yeniden baslatti) ayri
    // giris: kimlik/yol dogrulamasi + gozetimsiz yukseltme, pipe sunumu yok. Yeni kurulum yolunda bu dal hic girilmez.
    if (legacy_service_dispatch_candidate()) return run_legacy_service_host();
    std::wstring name(core::setup::kServiceName);
    SERVICE_TABLE_ENTRYW table[] = {{name.data(), service_main}, {nullptr, nullptr}};
    // SCM disindan calistirilirsa (ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) hicbir sey yapilmaz.
    if (!StartServiceCtrlDispatcherW(table)) return core::kExitBadArgs;
    return core::kExitOk;
}

}  // namespace ef::platform::service
