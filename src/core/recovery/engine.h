#pragma once
// REC-02: degisiklik yasam dongusu. Her ayar adaptoru capture/apply/read_back/restore saglar.
// Sira (docs/04): snapshot'lar dayanikli -> intent -> uygula -> geri oku -> sonuc; hata/iptal ->
// ters sirada telafi. Ilerleme gercek adim sayisindan (R26).

#include <atomic>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "contracts/models.h"
#include "contracts/result.h"
#include "core/recovery/journal.h"

namespace ef::core::recovery {

class SettingAdapter {
public:
    virtual ~SettingAdapter() = default;
    // Mevcut durumun tam anlik goruntusu (var/yok, ham tur, miras).
    virtual contracts::Result<Snapshot> capture(const contracts::SettingOperation& op) = 0;
    virtual contracts::Status apply(const contracts::SettingOperation& op) = 0;
    // Hedefin su anki kodlanmis degeri; yoksa nullopt (deger/anahtar mevcut degil).
    virtual contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) = 0;
    // Anlik goruntuye don: existed=false ise degeri SIL (varsayilan yazarak override uydurma).
    virtual contracts::Status restore(const contracts::SettingOperation& op, const Snapshot& snap) = 0;
    // Hedef artik yok (ornek: oyun sureci kapandi). Geri alma gereksizdir ve guvenli no-op sayilir.
    virtual bool target_gone(const contracts::SettingOperation&) { return false; }
    // Saha 2026-11-15: geri almada hedefin su anki degeri. Varsayilan read_back; anlik goruntu hedefin kimligini
    // tasiyorsa (ornek: guc plani alt ayari -> yakalanan plan GUID'i) adaptor o hedefi okur. Aksi halde kullanici
    // baska plana gectiginde dogru plan geri yazilir ama ETKIN plan okunur ve basarili geri alma "dogrulanamadi" gorunurdu.
    virtual contracts::Result<std::optional<std::string>> read_for_restore(const contracts::SettingOperation& op, const Snapshot&) {
        return read_back(op);
    }
    // Saha 2026-11-15: okunan deger anlik goruntudeki durum mu (geri alma dogrulamasi + "zaten eski degerde" kontrolu).
    // Varsayilan: onceden vardiysa ayni kodlanmis deger, yoksa deger yok. Ortak dizedeki tek oge gibi "var/yok" bilgisi
    // degerin kendisinde tutulan hedefler gecersiz kilar. Gercek uyusmazlik yine uyusmazliktir (gevsetme yok).
    virtual bool matches_snapshot(const contracts::SettingOperation&, const Snapshot& snap, const std::optional<std::string>& v) {
        return snap.existed ? (v && *v == snap.old_value) : !v;
    }
};

// Kayit/test icin okunan degerin metni: deger yoksa "<absent>".
inline std::string observed_text(const std::optional<std::string>& v) { return v ? *v : std::string("<absent>"); }

// Tek adimin geri alinmasi (motorun tum geri alma yollari ayni kurali kullanir). Gercek hedef okunur; anlik goruntudeyse
// yazma yok; bizim degerimizdeyse geri yuklenir ve dogrulanir; ikisi de degilse dis degisiklik (ezilmez). Adimin durum,
// hata, okunan deger ve hata kodu alanlari guncellenir (kalici yazim cagirandadir). true = geri alindi.
bool rollback_step(SettingAdapter& a, JournalStep& s);

class AdapterRegistry {
public:
    void add(contracts::OperationId id, SettingAdapter* a) { map_[id] = a; }
    SettingAdapter* find(contracts::OperationId id) const {
        const auto it = map_.find(id);
        return it == map_.end() ? nullptr : it->second;
    }

private:
    std::map<contracts::OperationId, SettingAdapter*> map_;
};

// Geri almada hedef ne eski ne de bizim yazdigimiz degerde: kullanici/baska uygulama degistirmis.
inline constexpr std::string_view kExternalChange = "external change: value differs from both snapshot and written value";

enum class Phase { Capturing, Applying, Verifying, RollingBack, Done };
std::string_view to_string(Phase p) noexcept;

struct Progress {
    Phase phase = Phase::Capturing;
    std::size_t done = 0;   // tamamlanan birim adim
    std::size_t total = 0;  // toplam birim adim (sahte yuzde yok)
    int percent() const noexcept { return total ? static_cast<int>(done * 100 / total) : 0; }
    // T18 (geriye uyumlu, sona eklendi): su an uygulanan birim adim (1 tabanli; 0 = adim disi: hazirlik/dogrulama), adim sayisi
    // ve adimin grup anahtari (motor bilmez; arka uc doldurur, bos olabilir). Sure tahmini yok.
    std::size_t step = 0;
    std::size_t steps = 0;
    std::string group;
};

