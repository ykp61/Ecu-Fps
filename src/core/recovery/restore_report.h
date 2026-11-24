#pragma once
// Saha 2026-11-15 (ikinci arkadas, 0.6.0): "Geri donus tamamlanamadi" ekraninda HANGI ayarin neden geri alinamadigi
// gorunmuyordu; destek paketinde de bu bilgi yoktu. Bu dosya journal listesinden (salt okuma) geri alinamamis adimlari
// cikarir, neden sinifini belirler ve destek paketi/restore.log icin kisisel veri icermeyen bicimde yazar.
//
// Neden siniflari (kullanici metni restore.reason.*):
//   NotVerified  : geri yazildi ama geri okuma eski degeri gostermedi, hedef okunamadi veya islem yarida kaldi
//   Denied       : yetki yok (yonetici izni verilmedi / erisim reddedildi)
//   External     : hedef ne eski ne bizim degerimizde; baska bir program/kullanici degistirdi ("Mevcut degeri koru")
//   NeedsRestart : broker ayni hedef icin "yeniden baslatma bekliyor" (pending_reboot) bildirdi (HAGS)
// Siniflandirma kaydedilen alanlardan yapilir; eski (0.6.0) kayitlarda okunan deger/hata kodu bos olabilir.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/recovery/journal.h"
#include "core/serialization/json.h"

namespace ef::core::recovery {

enum class RestoreFailReason { NotVerified, Denied, External, NeedsRestart };
std::string_view to_string(RestoreFailReason r) noexcept;  // not_verified | denied | external | needs_restart

struct FailedRestoreItem {
    std::string scope;   // journal kapsami: user | fixes | startup | machine | machine_fixes
    std::string tx_id;   // rastgele islem kimligi (32 hex; kisisel degil)
    contracts::OperationId op = contracts::OperationId::PowerSchemeSet;
    std::string target;  // tipli hedef ("gaming:windowed_opt")
    std::string expected;    // geri alinmasi beklenen deger; onceden yoksa "<absent>"
    std::string observed;    // okunan deger ("<absent>" = yok; bos = okunamadi / eski kayitta yok)
    std::string written;     // uygulamanin yazdigi deger
    std::string error;       // teknik hata metni
    std::string error_code;  // contracts::ErrorCode metni (bos = kod yok)
    RestoreFailReason reason = RestoreFailReason::NotVerified;
    bool interrupted = false;       // islem yarida kaldi (geri alma henuz denenmedi / kesildi)
    bool journal_unreadable = false;  // kayit dosyasi okunamadi/bozuk (target = dosya adi; adim bilgisi yok)
};

// Yapisal veriden: dis degisiklik = kExternalChange; Denied = error_code "denied"; NeedsRestart = broker'in ayni hedef
// icin bildirdigi pending_reboot (BatchResult adimi; journal'da tutulmaz, cagiran verir). Metin ipucu yalniz hata kodu
// olmayan eski (0.6.0) kayitlarda erisim reddi icin kullanilir.
RestoreFailReason classify_restore_failure(const JournalStep& s, bool pending_reboot) noexcept;
// Destek metni: dosya yolu -> "<path>", kullanici SID'i -> "<sid>", UTF-8 sinirinda kisaltma.
std::string sanitize_support_text(std::string_view s, std::size_t max_bytes);
// Beklenen degerin metni: onceden vardiysa anlik goruntu degeri, yoksa "<absent>".
std::string restore_expected_text(const JournalStep& s);

// RecoveryRequired ve yarim kalmis (Prepared/Applying/Applied/Verifying/RollingBack) islemlerin geri alinmamis adimlari
// (Planned, RolledBack, KeptExternal haric) ve okunamayan kayit dosyalari. Etkin (Active) islem basarisizlik degildir.
std::vector<FailedRestoreItem> failed_restore_items(std::string_view scope, const JournalStore::Listing& l);

// Arayuz metni: ayar adi katalog anahtari (bilinmeyen hedef -> "history.op.<islem>") ve neden anahtari.
std::string restore_item_name_key(const FailedRestoreItem& i);
std::string_view restore_reason_key(RestoreFailReason r) noexcept;

// Destek paketi / restore.log icin redakte: degerler yalniz izinli (ayar disi veri tasimayan) islemlerde yazilir;
// baska islemlerde hedef ve degerler bosaltilir (yol/komut satiri sizmasin). Metinler kisaltilir.
FailedRestoreItem redact_for_support(FailedRestoreItem i);
json::Value to_json(const std::vector<FailedRestoreItem>& items);
std::string restore_log_line(const FailedRestoreItem& i);

}  // namespace ef::core::recovery
