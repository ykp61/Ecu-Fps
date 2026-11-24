#pragma once
// R-F2 (ADR-043): eski adli 0.9.5 kurulumundan yeni markaya yukseltme — eski kurulum yerlesimi ve ESKI surumun yeni
// EXE'yi baslatma bicimleri (saf). Eski adlar yalniz legacy_names.h'den gelir.
//
// Gozlenen gercek (059b3ee3 kaynagi, app_update.cpp / service_host.cpp / staged_update.*): 0.9.5 kurulu istemcisi ayri
// bir "Setup" EXE'si BASLATMAZ. Yukseltme su zincirle olur:
//  1. Eski UI imzali zarfi + yeni EXE baytlarini eski pipe'tan eski hizmete yollar (UpdateBegin/Chunk/Commit).
//  2. Eski hizmet (LocalSystem) dogrular, durum Swapped yazar (eski ProgramData\updates\state.json) ve eski kurulum
//     yolundaki EXE'yi yerinde degistirir (exe -> exe.old yedek, exe.new -> exe).
//  3. Eski UI yeni EXE'yi ESKI yolda "--updated-from=<eski surum>" ile kullanici belirteciyle (yukseltilmemis,
//     etkilesimli oturum, calisma klasoru eski kurulum klasoru, ortam miras) baslatir ve hemen cikar; cikis kodunu
//     beklemez.
//  4. Eski hizmet 60 sn icinde ESKI pipe'tan UpdateHealthy(surum) bekler (istemci goruntusu == eski kurulu EXE,
//     Administrators uyesi, etkilesimli). Gelmezse recover_swap(false): exe.old geri gelir, hizmet 5 ile durur.
//  5. Geldiyse durum UiHealthy, hizmet 4 ile durur; SCM hata eylemi (3x RESTART 5 sn, sifir disi cikis da hata) hizmeti
//     ayni ImagePath ile ("\"<eski exe>\" --service") yeniden baslatir: artik YENI ikili, eski hizmet adiyla, SYSTEM.
//     0.9.5 mantiginda bu acilis Commit yapar (exe.old silinir).
// Yeni surum bu yuzden iki eski cagriyi tanimalidir: --updated-from (eski hizmete saglik sinyali) ve eski yoldan
// --service (gozetimsiz yukseltme: yeni kurulumu yan yana kur, saglikli olunca eskisini temizle).

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/setup/layout.h"

