#pragma once
// Cekirdek veri modelleri (docs/16_DATA_CONTRACTS.md). Win32 bagimsiz.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/measure.h"
#include "contracts/result.h"
#include "contracts/support.h"

namespace ef::contracts {

inline constexpr int kSchemaVersion = 1;

// ---- Ayar islemi ----
// Islem kimlikleri derlenmis listedir; dis veri yeni islem uretemez.
enum class OperationId {
    PowerModeSet,            // W01
    PowerSchemeSet,          // W02
    GameModeSet,             // W03
    GpuPreferenceSet,        // W04
    RefreshRateSet,          // W08
    VisualEffectsSet,        // W09
    ProcessPowerThrottling,  // W10
    ProcessPriority,         // W11
    StartupRunEntry,         // B01/B02
    StartupFolderItem,       // B03
    StartupTask,             // B04
    UpdatePauseSession,      // WIN-06
    VendorProfileValue,      // G01-G09
    GameConfigField,         // O01-O08
    ServiceSessionStop,      // WIN-09 (B07): oturumluk hizmet durdurma
    GamingToggleSet,         // OPT-09: Game Mode / arka plan kaydi / pencereli oyun iyilestirmesi / Windows VRR (HKCU)
    HagsSet,                 // W06 / OPT-10: HAGS tercihi (HKLM GraphicsDrivers HwSchMode; yalniz broker, yeniden baslatma)
    FastStartupSet,          // F-48: hizli baslatma tercihi (HKLM Session Manager\Power HiberbootEnabled; yalniz broker)
    AppCompatLayerSet,       // F-47: oyun EXE'si basina tam ekran iyilestirmeleri (HKCU AppCompatFlags\Layers)
    PowerSettingSet,         // F-32/F-33/F-42: etkin guc planinin AC alt ayari (boost/parking/ASPM; yalniz broker)
    MaintenanceJob,          // EF-X02: sabit bakim isi kimligi (yalniz broker; ayar degil)
    DnsServersSet,           // EF-X14: arayuz basina IPv4 DNS (yalniz broker; hazir liste)
    RestorePointCreate,      // EF-X16: sistem geri yukleme noktasi (yalniz broker; bakim isi, ayar degil)
    DriverInstall,           // Spec 2026-11-12 H2: Windows Update surucu kurulumu (yalniz broker; "wuinst:<UpdateID>:<rev>")
    VendorDriverInstall,     // ADR-032: imzasi dogrulanan resmi ekran karti paketi (yalniz broker; "vdrv:<uretici>:<16 hex>")
    BackgroundProcessLower,  // P2 WP3: arka plan surecini dusuk oncelik + EcoQoS ("bgproc:<pid>:<olusturma>:<yol ozeti 16 hex>")
    TweakValueSet,           // ADR-035/T02: derlenmis ayar katalogundaki tek kayit degeri ("tweak:<id>"; HKCU uygulamada, HKLM yalniz broker)
    ServiceStartTypeSet,     // ADR-035/T12: tablodaki Windows ozellik hizmetinin baslangic tipi Devre Disi ("svcstart:<ad>"; yalniz broker)
    DefenderExclusionSet,    // ADR-035/T11: secili oyun klasorunu Defender taramasindan dislar ("defx:<hex>", deger "1"; yalniz broker)
    GamePriorityHint,        // ADR-035/T09: izin listesindeki oyun EXE'si icin IFEO PerfOptions CpuPriorityClass=6 ("ifeoprio:<exe>", deger "above_normal"; yalniz broker)
    AppxPackageRemove,       // ADR-035/T13: derlenmis listedeki medya uygulamasini YALNIZ bu kullanici icin kaldirir ("appx:<PackageFamilyName>", deger "removed"; geri alma: aile adiyla yeniden kaydet; UAC yok)
    BootOptionSet,           // ADR-040 m.4: tablodaki tek BCD secenegi ("boot:dynamictick", deger "off"; yalniz broker; bayrakla KAPALI)
};
std::string_view to_string(OperationId id) noexcept;
std::optional<OperationId> parse_operation_id(std::string_view s) noexcept;

enum class OperationScope { CurrentUser, Machine, Process, Device, Game };
std::string_view to_string(OperationScope s) noexcept;
std::optional<OperationScope> parse_operation_scope(std::string_view s) noexcept;

struct SettingOperation {
    OperationId id;
    OperationScope scope;
    std::string target;       // dogrulanmis hedef kimligi (yol degil, typed anahtar)
    std::string new_value;    // kodlanmis deger; adaptor kendi codec'i ile cozer
    bool needs_reboot = false;
    bool affects_quality = false;
};

// ---- Islem (transaction) yasam dongusu, docs/04 ----
enum class TxState { Prepared, Applying, Applied, Verifying, Active, RollingBack, Restored, RecoveryRequired };
std::string_view to_string(TxState s) noexcept;
std::optional<TxState> parse_tx_state(std::string_view s) noexcept;
bool can_transition(TxState from, TxState to) noexcept;
bool is_terminal(TxState s) noexcept;

struct Transaction {
    std::string id;
    int schema_version = kSchemaVersion;
    std::string actor_sid;
    std::string target_sid;
    std::vector<SettingOperation> operations;  // sirali
    TxState state = TxState::Prepared;

