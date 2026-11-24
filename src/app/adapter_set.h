#pragma once
// Uretim adaptor seti: UI, kurtarma rolu ve kapanis ayni kaydi kullanir. Bir islem hangi
// adaptorle uygulandiysa cokme sonrasi ayni adaptorle geri alinabilmelidir (REC-05).

#include <memory>

#include "platform/windows/adapters/win_gaming.h"
#include "core/recovery/engine.h"
#include "core/adapters/windows/power.h"
#include "core/adapters/windows/background.h"
#include "core/adapters/windows/boot_options.h"
#include "platform/windows/adapters/win_boot_options.h"
#include "platform/windows/adapters/win_background.h"
#include "core/adapters/windows/display_process.h"
#include "core/adapters/windows/background_process.h"
#include "platform/windows/adapters/win_background_process.h"
#include "platform/windows/adapters/win_display_process.h"
#include "platform/windows/adapters/win_power.h"
#include "platform/windows/adapters/win_tasks_services.h"
#include "core/adapters/startup/startup.h"
#include "platform/windows/adapters/win_startup.h"
#include "core/adapters/nvidia/drs.h"
#include "core/adapters/windows/appcompat_layers.h"
#include "core/adapters/windows/gpu_preference.h"
#include "core/adapters/windows/fast_startup.h"
#include "core/adapters/windows/hags.h"
#include "core/adapters/windows/service_start_type.h"
#include "core/adapters/windows/power_subs.h"
#include "platform/windows/adapters/win_fast_startup.h"
#include "platform/windows/adapters/win_hags.h"
#include "core/adapters/windows/defender_exclusion.h"
#include "platform/windows/adapters/win_defender.h"
#include "core/adapters/windows/ifeo_priority.h"
#include "core/adapters/windows/appx_remove.h"
#include "platform/windows/adapters/win_appx.h"
#include "core/adapters/windows/tweak_adapter.h"
#include "platform/windows/adapters/win_machine_registry.h"
#include "platform/windows/adapters/win_power_subs.h"
#include "platform/windows/nvidia/nvapi_drs.h"

