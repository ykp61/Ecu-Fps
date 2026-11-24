#pragma once
// REC-04: dis degisiklik catismalari ve alan duzeyinde (oyun yapilandirmasi) birlestirme.
// Kullanici Evet -> orijinale don; Hayir -> kullanici degeri korunur ve sonuc "kismen" raporlanir.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "contracts/result.h"
#include "core/recovery/engine.h"
#include "core/recovery/journal.h"

namespace ef::core::recovery {

struct Conflict {
    std::size_t step_index = 0;
    std::string target;
    Snapshot original;
    std::string written;                 // uygulamanin yazdigi
    std::optional<std::string> current;  // hedefin su anki degeri
};

// Geri almada dis degisiklik nedeniyle beklemede olan adimlar (gercek hedef yeniden okunur).
std::vector<Conflict> find_conflicts(const JournalTx& tx, const AdapterRegistry& adapters);

enum class ConflictDecision { RestoreOriginal /* Evet */, KeepCurrent /* Hayir */ };

// Karari uygular, kalici yazar. Tum catismalar cozulunce islem Restored olur.
contracts::Result<JournalTx> resolve_conflict(JournalTx tx, std::size_t step_index, ConflictDecision d,
                                              const AdapterRegistry& adapters, JournalStore& store);

// Islem RecoveryRequired ve en az bir adimi dis degisiklik (kExternalChange) yuzunden bekliyor.
bool has_external_conflict(const JournalTx& tx) noexcept;

// Oyun Hazirligi "Mevcut degeri koru" (Evet/Hayir sonrasi): dis degisiklik bekleyen adimlar KeptExternal olur,
// HICBIR ayar yazilmaz/okunmaz. match verilirse yalniz eslesen adimlar. Bekleyen baska adim kalmazsa islem
// mevcut yasam dongusuyle kapanir (RecoveryRequired -> RollingBack -> Restored; status_of: KeptCurrent).
// Catisma yoksa InvalidTransition (hicbir sey yazilmaz).
contracts::Result<JournalTx> keep_external_values(JournalTx tx, JournalStore& store,
                                                  const std::function<bool(const contracts::SettingOperation&)>& match = {});

enum class RestoreOutcome { NotRestored, Full, PartialKeptUserChanges };
// Restored islemde bile kullanici degeri korunan adim varsa "kismen" — tam geri donus uydurulmaz.
RestoreOutcome restore_outcome(const JournalTx& tx) noexcept;

// ---- Alan duzeyinde yapilandirma birlestirme (cfg/ini satirlari) ----
// Satir bicimleri: `key value`, `key "value"`, `key=value`. Yorum/bos/bilinmeyen satirlar aynen korunur.
struct FieldChange {
    std::string key;
    std::optional<std::string> original;  // yoksa alan onceden yoktu
    std::string written;                  // uygulamanin yazdigi deger
};
struct FieldMergeResult {
    std::string text;
    std::vector<std::string> conflicts;   // kullanicinin sonradan degistirdigi bizim alanlarimiz
    std::size_t restored = 0;
};
std::optional<std::string> read_field(const std::string& text, const std::string& key);
std::string write_field(const std::string& text, const std::string& key, const std::optional<std::string>& value);
// Yalniz bizim alanlarimiz ve yalniz hala bizim yazdigimiz degerdeyse geri alinir; digerleri dokunulmaz.
FieldMergeResult restore_fields(const std::string& current_text, const std::vector<FieldChange>& changes);

}  // namespace ef::core::recovery