    Status advance(TxState next);
};

// ---- Test kosusu ----
enum class RunSource { AppTest, InGame };
std::string_view to_string(RunSource s) noexcept;

// Kosu gecerliligi; gorev durumu ve ozellik hazirligindan ayridir.
enum class RunValidity { Valid, InvalidCapped, InvalidInterrupted, InvalidInsufficientSamples, InvalidClockDiscontinuity };
std::string_view to_string(RunValidity v) noexcept;
std::optional<RunValidity> parse_run_validity(std::string_view s) noexcept;

struct BenchmarkRun {
    std::string run_id;
    RunSource source = RunSource::AppTest;
    std::string scene;
    std::uint64_t seed = 0;
    int workload_version = 0;
    std::string settings_fingerprint;
    RunValidity validity = RunValidity::InvalidInsufficientSamples;
    Measurement avg_fps = Measurement::missing(Unit::Fps, MissingReason::NotSampled);
    Measurement low1_fps = Measurement::missing(Unit::Fps, MissingReason::NotSampled);
};

// ---- Teshis ----
enum class Confidence { Abstain, Low, Medium, High };
std::string_view to_string(Confidence c) noexcept;

enum class Bottleneck { Cpu, Gpu, Ram, Vram, Storage, FrameCap, Presentation, Thermal, Power, Background, Unknown };
std::string_view to_string(Bottleneck b) noexcept;

struct Diagnosis {
    Bottleneck category = Bottleneck::Unknown;
    Confidence confidence = Confidence::Abstain;
    std::string reason;                     // abstain dahil her zaman aciklama
    std::vector<std::string> evidence_refs;
    std::vector<std::string> counter_evidence;
    bool retest_requested = false;
};
// Abstain disinda kanit referansi olmayan teshis gecersizdir.
bool diagnosis_is_well_formed(const Diagnosis& d) noexcept;

// ---- Deney (A/B) ----
enum class ExperimentDecision { Improved, NoChange, Regressed, Inconclusive };
std::string_view to_string(ExperimentDecision d) noexcept;

struct Experiment {
    std::string baseline_group;
    std::string change_group;
    std::vector<std::string> baseline_runs;
    std::vector<std::string> change_runs;
    bool same_conditions = false;
    ExperimentDecision decision = ExperimentDecision::Inconclusive;
    bool rolled_back = false;
};
// Es kosul saglanmadan veya kosu sayisi yetersizken Improved karari verilemez.
bool experiment_decision_allowed(const Experiment& e, int min_runs_each) noexcept;

}  // namespace ef::contracts
