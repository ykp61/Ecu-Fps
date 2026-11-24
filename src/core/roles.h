#pragma once
// SEC-01: ayni EXE'nin rolleri ve kati komut satiri sozlesmesi. Parametre ile serbest komut/yol/
// registry anahtari verilemez; yalniz rol + ust surec kimligi + tek kullanimlik nonce.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"

namespace ef::core {

// Yeni roller sona eklenir (mevcut degerler/adlar sabit). Setup: kurulum penceresi; SetupWorker: yukseltilmis
// kurulum yurutucusu; Service: LocalSystem yardimci hizmet; Uninstall: kaldirma (ADR-033).
enum class Role { Ui, Smoke, Broker, Recovery, BenchWorker, CaptureWorker, CleanupWorker, Setup, SetupWorker, Service, Uninstall };
std::string_view to_string(Role r) noexcept;

struct LaunchArgs {
    Role role = Role::Ui;
    std::optional<std::uint32_t> parent_pid;  // Broker/BenchWorker/CaptureWorker/CleanupWorker/SetupWorker icin zorunlu
    std::string nonce;                        // 32 hex; Broker/BenchWorker/CaptureWorker/CleanupWorker/SetupWorker icin zorunlu
    bool tray = false;                        // Ui: pencere gizli, tepside basla (otomatik baslatma)
    std::string updated_from;                 // Ui: zorunlu guncellemeyle baslatildi; eski surum (yalniz rakam ve nokta)
    bool bare = false;                        // komut satiri bos/yalniz bosluk (kurulum yonlendirmesi icin)
};

// Bos -> Ui (bare). "--smoke" -> Smoke. "--recovery" -> Recovery. "--updated-from=<surum>" -> Ui (zorunlu guncelleme
// sonrasi yeni surum; surum yalniz rakam ve nokta, en fazla 32 karakter). "--setup" -> Setup, "--uninstall" ->
// Uninstall, "--service" -> Service (her biri tek basina).
// "--role=broker|bench|capture|cleanup|setup --parent=<pid> --nonce=<32hex>" (sira serbest, tekrar yok, bilinmeyen
// anahtar yok). CaptureWorker, CleanupWorker ve SetupWorker yalniz bu ucluyle baslatilir; serbest yol/komut yok.
contracts::Result<LaunchArgs> parse_launch_args(std::wstring_view command_line);
std::wstring build_launch_args(const LaunchArgs& a);

// Saf: bos komut satiriyla kurulum klasoru disindan acilan Ui -> Setup; diger her durumda a.role
// (--tray/--updated-from her yerde Ui kalir).
Role effective_role(const LaunchArgs& a, bool running_from_install_dir) noexcept;

bool is_valid_nonce(std::string_view s) noexcept;

// Cikis kodlari (testler ve ust surec icin sozlesme)
inline constexpr int kExitOk = 0;
inline constexpr int kExitBadArgs = 2;
inline constexpr int kExitNotElevated = 10;
inline constexpr int kExitUnexpectedlyElevated = 11;
inline constexpr int kExitParentInvalid = 12;
inline constexpr int kExitHandshakeFailed = 13;
inline constexpr int kExitUntrusted = 14;
inline constexpr int kExitAlreadyRunning = 15;
inline constexpr int kExitRecoveryNeedsUser = 20;

// ---- Yukseltme akisi (platformdan bagimsiz; testte sahte baslatici) ----
enum class ElevationOutcome { Started, Denied, Failed };
std::string_view to_string(ElevationOutcome o) noexcept;

class ElevationLauncher {
public:
    virtual ~ElevationLauncher() = default;
    // Ayni EXE'yi yonetici olarak verilen argumanlarla baslatir.
    virtual ElevationOutcome launch_elevated(const LaunchArgs& args, std::string& detail) = 0;
};

// Yetkili islem istegi: once broker baslatilir; baslamazsa (UAC reddi/hata) apply ASLA cagrilmaz.
struct ElevatedRequestResult {
    ElevationOutcome outcome = ElevationOutcome::Failed;
    bool applied = false;
    std::string detail;
};
ElevatedRequestResult run_elevated_request(ElevationLauncher& launcher, std::uint32_t self_pid,
                                           const std::string& nonce, const std::function<bool()>& apply);

}  // namespace ef::core