// Islem siniri izni. Reddedilirse yeni yazma baslamaz; devam eden islem durur ve o ana kadarki adimlar geri alinir.
struct WriteDecision {
    bool allowed = true;
    std::string reason;  // gate.* anahtari
};
using WriteGuard = std::function<WriteDecision()>;

struct EngineOptions {
    std::function<void(const Progress&)> on_progress;
    const std::atomic<bool>* cancel = nullptr;  // true olursa sonraki adimdan once geri alinir
    // Yalniz sureli acil geri donus (ornek: goruntu ayari onaylanmadi) ve kapiyi kendisi dogrulamis kurtarma icin:
    // kullaniciyi bozuk durumda birakacak engel olusturulmaz. Yeni yazma (apply) bu bayragi KULLANMAZ.
    bool bypass_guard = false;
};

// Acilista Active adimin GERCEK hedef durumu (salt okuma; hicbir sey yazilmaz): bizim yazdigimiz degerde mi,
// baska bir degere mi donmus, yoksa okunamadi mi. Yalniz kayit bayragina guvenilmez (docs/04).
enum class LiveState { Applied, Changed, Unreadable };

class TransactionEngine {
public:
    TransactionEngine(JournalStore& store, const AdapterRegistry& adapters) : store_(store), adapters_(adapters) {}

    // Arka uc islem siniri: ayarlanirsa her yeni yazmadan once ve adimlar arasinda sorulur (UI kontrolunden bagimsiz).
    void set_guard(WriteGuard guard) { guard_ = std::move(guard); }

    // tx: id, sid'ler ve steps[].op dolu; snapshot'lari motor alir. Sonuc: Active veya
    // (hata/iptal) Restored / RecoveryRequired. Journal yazilamazsa hicbir sey uygulanmaz.
    contracts::Result<JournalTx> apply(JournalTx tx, const EngineOptions& opt = {});

    // Idempotent: Restored islemde no-op. Dogrulanamayan adim -> RecoveryRequired (kayit korunur).
    contracts::Result<JournalTx> rollback(JournalTx tx, const EngineOptions& opt = {});

    // Salt okuma: adimin hedefi hala yazdigimiz degerde mi (adaptor read_back). observed doldurulur ("<absent>" = yok).
    LiveState check_step(const JournalStep& s, std::string* observed = nullptr) const;
    // Koruma Modu (T07): adimin hedefini okur (salt okuma); adaptor yoksa hata.
    contracts::Result<std::optional<std::string>> read_live(const contracts::SettingOperation& op) const;
    // Koruma Modu (T07): sapmis adimin ORIJINAL yazdigimiz degeri ayni adaptorle yeniden yazar ve geri okuyarak dogrular.
    // Journal kaydina (anlik goruntu dahil) dokunmaz: Kapali yine gercek onceki degerleri geri yukler. Hata: adim kaydi degismez.
    contracts::Status reapply_step(const JournalStep& s) const;

private:
    contracts::Result<JournalTx> rollback_impl(JournalTx tx, const EngineOptions& opt);
    contracts::Status set_state(JournalTx& tx, contracts::TxState s);
    // CORE-02: islem dogrulandi ama "Active" kalici yazilamadi. Sessiz Verifying/Applied BIRAKILMAZ:
    // once tam geri alma denenir (hedefler anlik goruntuye doner); journal yazilamiyorsa hedefler journalsiz
    // geri yuklenir ve hata bildirilir. Geri alma dogrulanamazsa journal RecoveryRequired kalir.
    contracts::Result<JournalTx> finalize_failure(JournalTx tx, const EngineOptions& opt, const contracts::Status& cause);
    // Journal'a yazamadan, dogrulanmis adimlarin hedef degerlerini anlik goruntuye dondur (en iyi caba).
    bool restore_applied_without_journal(JournalTx& tx);
    JournalStore& store_;
    const AdapterRegistry& adapters_;
    WriteGuard guard_;
};

// Test adaptoru: bellek ici hedefler + hata enjeksiyonu.
class FakeAdapter final : public SettingAdapter {
public:
    contracts::Result<Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const Snapshot& snap) override;

    std::map<std::string, std::string> values;  // target -> deger (yoksa anahtar yok)
    std::string fail_apply_target;
    std::string corrupt_readback_target;        // bir sonraki okumada farkli deger dondur (tek sefer)
    std::string fail_restore_target;
    int applies = 0, restores = 0;
};

}  // namespace ef::core::recovery
