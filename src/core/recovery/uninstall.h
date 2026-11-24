#pragma once
// REC-06: "Her seyi geri al ve verileri sil". Once tum aktif degisiklikler geri alinir ve dogrulanir;
// yalniz hepsi basariliysa veri silinir. Kismi basarisizlikta hicbir veri silinmez.

#include <string>
#include <vector>

#include "contracts/result.h"
#include "core/recovery/service.h"

namespace ef::core::recovery {

class DataRemover {
public:
    virtual ~DataRemover() = default;
    virtual contracts::Status remove_autostart() = 0;
    virtual contracts::Status remove_user_data() = 0;     // %LOCALAPPDATA%\EcuFps
    virtual contracts::Status remove_machine_data() = 0;  // %ProgramData%\EcuFps (yonetici)
};

enum class UninstallStage { Restore, RemoveProgram, RemoveAutostart, RemoveUserData, RemoveMachineData, Done };

struct UninstallResult {
    bool restored_all = false;
    bool data_removed = false;
    UninstallStage stopped_at = UninstallStage::Restore;
    std::vector<std::string> unrestored;   // geri alinamayan islemler (veri silinmedi)
    std::vector<std::string> notes;        // ornek: makine verisi icin yonetici gerekli
    bool reboot_pending = false;           // ADR-033: program kaldirildi; kalan dosya yeniden baslatmada silinir
};

// ADR-033 / spec 8: kurulu programin kaldirilmasi (hizmet, dosyalar, kisayol, ARP, ProgramData). Uretimde yukseltilmis
// kurulum iscisi (tek UAC) calistirir; testlerde sahte. ProgramData yalniz makine journal'lari temizse silinir (isci).
class ProgramRemover {
public:
    virtual ~ProgramRemover() = default;
    struct Outcome {
        bool attempted = false;       // isci calisti mi (UAC reddi / baslatilamadi -> false)
        bool removed = false;         // program kaldirildi (islem motoru ok)
        bool reboot_pending = false;  // calisan EXE / klasor yeniden baslatmada silinecek
        bool denied = false;          // yonetici izni verilmedi: hicbir sey silinmedi
        std::string detail;
    };
    virtual Outcome remove_program() = 0;
};

UninstallResult run_uninstall(const std::vector<JournalScope>& scopes, DataRemover& remover);

// Tam kaldirma. Sira: Restore -> (program != nullptr: RemoveProgram) -> RemoveAutostart -> RemoveUserData.
// program == nullptr (tasinabilir kopya): run_uninstall ile ayni (makine verisi dahil). Herhangi bir hata durdurur;
// geri yukleme tam degilse hicbir sey silinmez; program kaldirma reddedilir/basarisiz olursa Run kaydi ve kullanici
// verisi de silinmez (uygulama kullanilabilir kalir).
UninstallResult run_full_uninstall(const std::vector<JournalScope>& scopes, DataRemover& data, ProgramRemover* program);

// run_full_uninstall notlari (sabit anahtarlar; UI yerellestirir).
inline constexpr const char* kNoteProgramRebootPending = "program removed; remaining files are deleted at restart";
inline constexpr const char* kNoteProgramDenied = "administrator approval was not given; nothing was removed";
inline constexpr const char* kNoteProgramFailed = "the program could not be removed; your data was kept";

}  // namespace ef::core::recovery
