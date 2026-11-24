#pragma once
// Handoff v1 REC-01/REC-02: "Islem Gecmisi" sayfasinin verisi (saf; birim testlenir).
// Kalici journal kayitlari (user/machine/startup kapsamlari) en yeniden eskiye siralanir. Degerler journal'da
// ne yaziyorsa o gosterilir; eksik deger tahmin edilmez. Bozuk/yabanci/yeni semali dosyalar sayilir, silinmez.
// Disk temizligi journal'a girmez (ADR-016): burada "geri alinabilir" satir olarak gorunmez.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/recovery/journal.h"

namespace ef::core::recovery {

struct HistoryStepRow {
    contracts::OperationId op = contracts::OperationId::PowerSchemeSet;
    std::string target;
    bool existed = false;     // ayar/girdi onceden var miydi
    std::string old_value;    // kaydedilmis onceki deger (codec metni; bos olabilir)
    std::string written;      // uygulamanin yazdigi son deger (bos: yazilmadi)
    StepState state = StepState::Planned;
    bool has_error = false;
};

// KeptCurrent: hicbir adim geri alinmadi; dis degisiklik bekleyen adim(lar)da kullanici mevcut degeri korudu (ayar
// yazilmadi). PartlyRestored: bazi adimlar geri alindi, bazilarinda dis deger korundu.
enum class HistoryStatus { Active, Restored, PartlyRestored, RecoveryRequired, Incomplete, KeptCurrent };

struct HistoryTxRow {
    std::string scope;  // "user" / "machine" / "startup"
    std::string id;
    std::int64_t created_unix_ms = 0;
    HistoryStatus status = HistoryStatus::Incomplete;
    std::vector<HistoryStepRow> steps;
};

struct HistoryView {
    std::vector<HistoryTxRow> rows;
    std::size_t corrupt = 0, foreign = 0, newer = 0;
    std::size_t total = 0;  // kesilmeden onceki gecerli kayit sayisi
};

HistoryStatus status_of(const JournalTx& tx) noexcept;

// listings: (kapsam adi, tarama sonucu). max_rows: gosterilecek ust sinir (bellek/cizim sinirli).
HistoryView build_history(const std::vector<std::pair<std::string, JournalStore::Listing>>& listings, std::size_t max_rows = 200);

}  // namespace ef::core::recovery
