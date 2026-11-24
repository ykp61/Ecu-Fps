#pragma once
// WIN-09: "Baslangic ve arka plan" sayfasinin durum makinesi (saf, birim testlenir).
//  - Tarama salt okumadir: baslangic girdileri + tavsiye (recommend) + izinli hizmetlerin durumu.
//  - Her girdi icin ayri Evet/Hayir (istenen durum); "Uygula" ayrica onay (Evet/Hayir) ister.
//  - Uygulama girdi basina tek journal islemidir (deps.change); sonuc yeniden okunarak gosterilir.
//  - Yonetici gerektiren girdiler (HKLM, ortak klasor) ve guvenlik/hile onleme/Windows bileseni
//    girdileri bu sayfadan degistirilemez (AGENTS: guvenlik/anti-cheat kapatma yok).
//  - Hizmetler yalniz bilgi: oyun oturumunda durdurma broker (OPT-02) ile Performans Acik akisindadir.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "core/adapters/startup/recommend.h"

namespace ef::core::startup {

namespace sa = adapters::startup;

enum class StartupPhase { Idle, Scanning, Ready, Confirming, Applying, Failed };

struct StartupRow {
    sa::Entry entry;
    sa::Recommendation rec;
    bool ours = false;  // bu uygulamanin etkin (geri alinabilir) kaydi var
};

struct ServiceRow {
    std::string name;
    std::string display_name;
    std::string_view breaks_key;  // durdukca ne calismaz (metin anahtari)
    bool present = false;
    bool running = false;
};

struct ScanResult {
    std::vector<StartupRow> rows;
    std::vector<ServiceRow> services;
};

struct ChangeResult {
    std::size_t applied = 0, failed = 0;
    std::size_t skipped = 0;      // kapi kapandigi icin denenmeyen girdiler
    std::string gate_reason;      // gate.* (bos = kapi engeli yok)
};

// Girdi bu sayfadan degistirilebilir mi (yonetici veya korunan kural -> hayir).
bool row_editable(const StartupRow& r) noexcept;
// Gosterim sirasi: once "kapatilmasi onerilir", sonra "oneri yok", sonra "acik kalsin"; ayni grupta ada gore.
void sort_rows(std::vector<StartupRow>& rows);

struct StartupDeps {
    std::function<contracts::Result<ScanResult>()> scan;
    // Tek girdi icin journal'li degisiklik (enable=true -> acik). Geri okuma dogrulamasi icerir.
    std::function<contracts::Status(const sa::Entry&, bool enable)> change;
    std::function<void(std::function<void()>)> run_background;
    std::function<void(std::function<void()>)> post_ui;
    // Islem siniri: guncel oyun/algilama kapisi (bos = izin, dolu = gate.* neden). Her girdiden once sorulur.
    std::function<std::string()> gate_denied;
};

class StartupSession {
public:
    explicit StartupSession(StartupDeps d) : d_(std::move(d)) {}

    void scan();                                   // Idle/Ready/Failed -> Scanning -> Ready/Failed
    void set_desired(std::size_t i, bool enabled); // yalniz Ready ve duzenlenebilir satir
    bool desired(std::size_t i) const;
    bool editable(std::size_t i) const;
    std::size_t pending() const;                   // istenen != mevcut satir sayisi
    void request_apply();                          // Ready + pending>0 -> Confirming
    void answer(bool yes);                         // Confirming -> Applying -> Ready / Ready

    StartupPhase phase() const { return phase_; }
    const std::vector<StartupRow>& rows() const { return rows_; }
    const std::vector<ServiceRow>& services() const { return services_; }
    const std::optional<ChangeResult>& last() const { return last_; }
    std::function<void()> on_change;

private:
    void set(StartupPhase p);
    void finish_scan(contracts::Result<ScanResult> r);
    StartupDeps d_;
    StartupPhase phase_ = StartupPhase::Idle;
    std::vector<StartupRow> rows_;
    std::vector<bool> desired_;
    std::vector<ServiceRow> services_;
    std::optional<ChangeResult> last_;
};

}  // namespace ef::core::startup

#include "core/recovery/engine.h"

namespace ef::core::startup {

// Journal'li tek girdi degisikligi (girdi basina ayri islem; "startup" journal kapsami, ADR-019):
//  - Bu girdiyi hedefleyen etkin islem varsa once o geri alinir (birebir eski bayt).
//  - Geri okunan durum hala istenen degilse yeni islem uygulanir (motor: kalici kayit -> yaz -> geri oku).
//  - Yonetici gerektiren konum reddedilir (broker OPT-02).
struct ChangeContext {
    recovery::JournalStore* store = nullptr;
    recovery::TransactionEngine* engine = nullptr;
    sa::StartupApi* api = nullptr;
    std::function<std::string()> new_tx_id;
    std::function<std::int64_t()> now_unix_ms;
    std::string actor_sid, target_sid;
};
contracts::OperationId operation_for(sa::Location l) noexcept;
contracts::Status change_entry(ChangeContext& c, const sa::Entry& e, bool enable);
// Bu uygulamanin etkin kaydi olan hedefler (format_target bicimi).
std::vector<std::string> active_targets(recovery::JournalStore& store);

}  // namespace ef::core::startup
