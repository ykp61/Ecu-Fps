#pragma once
// WIN-08: temizlik akisinin durum makinesi (saf, birim testlenir). ADR-016:
//  - Once onizleme (salt okuma); kategori secimi kullanicida (varsayilanlar motor bilgisinden).
//  - Temizlik geri alinamaz: ayri onay (Evet/Hayir) olmadan hicbir dosya silinmez.
//  - Ilerleme gercek dosya sayisindan; iptal sonraki dosyadan once durur.
//  - Sonuc olculur (silinen bayt + birim bos alani once/sonra); tahmin yok.

#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <optional>
#include <vector>

#include "core/cleanup/cleanup.h"

namespace ef::core::cleanup {

enum class SessionPhase { Idle, Scanning, Ready, Confirming, Running, Done, Failed };

struct SessionDeps {
    // volume: secilen birim koku (bos = birim filtresi yok). opt: iptal + dosya/sure butcesi.
    std::function<CleanupPlan(const std::vector<CleanupCategory>&, const std::wstring& volume, const PreviewOptions& opt)> preview;
    std::function<contracts::Result<CleanupReport>(const CleanupPlan&, const std::vector<CleanupCategory>&, const RunOptions&)> run;
    std::function<void(std::function<void()>)> run_background;
    std::function<void(std::function<void()>)> post_ui;
    // Islem siniri: guncel oyun/algilama kapisi. Bos dize = izin; dolu = gate.* neden. Silmeden hemen once ve
    // silme surerken en fazla gate_poll_ms'de bir sorulur (Evet bekleyisindeki eski izne guvenilmez).
    std::function<std::string()> gate_denied;
    int gate_poll_ms = 1000;
};

class CleanupSession {
public:
    explicit CleanupSession(SessionDeps d) : d_(std::move(d)) {}

    void scan();                        // Idle/Ready/Done/Failed -> Scanning -> Ready
    // EXT-CLN: tarama iptali. Bekleyen tarama sonucu yok sayilir; hicbir dosya silinmez (Idle'a doner).
    void cancel_scan();
    bool scan_cancelled() const { return scan_cancelled_; }
    // EXT-CLN-01: birim secimi. Degisirse onceki plan gecersizdir: yeniden taranir (secim korunur).
    void set_volume(std::wstring root);
    const std::wstring& volume() const { return volume_; }
    // EXT-CLN-03: silmeden once dosya listesi (tek kategori acik; ayni kategori tekrar -> kapanir).
    void toggle_files(CleanupCategory c);
    std::optional<CleanupCategory> files_open() const { return files_open_; }
    // Failed nedeni (ornek "volume changed"); UI ayri ileti gosterir.
    const std::string& failure() const { return failure_; }
    // WIN-08 ek: yukseltilmis yardimcinin taradigi yonetici kategori onizlemeleri plana islenir; sayilar
    // yardimcidan geldigi icin uydurma yok. Tarama/birim degisikliginde yetki durumu sifirlanir.
    void adopt_admin_previews(const std::vector<CleanupPreview>& previews);
    bool admin_previewed(CleanupCategory c) const;  // bu kategori yukseltilmis taramayla hazir mi
    void clear_admin_previews();
    void set_selected(CleanupCategory c, bool on);
    bool selected(CleanupCategory c) const;
    bool selectable(CleanupCategory c) const;  // hazir, dosyasi var ve (yonetici icin yetki alinmis)
    std::uint64_t selected_bytes() const;
    std::size_t selected_files() const;
    bool any_selected() const;  // T15: bellek satiri dahil herhangi bir secim (calistirma kapisi)
    void request_run();                 // Ready -> Confirming (secim yoksa kalir)
    void answer(bool yes);              // Confirming -> Running / Ready
    void cancel() { cancel_ = true; }

    SessionPhase phase() const { return phase_; }
    const CleanupPlan& plan() const { return plan_; }
    const CleanupProgress& progress() const { return progress_; }
    const std::optional<CleanupReport>& report() const { return report_; }
    // Son calistirma oyun/algilama kapisi nedeniyle hic baslamadi veya yarida durdu (gate.*); bos = kapi engeli yok.
    const std::string& gate_reason() const { return gate_reason_; }
    std::function<void()> on_change;

private:
    void set(SessionPhase p);
    SessionDeps d_;
    SessionPhase phase_ = SessionPhase::Idle;
    std::string gate_reason_;
    CleanupPlan plan_;
    std::map<CleanupCategory, bool> selection_;
    CleanupProgress progress_;
    std::optional<CleanupReport> report_;
    std::atomic<bool> cancel_{false};       // calistirma iptali (Durdur)
    std::atomic<bool> scan_cancel_{false};  // tarama iptali (Iptal)
    std::uint64_t scan_gen_ = 0;            // eski tarama sonucunu yok saymak icin
    bool scan_cancelled_ = false;           // son tarama kullanici tarafindan iptal edildi
    std::wstring volume_;
    std::optional<CleanupCategory> files_open_;
    std::set<CleanupCategory> admin_ready_;  // yukseltilmis taramasi alinmis yonetici kategorileri
    std::string failure_;
};

const CategoryPlan* find_category(const CleanupPlan& p, CleanupCategory c);

}  // namespace ef::core::cleanup