namespace ef::app {

struct AdapterSet {
    platform::adapters::WinPowerApi power_api;
    core::adapters::windows::PowerModeAdapter power_mode{power_api};
    core::adapters::windows::PowerSchemeAdapter power_scheme{power_api};
    // WIN-09: oturumluk hizmet durdurma. Uygulama yalniz broker (yonetici, makine kapsami) uzerinden;
    // burada kayitli olmasi cokme sonrasi kurtarmanin ayni adaptoru bulmasi icindir.
    platform::adapters::WinServiceApi service_api;
    platform::adapters::WinServiceBusyProbe service_busy;
    core::adapters::windows::ServiceSessionStopAdapter service_session_stop{service_api, service_busy};
    core::adapters::windows::ServiceStartTypeAdapter service_start_type{service_api, service_busy};
    // UI-10: W09 Windows animasyonlari/golgeleri (kullanici kapsami, yonetici gerekmez)
    platform::adapters::WinVisualEffectsApi vfx_api;
    core::adapters::windows::VisualEffectAdapter vfx{vfx_api};
    // WIN-09: baslangic girdileri (kullanici kapsami StartupApproved; HKLM yalniz broker)
    platform::adapters::WinStartupApi startup_api;
    core::adapters::startup::StartupAdapter startup{startup_api};
    // OPT-09: Windows oyun ayarlari (HKCU izin listesi)
    platform::adapters::WinUserRegistryApi user_reg;
    core::adapters::windows::GamingToggleAdapter gaming{user_reg};
    // F-47: oyun EXE'si basina tam ekran iyilestirmeleri (HKCU AppCompatFlags\Layers; UAC yok)
    core::adapters::windows::AppCompatLayerAdapter appcompat{user_reg};
    // T01: hibrit dizustu oyun EXE'si basina Grafik tercihi = Yuksek performans (HKCU; UAC yok)
    core::adapters::windows::GpuPreferenceAdapter gpu_pref{user_reg};
    // CHK: ekran yenileme hizi duzeltmesi (kalici, onayli)
    platform::adapters::WinDisplayApi display_api;
    core::adapters::windows::RefreshRateAdapter refresh{display_api, true};
    // OPT-10: yonetici gruplari. YAZMA yalniz broker'da (makine journal'i); burada kayitli olmalari plan
    // okumasi, Islem Gecmisi ve acilis uzlastirmasinin ayni adaptoru bulmasi icindir.
    platform::adapters::WinHagsApi hags_api;
    core::adapters::windows::HagsAdapter hags{hags_api};
    // F-48: hizli baslatma (makine). YAZMA yalniz broker'da; kayit kurtarma/Islem Gecmisi icin buradadir.
    platform::adapters::WinFastStartupApi fast_startup_api;
    core::adapters::windows::FastStartupAdapter fast_startup{fast_startup_api};
    // F-32/F-33/F-42: etkin planin AC alt ayarlari (makine). YAZMA yalniz broker'da.
    platform::adapters::WinPowerSubApi power_subs_api;
    core::adapters::windows::PowerSubAdapter power_subs{power_subs_api};
    platform::nvidia::LazyDrs drs_api;
    core::adapters::nvidia::DrsAdapter drs{drs_api};
    // P2: arka plan programini dusuk oncelik + EcoQoS'a alir (kullanici kapsami, yonetici yok, kapatma yok). Yazma yalniz
    // Performans Evet onayindan sonra journal'li motor uzerinden; ayni adaptor cokme sonrasi geri almayi da yapar.
    platform::adapters::WinBackgroundProcessApi bg_process_api;
    core::adapters::windows::BackgroundProcessAdapter bg_process{bg_process_api};
    // ADR-035/T02: derlenmis ayar katalogu (HKCU uygulamada; HKLM YAZMA yalniz broker'da, kayit kurtarma/okuma icin burada).
    platform::adapters::WinMachineRegistryApi machine_reg;
    core::adapters::windows::TweakAdapter tweak{user_reg, &machine_reg};
    // ADR-035/T11: Defender haric tutma (yazma/okuma yalniz broker; kurtarma/gecmis ve plan icin burada).
    platform::adapters::WinDefenderApi defender_api;
    core::adapters::windows::DefenderExclusionAdapter defender{defender_api};
    // ADR-035/T09: IFEO PerfOptions oyun onceligi (yazma yalniz broker; okuma/kurtarma/gecmis ve plan icin burada).
    core::adapters::windows::IfeoPriorityAdapter ifeo_priority{&machine_reg};
    // ADR-035/T13: medya uygulamasi kaldirma (kullanici kapsami, UAC yok; yeniden kayit ile geri alinir).
    platform::adapters::WinAppxApi appx_api;
    core::adapters::windows::AppxRemoveAdapter appx_remove{appx_api};
    // ADR-040 m.4: onyukleme secenegi (dinamik tick). Bayrakla KAPALI; YAZMA yalniz broker'da, kayit kurtarma/gecmis icin.
    platform::adapters::WinBootOptionApi boot_option_api;  // B4: okuma dogrulanmadi -> Unsupported (oge plana girmez)
    core::adapters::windows::BootOptionAdapter boot_option{boot_option_api};
    core::recovery::AdapterRegistry registry;

    AdapterSet() {
        registry.add(contracts::OperationId::BootOptionSet, &boot_option);  // ADR-040 (kurtarma/gecmis icin kayitli)
        registry.add(contracts::OperationId::TweakValueSet, &tweak);
        registry.add(contracts::OperationId::PowerModeSet, &power_mode);
        registry.add(contracts::OperationId::PowerSchemeSet, &power_scheme);
        registry.add(contracts::OperationId::ServiceSessionStop, &service_session_stop);
        registry.add(contracts::OperationId::ServiceStartTypeSet, &service_start_type);  // T12 (yazma yalniz broker'da; kurtarma/gecmis icin burada)
        registry.add(contracts::OperationId::DefenderExclusionSet, &defender);  // T11
        registry.add(contracts::OperationId::GamePriorityHint, &ifeo_priority);  // T09
        registry.add(contracts::OperationId::AppxPackageRemove, &appx_remove);  // T13
        registry.add(contracts::OperationId::VisualEffectsSet, &vfx);
        registry.add(contracts::OperationId::GamingToggleSet, &gaming);
        registry.add(contracts::OperationId::AppCompatLayerSet, &appcompat);
        registry.add(contracts::OperationId::GpuPreferenceSet, &gpu_pref);
        registry.add(contracts::OperationId::RefreshRateSet, &refresh);
        registry.add(contracts::OperationId::HagsSet, &hags);
        registry.add(contracts::OperationId::FastStartupSet, &fast_startup);
        registry.add(contracts::OperationId::PowerSettingSet, &power_subs);
        registry.add(contracts::OperationId::VendorProfileValue, &drs);
        registry.add(contracts::OperationId::BackgroundProcessLower, &bg_process);
        registry.add(contracts::OperationId::StartupRunEntry, &startup);
        registry.add(contracts::OperationId::StartupFolderItem, &startup);
    }
    AdapterSet(const AdapterSet&) = delete;
    AdapterSet& operator=(const AdapterSet&) = delete;
};

}  // namespace ef::app
