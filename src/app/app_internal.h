#pragma once
// Denetim 4.4: app.cpp alanlara bolundu; App uye islevleri app*.cpp dosyalarinda. Bu baslik yalniz bu dosyalarin
// ortak yardimcilaridir (onceden app.cpp icinde adsiz ad alanindaydi). Davranis degismedi.

#include "app/worker.h"
#include "app/app.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <psapi.h>
#include <shellapi.h>
#include <thread>
#include <tlhelp32.h>
#include <ctime>
#include "core/diagnosis/calibration.h"  // DIAG-11: uretim yayin kapisi durumu
#include "core/diagnosis/graphics_api.h"  // F-54: yuklu modullerden API siniflandirmasi
#include "core/diagnosis/calibration_export.h"  // DIAG-10: diagcal_v1 ornek uretimi
#include "core/diagnosis/components.h"          // DIAG-10: MEAS-03 ornekleme aralik kapisi
#include "core/games/config_risk.h"
#include "core/games/exe_resolver.h"
#include "core/games/process_risk.h"
#include "core/games/cs2.h"
#include "core/readiness/readiness.h"  // F-79: arka plan yigini siniflandirmasi
#include "core/insights/history_compare.h"
#include "core/insights/peer.h"
#include "core/support/bundle.h"
#include "core/measure/bench_text.h"
#include "app/endurance_report_map.h"
#include "benchmark/endurance/endurance_runner.h"
#include "benchmark/balance/balance_runner.h"  // spec 2026-11-12 A5: denge testi kosucusu
#include "benchmark/balance/balance_checks.h"  // spec 2026-11-12 C1/C2: salt okuma uyum denetimleri
#include "core/diagnosis/mismatch.h"
#include "measurement/telemetry/adapter_gpu.h"  // A3: arka plan GPU karistiricisi
#include "platform/windows/metrics/balance_check_sampler.h"
#include "platform/windows/readiness/display_reader.h"  // C3: yuksek performansli bagdastirici (LUID)
#include "core/diagnosis/balance.h"
#include "core/diagnosis/game_limit.h"  // spec 2026-11-12 B: Oyunda Test GPU mesgul orani + kare siniri suphesi
#include "core/inventory/power_security.h"
#include "platform/windows/adapters/win_power.h"
#include "app/model_loader.h"
#include "core/adapters/windows/dns.h"
#include "core/diagnosis/platform.h"
#include "core/measure/run_marker.h"
#include "core/metrics/sensor.h"
#include "core/diagnostics/incident_engine.h"
#include "platform/windows/diagnostics/event_reader.h"
#include "platform/windows/capture_channel.h"
#include "platform/windows/net/speedtest_http.h"
#include "app/readiness_collect.h"
#include "app/setup_app.h"
#include "app/update_support.h"
#include "ui/consent/fix_dialogs.h"
#include "core/brand/tray_variant.h"
#include "core/metrics/sampling.h"
#include "core/cleanup/admin_transfer.h"
#include "platform/windows/cleanup/win_cleanup.h"
#include "platform/windows/crash_handler.h"
#include "platform/windows/data_remover.h"
#include "core/setup/layout.h"
#include "platform/windows/install/program_remover.h"  // ADR-033: kurulu programin kaldirilmasi (tek UAC)
#include "platform/windows/known_roots.h"
#include "core/service/uac_copy.h"
#include "platform/windows/save_dialog.h"
#include "platform/windows/vendor_install/vendor_install_win.h"
#include "platform/windows/adapters/win_display_process.h"
#include "platform/windows/games/game_icons.h"
#include "platform/windows/games/library_scan.h"
#include "platform/windows/games/protection_services.h"
#include "platform/windows/games/steam_config.h"
#include "platform/windows/games/upscaler_scan.h"
#include "platform/windows/games/process_snapshot.h"
#include "core/perf/power_plan.h"
#include "core/perf/vfx_plan.h"
#include "core/perf/gaming_plan.h"
#include "core/perf/admin_plan.h"
#include "core/perf/admin_sequencing.h"
#include "platform/windows/metrics/net_rate_sampler.h"
#include "platform/windows/metrics/pdh_sampler.h"
#include "platform/windows/inventory/gpu_storage.h"  // F-63: adaptor ayrik VRAM toplami
#include "measurement/telemetry/process_gpu.h"       // F-63: oyun PID ayrik VRAM sayaci
#include "measurement/frame/capture_result.h"        // B3: yakalama protokolu surum kimligi
#include "core/product_info.h"
#include "core/recovery/service.h"
#include "platform/windows/elevation.h"
#include "platform/windows/inventory/cpu_sets.h"
#include "platform/windows/readiness/background_reader.h"
#include "platform/windows/readiness/storage_reader.h"  // F-81: oyun diski turu (salt okuma)
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/inventory/windows_features.h"
#include "platform/windows/journal_fs.h"
#include "platform/windows/nvidia/nvapi_drs.h"  // F-61: salt okuma GPU limit/saat telemetrisi
#include "platform/windows/run_key_store.h"
#include "platform/windows/wua/wua_search.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/win_env.h"
#include "ui/consent/consent_dialog.h"
#include "ui/brand/brand.h"
#include "ui/theme/system_palette.h"
#include "ui/theme/theme.h"

#include "app/measure_support.h"  // P9/B5: olcum yardimcilari ayri basliga tasindi

namespace ef::app::detail {

constexpr UINT kMsgWindowClosed = WM_APP + 30;
constexpr char kConsentFile[] = "consent.json";
constexpr char kPrefsFile[] = "preferences.json";
constexpr char kHistoryFile[] = "history.json";

// R21: IFileSaveDialog COM STA ister; ana is parcacigi mesaj dongusunden once ilklenir.
struct ComStaInit {
    bool ok = false;
    ComStaInit() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ok = SUCCEEDED(hr);
    }
    ~ComStaInit() {
        if (ok) CoUninitialize();
    }
    ComStaInit(const ComStaInit&) = delete;
    ComStaInit& operator=(const ComStaInit&) = delete;
};

inline std::string product_version_string() {
    const auto v = core::product_version();
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

}  // namespace ef::app::detail
