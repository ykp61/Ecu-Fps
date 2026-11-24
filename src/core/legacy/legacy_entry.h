#pragma once
// T3b (ADR-043 ek): eski yoldan "--service" ve "--updated-from" girislerinin SAF kararlari. Platform yapistiricisi
// (src/platform/windows/legacy_*) yalniz gercekleri toplar; karar ve sinir denetimi burada, birim testlidir.
//  - Yol: yalniz surucu harfli mutlak yerel yol kabul edilir (goreli, UNC, \\?\UNC\, \\.\ aygit, "." / ".." bileseni,
//    ADS ':' reddedilir). \\?\ oneki soyulur, '/' -> '\\', buyuk/kucuk harf duyarsiz, sondaki ayirac yok sayilir.
//  - Gozetimsiz yukseltme yalniz: ham goruntu yolu == eski kurulu EXE, GetFinalPathNameByHandle yolu da ayni (baglanti /
//    sembolik bag yok), zincirde yeniden ayristirma noktasi yok, belirtec SYSTEM, SCM dagiticisina baglanildi, SCM'nin
//    ESKI hizmet icin bildirdigi PID == kendi PID'i ve eski hizmetin ImagePath'i tam olarak "\"<eski exe>\" --service".
//  - Goruntu guven politikasindan gecmezse (imza) yukseltme yapilmaz; eski EXE geri getirilir (cikis 5).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "core/legacy/legacy_upgrade.h"
#include "core/setup/layout.h"

namespace ef::core::legacy {

// Kanonik yerel yol (BUYUK harf, '\\'); kabul edilmeyen bicimde nullopt.
std::optional<std::wstring> canonical_local_path(std::wstring_view p);
bool same_canonical_path(std::wstring_view a, std::wstring_view b);

// Ham goruntu yolu eski kurulu EXE mi (hizmet dagiticisi hangi giris fonksiyonunu kullanacak). Yalniz aday secimi:
// guvenlik karari decide_legacy_service_entry'dedir.
bool legacy_service_candidate(std::wstring_view self_raw, const setup::InstallLayout& legacy);

struct LegacyServiceFacts {
    std::wstring self_raw;          // GetModuleFileNameW
    std::wstring self_final;        // GetFinalPathNameByHandleW (FILE_NAME_NORMALIZED); okunamazsa bos
    bool reparse_in_path = false;   // eski kurulum klasoru veya EXE (ya da bir ust bileseni) yeniden ayristirma noktasi
    bool path_probe_failed = false; // oznitelik okunamadi
    bool token_system = false;      // TokenUser == S-1-5-18
    bool scm_connected = false;     // StartServiceCtrlDispatcherW baglandi (hizmet ana fonksiyonu icinde)
    std::uint32_t self_pid = 0;
    std::uint32_t scm_pid = 0;      // SCM'nin ESKI hizmet adi icin bildirdigi PID
    std::wstring scm_image_path;    // eski hizmetin ImagePath'i
    bool image_trusted = false;     // broker_trust(kBuildPolicy, verify_file_signature(self))
};

enum class LegacyEntry {
    NotLegacy,      // eski yol degil: normal yeni hizmet girisi
    RunUpgrade,     // run_legacy_upgrade(LegacyHost)
    RestoreOldExe,  // dogrulanmis eski hizmet sureci ama goruntu guvenilmez: yukseltme yok, eski EXE geri (5 / 6)
    Refuse,         // kimlik/yol dogrulanamadi: hicbir sey yazilmaz, cikis 1 (0.9.5 "yanlis konum" ile ayni)
};
std::string_view to_string(LegacyEntry e) noexcept;

struct LegacyEntryDecision {
    LegacyEntry action = LegacyEntry::NotLegacy;
    std::string reason;  // yol icermez
};
LegacyEntryDecision decide_legacy_service_entry(const LegacyServiceFacts& f, const setup::InstallLayout& legacy);

// ImagePath'ten EXE yolu: yalniz cift tirnakli ilk bilesen ("\"<exe>\" ..."); tirnaksiz / bozuk -> nullopt.
std::optional<std::wstring> image_path_exe(std::wstring_view image_path);

// Eski EXE geri getirildikten sonra eski hizmetin durum dosyasi (ProgramData\<eski ad>\updates\state.json; 0.9.5
// encode_state bicimi: phase, version, from_version, highest_manifest_version, swapped_at_ms). 0.9.5 roll_back ile ayni:
// Idle + tekrar oynatma tabani korunur. Okunamayan/bozuk durumda nullopt (dosyaya dokunulmaz).
std::optional<std::string> legacy_state_after_restore(std::string_view current_json);

// WinUpgradeSystem'in savunma derinligi izin listesi: platform her yazici cagrida hedefi buradan gecirir; liste disi ad
// veya yol (yanlis kayit / gelecekteki hata) hicbir Win32 cagrisina ulasmaz. UTF-8 yollar kanonik karsilastirilir.
enum class SysOp { CreateService, StartService, StopService, DeleteService, WriteArp, DeleteArp, WriteShortcut, DeleteShortcut, InstallAcl,
                   MachineAcl, RebootDelete };
bool upgrade_name_allowed(SysOp op, std::wstring_view name, const UpgradeNames& n);
bool upgrade_path_allowed(SysOp op, std::string_view path_utf8, const UpgradePaths& p);
bool path_under(std::string_view path_utf8, std::string_view root_utf8);  // kanonik; root'un kendisi dahil

// Yeni hizmet acilisinda eski artefakt temizligi (NewService) yeniden deneme plani. attempt 0'dan.
// WaitingForHost: LegacyHost hala calisiyor (yeni hizmeti kendisi baslatti, Healthy henuz yazilmadi) -> 5 sn, 10 dk'ya dek.
// CleanupPending: bir adim kilitli/durmadi -> 60 sn, 30 kez. Diger sonuclar: tekrar yok (nullopt).
std::optional<std::uint32_t> legacy_cleanup_retry_ms(UpgradeOutcome o, int attempt) noexcept;

}  // namespace ef::core::legacy
