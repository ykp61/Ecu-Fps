#pragma once
// P12-B1: "Duzelt" gercekten duzeltir. Saf kurallar (Win32 yok; birim testlenir).
//  - is_auto_fixable: uygulamanin kendisinin yazip geri okuyabildigi ve "Geri al" ile geri alabildigi duzeltme.
//    fix.power, fix.dns (kullanici secimi), fix.restore_point / fix.image_check (istege bagli isler) bu kapsamda DEGILDIR.
//  - row_action_shown: ana yuzeyde dugme gosterilir mi. Windows ayar sayfasi / web sayfasi acan yonlendirmeler
//    (OpenSettings / OpenUrl) ve AutoFix adini tasiyip yalniz yonlendiren eylemler dugme OLMAZ.
//  - plan_fix_all: otomatik duzeltilebilen tum bulgularin tek Evet/Hayir ile uygulanacak listesi.
//  - apply_fix_item: kullanici kapsamli tek bulgunun mevcut islem motoruyla uygulanmasi (yeni yazma yolu yok).
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/models.h"
#include "core/readiness/readiness.h"
#include "core/recovery/engine.h"

namespace ef::core::readiness {

// Toplu duzeltme sonucunun bildirim kimligi: hicbir bulgu satirina ait degildir (genel bildirim; Panel kartinda da gorunur).
inline constexpr const char* kFixAllNoticeId = "*fix_all";

struct FixItem {
    std::string check_id;    // Check::id
    std::string action_arg;  // "fix.game_mode" ...
    bool admin = false;      // makine kapsami (HAGS, hizli baslatma): tek UAC / yardimci hizmet
    bool display = false;    // ekran yenileme hizi: uygulamadan sonra "Koru / Geri al" (15 sn)
    bool restart = false;    // etkin olmasi icin yeniden baslatma gerekir (HAGS)
    std::vector<contracts::SettingOperation> ops;  // admin: tek islem; kullanici: bulgunun tum adimlari tek islemde
};

// Bulgu otomatik duzeltilebilir mi: AutoFix + desteklenen anahtar + gerekli bilgiler tam. Yoksa nullopt.
std::optional<FixItem> auto_fix_item(const Check& c);
bool is_auto_fixable(const Check& c);

// B5: bekleme belleginde esik bulgusunun "Duzelt"i mevcut bakim isini (T15, maint:standby_purge) calistirir. Ayar degil:
// toplu duzeltmeye (plan_fix_all) ve "Geri al"a girmez; tek Evet/Hayir ile elle. Is kimligi sabit tablodan gelir; bulgudan
// yol/komut/deger okunmaz. Baska bulgu icin nullopt.
std::optional<contracts::SettingOperation> maintenance_job_operation(const Check& c);

// Satirda eylem dugmesi gosterilir mi (Oyun Hazirligi sayfasi ve Panel bulgu satiri ayni kurali kullanir).
// Gosterilen: otomatik duzeltme, kullanicinin secimli DNS duzeltmesi, istege bagli Evet/Hayir isleri (geri yukleme noktasi,
// goruntu kontrolu) ve uygulamanin kendi calistirdigi testler/olcumler. Gosterilmeyen: OpenSettings, OpenUrl ve
// uygulamanin uygulayamadigi AutoFix (fix.power).
bool row_action_shown(const Check& c);

struct FixAllPlan {
    std::vector<FixItem> items;  // bulgu sirasiyla
    std::size_t user_count() const;
    std::size_t admin_count() const;
    bool has_display() const;
    bool has_restart() const;
    bool empty() const { return items.empty(); }
};
// undo: bulgu basina geri alma durumu (bos = hepsi None). Hedefinde geri alinmamis kayit olan (Undo/Retry/...) bulguya
// yeni yazma planlanmaz. Yalniz Advice/Problem bulgular girer.
FixAllPlan plan_fix_all(const std::vector<Check>& checks, const std::vector<UndoState>& undo);

// Kullanici kapsamli tek bulguyu ("fixes" motoru) uygular: ayni islemde tum adimlar, geri okuma, kalici kayit. new_tx kimlik /
// sid / zaman alanlari dolu bos islem dondurur. Sonuc motorun sonucudur (Active = uygulandi ve dogrulandi).
contracts::Result<recovery::JournalTx> apply_fix_item(recovery::TransactionEngine& engine, const FixItem& item,
                                                      const std::function<recovery::JournalTx()>& new_tx);

// Toplu duzeltme sonucunun tek kisa cumlesi (katalog anahtari; {n} duzeltilen, {m} duzeltilemeyen). Dogrulanmamis basari
// yazilmaz: yalniz motorun Active / broker'in Applied dedigi ayarlar "duzeltildi" sayilir.
//  applied: duzeltilen ayar sayisi (kullanici + yonetici); failed: duzeltilemeyen; restart: duzeltilenlerden biri yeniden
//  baslatma bekliyor; admin_key: yonetici adiminin kendi bildirimi (UAC reddi vb.; bos / "chk.notice.fix_reboot" = basari).
struct FixAllNotice {
    std::string key;
    bool warning = false;
};
FixAllNotice fix_all_notice(int applied, int failed, bool restart, std::string_view admin_key);

}  // namespace ef::core::readiness