namespace ef::core::legacy {

// Eski adlarla kurulum yerlesimi (make_layout ile ayni turetme; machine_dir/user_dir eski veri klasoru).
setup::InstallLayout legacy_layout(const setup::KnownRoots& r);
std::wstring legacy_arp_key();  // Software\...\Uninstall\<eski anahtar>
// Eski kurulum klasorune BIZIM koydugumuz dosyalar (kurulum manifesti yoktur; setup journal'i commit'te silinir):
// EXE ve degisim kardesleri (.new, .old, .updated, .new.failed). Klasordeki baska dosyaya dokunulmaz.
std::vector<std::filesystem::path> legacy_owned_files(const setup::InstallLayout& legacy);

enum class LegacyInvocation {
    None,             // eski kurulum yolundan calismiyor: normal akis
    HealthHandshake,  // --updated-from=<surum>: eski UI baslatti; eski hizmete eski pipe'tan saglik sinyali
    ServiceHost,      // --service: SCM eski hizmet adiyla baslatti (SYSTEM): gozetimsiz yukseltme
    Tray,             // --tray: eski HKCU Run degeri
    Shortcut,         // bos: eski Baslat/masaustu kisayolu (Setup penceresi ACILMAZ; Ui)
    Uninstall,        // --uninstall: eski ARP kaydi (yukseltme sururken reddedilir)
    Setup,            // --setup
    Recovery,         // --recovery
    Other,            // eski yoldan baska/gecersiz arguman
};
std::string_view to_string(LegacyInvocation k) noexcept;

// Eski surumun yeni EXE'yi baslattigi bilinen bicimler (belge + test tablosu). spelling bos = arguman yok.
struct LegacyLaunch {
    std::wstring_view spelling;
    std::string_view launcher, token, session, cwd;
    LegacyInvocation kind;
};
inline constexpr LegacyLaunch kLegacyLaunches[] = {
    {L"--updated-from=0.9.5", "old UI launch_service_updated_exe (CreateProcessW)", "user, filtered admin, medium IL", "interactive",
     "old install dir", LegacyInvocation::HealthHandshake},
    {L"--service", "SCM restart after old service exit 4 (failure action)", "LocalSystem", "0", "%SystemRoot%\\System32",
     LegacyInvocation::ServiceHost},
    {L"--tray", "HKCU Run value (old names)", "user", "interactive", "explorer default", LegacyInvocation::Tray},
    {L"", "old Start menu / Public desktop .lnk", "user", "interactive", "old install dir", LegacyInvocation::Shortcut},
    {L"--uninstall", "old ARP UninstallString", "admin (UAC)", "interactive", "caller", LegacyInvocation::Uninstall},
    {L"--setup", "setup offer", "user", "interactive", "caller", LegacyInvocation::Setup},
    {L"--recovery", "recovery entry", "user", "interactive", "caller", LegacyInvocation::Recovery},
};

// Yeni ikilinin bu cagriya verdigi tepki.
enum class LegacyAction {
    Normal,              // eski yol degil
    UnattendedUpgrade,   // run_legacy_upgrade(LegacyHost); pipe/hizmet sunumu YOK
    UiSignalOldService,  // Ui + eski pipe'a UpdateHealthy (kendi surumu); yeni pipe'a degil
    Ui,                  // ayristirilan rol normal calisir; bos/--tray Setup penceresine yonlenmez, "Kurulum gerekli" yok
    RefuseUpgradePending,
};
LegacyInvocation classify_legacy_invocation(std::wstring_view command_line, const std::filesystem::path& self_exe, const setup::KnownRoots& r);
LegacyAction legacy_action(LegacyInvocation k) noexcept;

// T3c (ADR-044): wWinMain giris yonlendirmesi (saf). upgrade_record_present: yeni ProgramData altindaki
// setup\legacy-upgrade.json var mi (okunamayan/erisilemeyen durum da "var" sayilir: yanlislikla kaldirma yerine red).
//  Normal               : rol ayristirildigi gibi (eski yoldan --service / --updated-from kendi yollarini izler).
//  ForceUi              : bos (eski kisayol) ve --tray (eski Run degeri) eski yoldan: kurulum penceresi DEGIL, Ui.
//  RefuseUpgradePending : eski yoldan --uninstall (veya taninmayan arguman) yukseltme kaydi varken: hicbir sey
//                         yapilmaz, cikis kExitLegacyUpgradePending (21).
enum class LegacyRoute { Normal, ForceUi, RefuseUpgradePending };
LegacyRoute legacy_launch_route(LegacyInvocation k, bool upgrade_record_present) noexcept;
std::string_view to_string(LegacyRoute r) noexcept;

// Eski hizmetin saglik sinyalini bekledigi ad (pipe) ve SCM PID dogrulamasindaki hizmet adi.
std::wstring_view legacy_health_pipe() noexcept;
std::wstring_view legacy_health_service() noexcept;

// Eski yoldan --service olarak calisan yeni ikilinin SCM'ye bildirdigi hizmete ozgu cikis kodlari. "Istemci" SCM hata
// eylemi politikasidir (eski kurulumun: 3x RESTART 5 sn, sifir disi cikis = hata): 0 -> yeniden baslatilmaz,
// sifir disi -> 5 sn sonra AYNI ImagePath yeniden baslatilir.
inline constexpr std::uint32_t kLegacyHostExitHandedOff = 0;    // yeni hizmet saglikli; eski hizmet durur kalir
inline constexpr std::uint32_t kLegacyHostExitWrongLocation = 1;  // 0.9.5 ile ayni anlam
inline constexpr std::uint32_t kLegacyHostExitRolledBack = 5;   // 0.9.5 kStopUpdateRolledBack: SCM ESKI EXE ile baslatir
inline constexpr std::uint32_t kLegacyHostExitRetry = 6;        // geri alma tamamlanamadi (EXE kilitli): 5 sn sonra yeniden
inline constexpr int kExitLegacyUpgradePending = 21;           // eski yoldan --uninstall: yukseltme suruyor

}  // namespace ef::core::legacy
