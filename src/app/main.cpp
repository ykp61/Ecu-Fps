// Ecu Fps giris noktasi. Roller (SEC-01) ve ana pencere (UI-01).
// Normal kullanici duzeyinde acilir; ekran salt okuma verisi gosterir, sistem ayari degistirmez.

#include <windows.h>

#include <memory>
#include <string>

#include "app/adapter_set.h"
#include "app/app.h"
#include "app/setup_app.h"
#include "core/i18n/catalog.h"
#include "core/product_info.h"
#include "core/roles.h"
#include "core/security/trust.h"
#include "core/setup/layout.h"
#include "core/recovery/service.h"
#include "platform/windows/broker_channel.h"
#include "platform/windows/capture_channel.h"
#include "platform/windows/cleanup_channel.h"
#include "platform/windows/elevation.h"
#include "platform/windows/install/setup_worker.h"
#include "platform/windows/known_roots.h"
#include "platform/windows/legacy_health_signal.h"
#include "platform/windows/legacy_migration.h"
#include "platform/windows/recovery_host.h"
#include "platform/windows/service/service_host.h"
#include "platform/windows/games/live_permit.h"
#include "platform/windows/signature.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/vendor_runtime.h"
#include "platform/windows/win_env.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmd_line, int show) {
    namespace core = ef::core;
    // GPU-01: DLL arama yalniz System32; EXE/calisma klasorundeki taklit DLL'ler aranmaz.
    ef::platform::harden_dll_search();
    auto args = core::parse_launch_args(cmd_line ? cmd_line : L"");
    if (!args.ok()) return core::kExitBadArgs;
    const auto& a = args.value();
    // T3c (ADR-044): eski adli kurulum yolundan baslatma (gecis yayini EXE'yi eski yolda degistirdi). Eski kisayol (bos)
    // ve eski Run degeri (--tray) kurulum penceresi DEGIL Ui acar; yukseltme kaydi varken --uninstall reddedilir (21).
    const auto legacy_route = ef::platform::legacy_launch_route(cmd_line ? cmd_line : L"");
    if (legacy_route == core::legacy::LegacyRoute::RefuseUpgradePending) return core::legacy::kExitLegacyUpgradePending;
    // ADR-033: bos komut satiriyla kurulum klasoru disindan acilis kurulum penceresidir. Kurulum yeri yalniz bos
    // acilista sorgulanir (salt okuma); diger argumanlar her yerde ayni rolu verir. EF_SETUP_ROLE=0 (ctest pencere
    // testleri derleme klasorunden calisir) bos acilisi Ui birakir; --setup her durumda kurulum penceresidir.
    const bool from_install_dir =
        !a.bare || legacy_route == core::legacy::LegacyRoute::ForceUi || core::setup::is_installed_location(ef::platform::normalized_self_exe(),
                                                      core::setup::make_layout(ef::platform::known_roots()));
    auto role = core::effective_role(a, from_install_dir);
    if (role == core::Role::Setup && a.role == core::Role::Ui && (!core::setup::kSetupRoleEnabled || ef::app::setup_role_disabled_by_env()))
        role = core::Role::Ui;
    // Kullanici metinleri gomulu katalogdan gelir (FOUND-06).
    // Denetim 5.2: dil = Ayarlar'daki tercih, yoksa Windows GORUNTU dili (onceden bolge bicimi okunuyordu: Turkce bolge
    // + Ingilizce Windows Turkce, tersi Ingilizce aciliyordu).
    const auto load_catalog = [] {
        return ef::core::i18n::Catalog::embedded(
            ef::core::i18n::resolve_ui_locale(ef::app::saved_ui_language(), ef::app::windows_ui_language()));
    };
    switch (role) {
        case core::Role::Setup: {
            // Spec 3: kurulum penceresi (yukseltilmemis; kendi tek ornek kilidi). Yukseltme yalniz Kur -> Evet -> UAC.
            // R-F1: kurulum penceresi de veri klasorune (tercih, setup\) yazar; eski veri gocu ondan ONCE calisir.
            (void)ef::platform::run_legacy_user_migration();
            auto catalog = load_catalog();
            if (!catalog.ok()) return 6;
            return ef::app::run_setup(instance, a, catalog.value());
        }
        case core::Role::Uninstall:
            // ADR-033 spec 8 (ARP / --uninstall): sessiz kaldirma YOK. Normal Ui acilir, Ayarlar sayfasinda ayni
            // Kaldir onayi (Evet/Hayir) gosterilir; Evet -> geri yukleme, tek UAC, program + veri kaldirma (App).
            break;
        case core::Role::Service:
            // ADR-033: LocalSystem yardimci hizmet. SCM baslatmadiysa kExitBadArgs; kurulum yolu disinda hizmet vermez.
            return ef::platform::service::run_service();
        case core::Role::Broker: {
            // Yetkili yurutucu: yalniz UAC ile yukseltilmis ve ayni EXE'den baslatilmissa calisir.
            if (!a.parent_pid) return core::kExitBadArgs;
            if (!ef::platform::is_process_elevated()) return core::kExitNotElevated;
            const auto parent = ef::platform::verify_parent_same_exe(*a.parent_pid);
            if (!parent) return core::kExitParentInvalid;
            {
                // SEC-04: release derlemede yalniz gecerli imzali broker; dev'de imzasiz izinli ama etiketli.
                const auto sig = ef::platform::verify_file_signature(ef::platform::self_exe_path());
                const auto trust = core::security::broker_trust(core::security::kBuildPolicy, sig);
                if (!trust.allowed) return core::kExitUntrusted;
                if (trust.dev_unsigned) OutputDebugStringA(core::security::dev_bypass_marker().data());
                if (trust.unsigned_alpha) OutputDebugStringA(core::security::unsigned_alpha_marker().data());  // ADR-034
            }
            // OPT-02/OPT-10: tek istemcili pipe oturumu (nonce + PID + olusturma zamani), yalniz izin listesindeki
            // yonetici islemleri makine journal'iyla. Istemci Bye deyince veya zaman asiminda biter (yetim yok).
            return ef::platform::run_broker(a, *parent);
        }
        case core::Role::BenchWorker:
            if (!a.parent_pid || !ef::platform::parent_is_same_exe(*a.parent_pid)) return core::kExitParentInvalid;
            ef::platform::wait_parent_exit(*a.parent_pid, 0);
            return core::kExitOk;
        case core::Role::CaptureWorker: {
            // Oyun ici kare yakalama yardimcisi: ETW icin yukseltilmis, ayni EXE'den baslatilmis olmali.
            if (!a.parent_pid || !core::is_valid_nonce(a.nonce)) return core::kExitBadArgs;
            if (!ef::platform::is_process_elevated()) return core::kExitNotElevated;
            const auto parent = ef::platform::verify_parent_same_exe(*a.parent_pid);
            if (!parent) return core::kExitParentInvalid;
            {
                // SEC-04: broker ile ayni trust politikasi (release'te yalniz imzali, dev'de etiketli imzasiz).
                const auto sig = ef::platform::verify_file_signature(ef::platform::self_exe_path());
                const auto trust = core::security::broker_trust(core::security::kBuildPolicy, sig);
                if (!trust.allowed) return core::kExitUntrusted;
                if (trust.dev_unsigned) OutputDebugStringA(core::security::dev_bypass_marker().data());
                if (trust.unsigned_alpha) OutputDebugStringA(core::security::unsigned_alpha_marker().data());  // ADR-034
            }
            return ef::platform::run_capture_worker(a, *parent);
        }
        case core::Role::CleanupWorker: {
            // WIN-08 ek: yonetici temizlik yardimcisi. Ayni EXE + yukseltilmis + guvenilir olmali.
            if (!a.parent_pid || !core::is_valid_nonce(a.nonce)) return core::kExitBadArgs;
            if (!ef::platform::is_process_elevated()) return core::kExitNotElevated;
            const auto parent = ef::platform::verify_parent_same_exe(*a.parent_pid);
            if (!parent) return core::kExitParentInvalid;
            {
                const auto sig = ef::platform::verify_file_signature(ef::platform::self_exe_path());
                const auto trust = core::security::broker_trust(core::security::kBuildPolicy, sig);
                if (!trust.allowed) return core::kExitUntrusted;
                if (trust.dev_unsigned) OutputDebugStringA(core::security::dev_bypass_marker().data());
                if (trust.unsigned_alpha) OutputDebugStringA(core::security::unsigned_alpha_marker().data());  // ADR-034
            }
            return ef::platform::run_cleanup_worker(a, *parent);
        }
        case core::Role::SetupWorker: {
            // ADR-033: yukseltilmis islemsel kurulum iscisi. CleanupWorker ile ayni koruma sirasi; yalniz sabit
            // Install/Uninstall/Repair istegi, kendi goruntusunu kopyalar, her adim kurulum journal'inda.
            if (!a.parent_pid || !core::is_valid_nonce(a.nonce)) return core::kExitBadArgs;
            if (!ef::platform::is_process_elevated()) return core::kExitNotElevated;
            const auto parent = ef::platform::verify_parent_same_exe(*a.parent_pid);
            if (!parent) return core::kExitParentInvalid;
            {
                const auto sig = ef::platform::verify_file_signature(ef::platform::self_exe_path());
                const auto trust = core::security::broker_trust(core::security::kBuildPolicy, sig);
                if (!trust.allowed) return core::kExitUntrusted;
                if (trust.dev_unsigned) OutputDebugStringA(core::security::dev_bypass_marker().data());
                if (trust.unsigned_alpha) OutputDebugStringA(core::security::unsigned_alpha_marker().data());  // ADR-034
            }
            return ef::platform::install::run_setup_worker(a, *parent);
        }
        case core::Role::Smoke:
            break;
        case core::Role::Recovery: {
            // REC-05: basliksiz kurtarma (ag/lisans gerekmez). UI kurtarma ekrani ayni servisi kullanir.
            ef::app::AdapterSet adapters;  // UI ile ayni uretim adaptorleri
            // R-F1: eski veri gocu journal acilmadan once; aktarilamayan bekleyen eski kayit kullanici gerektirir.
            if (ef::platform::run_legacy_user_migration().legacy_pending()) return core::kExitRecoveryNeedsUser;
            auto host = ef::platform::open_recovery_host(adapters.registry);
            // Makine kapsamlari yalniz broker ile ayni makine kilidi tutulurken uzlastirilir (es zamanli yazici yok);
            // yukseltilmemis veya kilit alinamadiysa yalniz kullanici kapsamlari (makine kaydi UI'da Evet/Hayir ile).
            const auto machine_lock = ef::platform::MachineOpsLock::try_acquire(10000);
            // Yarim islem geri alma da yazmadir: oyun acikken veya durum dogrulanamazken yapilmaz, kayitlar korunur.
            if (ef::platform::games::LiveGamePermit permit; !permit.verify(ef::core::games::GuardedAction::SettingMutation).allowed)
                return core::kExitRecoveryNeedsUser;
            const auto summary =
                ef::core::recovery::run_startup_recovery(ef::core::recovery::scopes_for_role(host->scopes(), machine_lock != nullptr));
            return summary.needs_user ? core::kExitRecoveryNeedsUser : core::kExitOk;
        }
        case core::Role::Ui:
            break;
    }
    // SEC-05 / ADR-013: oturum basina tek ornek; ikinci baslatma mevcut pencereyi one getirir.
    std::unique_ptr<ef::platform::SingleInstance> single_instance;
    const bool smoke = a.role == core::Role::Smoke;
    if (!smoke) {
        bool already = false;
        single_instance = ef::platform::SingleInstance::acquire(already);
        if (already) {
            // B2 / bosluk 4: calisan ornek yanit verirse one gelir; cikmakta olan ornek (pencere yok / dongu bitti) icin
            // kilidin birakilmasi sinirli sure beklenir ve sonra normal baslanir. Sure dolarsa ikinci ornek acilmaz.
            if (ef::platform::resolve_second_launch(single_instance) != ef::core::RelaunchOutcome::Acquired)
                return core::kExitAlreadyRunning;
        }
    }

    // R-F1: marka degisikligi kullanici verisi gocu. Tek ornek kilidinden sonra (ikinci baslatma gocurmez), dil tercihi /
    // journal / adaptorler yeni veri klasorunu acmadan ONCE, surec basina bir kez. Smoke rolu gercek veriye dokunmaz.
    if (!smoke) (void)ef::platform::run_legacy_user_migration();

    auto catalog = load_catalog();
    if (!catalog.ok()) return 6;  // gomulu tablo bozuksa derleme/test zaten yakalar

    // UI-06: tepsi + istege bagli pencere + performans anahtari (App).
    // P9-B9 (flake kok nedeni): App iki katalog (her biri ~100 KiB, satir ici std::array<std::string>) ve cok sayida
    // alan tasir; wWinMain yiginina koymak 1 MiB'lik UI is parcacigi yiginini dist'te dil degisiminden sonra %72'ye
    // (740 KiB, TEB yuksek su isareti), debug'da %100'un uzerine (windows.first_run: 0xC00000FD) cikariyordu. Yigin yerine
    // yigin bellek (heap): ayni omur ve yikim sirasi (single_instance'tan once yok olur).
    auto app = std::make_unique<ef::app::App>(instance, a, show, std::move(catalog).value());
    // Zorunlu guncelleme: yeni surum baslatilirken kilit birakilir; kurulum geri alinirsa eski surum yeniden alir.
    app->set_instance_hooks([&single_instance] { single_instance.reset(); },
                            [&single_instance, smoke] {
                                if (smoke || single_instance) return;
                                bool already = false;
                                single_instance = ef::platform::SingleInstance::acquire(already);
                            });
    return app->run();
}
