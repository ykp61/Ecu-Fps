#pragma once
// UI-03: test ve sonuc ekranlarinin durum modeli (saf, birim testlenir). Olcum motoru MEAS/BENCH
// gorevlerindedir; bu model yalniz onlarin uretecegi gercek degerleri gosterir.
// Kurallar:
//  - Olculmeyen deger 0 degil "Olculemedi" + neden olarak gosterilir; ornek/yer tutucu sayi yok.
//  - Kendi 3D sahnesi yalniz Uygulama Testi aktif olarak calisirken cizilir.
//  - Iki test kaynagi vardir; sureler ayri performans modu degildir.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "contracts/measure.h"
#include "contracts/models.h"
#include "core/diagnosis/drops.h"
#include "core/measure/game_limit_view.h"
#include "core/insights/history.h"
#include "core/measure/balance_view.h"
#include "core/measure/bench_cards.h"
#include "core/measure/endurance_view.h"
#include "core/measure/frame_stats.h"
#include "core/measure/run_manifest.h"
#include "core/perf/proof_experiment.h"

namespace ef::core::measure {

enum class TestSource { Builtin, InGame, Endurance };
enum class TestPhase { Idle, Preparing, Running, Stopping, Done, Cancelled, Failed };
std::string_view to_string(TestPhase p) noexcept;

enum class Diagnosis { None, Limited, Mixed, Insufficient };
enum class Verdict { Improved, Regressed, Uncertain, NoCohort };

struct ResultView {
    TestSource source = TestSource::Builtin;
    std::string title;                  // oyun adi veya test surumu (utf-8)
    std::int64_t when_unix_ms = 0;
    Diagnosis diagnosis = Diagnosis::Insufficient;
    contracts::Confidence confidence = contracts::Confidence::Low;
    std::vector<std::string> evidence_keys;  // katalog anahtarlari (DIAG)
    contracts::Measurement avg_fps = contracts::Measurement::missing(contracts::Unit::Fps, contracts::MissingReason::NotSampled);
    contracts::Measurement low1_fps = contracts::Measurement::missing(contracts::Unit::Fps, contracts::MissingReason::NotSampled);
    contracts::Measurement low01_fps = contracts::Measurement::missing(contracts::Unit::Fps, contracts::MissingReason::NotSampled);
    contracts::Measurement p99_ms = contracts::Measurement::missing(contracts::Unit::Milliseconds, contracts::MissingReason::NotSampled);
    contracts::Measurement p99_9_ms = contracts::Measurement::missing(contracts::Unit::Milliseconds, contracts::MissingReason::NotSampled);
    // Spec 2026-11-12 C: en yavas gecerli kare (ms; FrameStats::max_ms / sahne "scene_max_ms"). Eski kayitta yok -> "—".
    contracts::Measurement worst_ms = contracts::Measurement::missing(contracts::Unit::Milliseconds, contracts::MissingReason::NotSampled);
    FramePattern pattern = FramePattern::Insufficient;
    // Saniye basina gercek FPS (en fazla kMaxFpsSeries nokta); yoksa bos: grafik cizilmez, uydurulmaz.
    std::vector<double> fps_series;
    // BENCH v2: Uygulama Testi bilesen kartlari (GPU/CPU/RAM/Disk). Bossa yalniz FPS kutulari gosterilir.
    std::vector<ComponentCard> components;
    TestMode mode = TestMode::Quick;
    // M02/M03 (UI denetimi B): Uygulama Testi teshis degildir. Bos degilse baslik bu anahtardir (bench.result.*,
    // alt test durumlarindan) ve guven satiri gosterilmez. tier: sahne kademesi ("medium"); ic kimlik gosterilmez.
    std::string headline_key;
    std::map<std::string, std::string> headline_args;
    bool show_confidence = true;
    std::string tier;
    // BM-01: yalniz Oyunda Test icindir. Kosu manifestosu gecerliyse true; kayip ETW olayi, kisa kosu,
    // runtime_mixed/runtime_other gibi nedenlerde false. Sonuc ekrani bunu "gecerli kosu" gibi gostermez.
    bool run_valid = true;
    // DIAG-08 (Oyunda Test): kanita dayali takilma raporu. Yalniz gercek karelerle hesaplanir; bos = rapor yok.
    std::optional<core::diagnosis::DropReport> drops;
    // Spec 2026-11-12 B (Oyunda Test): "bu oyunda ne sinirladi" ozeti (kare basi GPU mesgul suresi + kare siniri
    // suphesi). Yalniz gercek yakalamadan uretilir; bos = kart cizilmez.
    std::optional<GameLimitView> game_limit;
    // Denge testi (spec 2026-11-12 A5): kaynak Builtin kalir (kullaniciya gorunen kaynak yine ikidir); doluysa bu
    // kayit Uygulama Testi'nin "Denge testi" kapsamidir ve Denge karti bundan cizilir. FPS kutulari kullanilmaz.
    std::optional<BalanceView> balance;
};

// BM-01: Oyunda Test kare yakalama sonucundan sonuc gorunumu (saf; birim testlenir). Kosu gecersiz veya
// dusuk guvenliyse run_valid=false, guven Low ve her dislama icin "bench.run.<kod>" kanit anahtari eklenir.
// Olculen degerlerden kullanici dilinde akicilik (smooth.* anahtari). Kare deseni biliniyorsa o; degilse ortalama ile
// %1 en dusuk FPS orani. Ikisi de yoksa smooth.unknown ("Olculmedi"): varsayim yok.
std::string_view smoothness_key(const contracts::Measurement& avg, const contracts::Measurement& low1, FramePattern pattern) noexcept;

ResultView ingame_result_view(std::string title, const RunManifest& manifest, const FrameStats& stats, std::int64_t when_unix_ms);

struct ComparisonView {
    bool available = false;
    Verdict verdict = Verdict::NoCohort;
    std::optional<double> delta_percent;  // iki kayitta da olculen ortalama FPS varsa (yuzde)
    // UI-16/B3: surum karsilastirilabilirligi. Ayni kaynak + kosul izi bulunsa bile uygulama/sahne/
    // protokol surumu farkli/bilinmiyorsa veya kosu gecersizse metrik farki iddia edilmez; note_key
    // nedeni soyler (ortak comparison_policy karari).
    bool comparable = true;
    std::string note_key;
};

// UI-16/B3: onceki ayni-kosullu gecmis kayda gore ortalama FPS farki (saf). Kaynak veya kosul izi
// bos/farkliysa available=false. Ortak comparison_policy uyumsuz derse comparable=false ve note_key
// neden katalog anahtaridir (comparison.version_mismatch / scene_mismatch / protocol_mismatch /
// invalid_run). Tek kosu oldugu icin verdict daima Uncertain; fark yalniz tanimlayicidir,
// nedensellik/iyilesme iddiasi degildir.
ComparisonView measure_comparison(const core::insights::HistoryEntry& base, const core::insights::HistoryEntry& current,
                                  std::string_view avg_fps_key);
// Spec 2026-11-12 C: karsilastirma tabani. prior = yeni kayit eklenmeden ONCEKI gecmis (en yeni basta; en cok
// insights::kMaxEntries kayit). Ayni kaynak + bos olmayan ayni kosul izli en yeni kayit; yoksa nullptr (iki kayittan az:
// onceki kayit yok -> karsilastirma yok).
const core::insights::HistoryEntry* comparison_base(const std::vector<core::insights::HistoryEntry>& prior,
                                                    const core::insights::HistoryEntry& current) noexcept;
// B15: uygulama yeniden acilisinda sonuc kartinin farki (saf). entries = kayitli gecmis (en yeni basta); result = kartta
// gosterilen sonuc. Yalniz en yeni kayit Uygulama Testi ("builtin") ve gosterilen sonuc o kayitsa (when_unix_ms ayni)
// test sonundakiyle ayni taban + ortak politika kullanilir. Oyunda Test / farkli kayit / taban yok: bos ComparisonView.
ComparisonView reopen_comparison(const std::vector<core::insights::HistoryEntry>& entries, const ResultView* result);

inline constexpr std::size_t kMaxStorageRoots = 8;

// R13/INS-03: uzun test (isinma) durumu. Kendi is parcaciginda calisir; olcum fazi (TestPhase) degismez.
// Ilerleme yalniz GERCEK gecen suredir (started_ms); sahte yuzde yok.
struct EnduranceState {
    bool running = false;
    bool has_result = false;
    bool failed = false;        // is parcacigi istisnasi: sonuc yok, hicbir ayar degismedi
    double elapsed_s = 0;       // bitince gercek duvar saati
    double budget_s = 600;      // INS-03 EnduranceBudget.planned_seconds
    std::int64_t started_ms = 0;  // calisirken GetTickCount64 tabani (0 = yok); B14: kosucu t=0 aninda alinir
    // B14: son dilim ornegi (her saniye). Yalniz olculen deger; okunamayan alan bos kalir ("okunamiyor").
    bool has_live = false;
    std::optional<double> live_fps, live_gpu_mhz, live_gpu_load_pct;
    // Kosucu t=0 aninda bildirir: GPU saati/yuku bu kosuda desteklenmiyor (NVIDIA disi ya da sahne baska kartta).
    // Desteklenmeyen deger "okunamiyor" diye yazilmaz.
    bool telemetry_unsupported = false;
    EnduranceView result;
};

// P1: "Performans etkisini olc" ABBA deneyi (docs/superpowers/plans/2026-11-17-p1-abba-proof.md). Yalniz oturumda yasar;
// sonuc diske yazilmaz ve gecmis kaydi degildir. Sayilar bu deneyin olcumudur; oyun FPS tahmini degildir.
struct ProofView {
    core::perf::ProofPhase phase = core::perf::ProofPhase::Idle;
    core::perf::ProofProgress progress;
    int estimate_min = 0;  // yaklasik sure (dk); 0 = bilinmiyor (kosu suresinden hesaplanir)
    std::optional<core::perf::ProofResult> result;
    core::perf::ProofRefusal refusal = core::perf::ProofRefusal::None;  // son reddedilen baslatma
    std::string stage;  // "off" | "on" | "restore" | "warmup" | "run_off" | "run_on": suren adimin kisa anahtari
    // Olculen alt kume (yonetici/yeniden baslatma istemeyen gruplar). scope_*: calisan/son deneyin (baslangicta sabitlenir);
    // pre_*: simdi baslatilirsa olculecek alt kume (canli onizleme). partial = plandan disarida kalan ayar var.
    std::size_t scope_groups = 0;
    bool scope_partial = false;
    bool scope_admin = false;  // kapsamda yonetici ayar var (yardimci hizmet yolu; UAC acilmaz)
    std::size_t pre_groups = 0;
    bool pre_partial = false;
    bool pre_admin = false;
    // Baslat dugmesinin yanindaki gorunur on kosul nedeni (None = baslatilabilir). Kosullar App'te hesaplanir.
    core::perf::ProofRefusal precheck = core::perf::ProofRefusal::None;
    bool running() const { return phase == core::perf::ProofPhase::Running || phase == core::perf::ProofPhase::Restoring; }
};

struct MeasureState {
    TestSource source = TestSource::Builtin;
    TestPhase phase = TestPhase::Idle;
    bool builtin_available = false;  // BENCH motoru bagli mi
    bool ingame_available = false;   // MEAS kaynagi bagli mi (uyumluluk kapisi: dogrulanmamis -> false)
    // Oyunda Test kapaliyken gosterilecek yerellestirilmis neden (gate.*; bos = genel "kullanilamiyor").
    // UI ham kod yazmaz; katalog metni yoksa genel metne duser.
    std::string ingame_reason_key;
    bool game_running = false;       // EXT-AC-02: oyun calisirken yerlesik agir test baslatilmaz
    // Oyun tespiti guvenilir degilse de agir test baslatilmaz; neden katalog anahtariyla gosterilir.
    bool gate_blocked = false;
    bool history_save_failed = false;  // son sonuc kalici kayda yazilamadi
    bool gate_stopped = false;       // test surerken oyun algilandi ve test durduruldu
    std::string gate_reason_key;     // "gate.game_running" / "gate.detection_*"; bos = genel neden
    // BENCH-05 / TEST-02: depolama testi icin kullanici secimi (yerel sabit birimler; 0 = secilmedi -> yazma yok)
    std::vector<std::string> storage_roots;  // "C:\" ... (en fazla kMaxStorageRoots)
    bool storage_blocked = false;            // disk veri riski bildirdi: yazma testi calistirilmadi
    std::size_t storage_choice = 0;          // 0 = yapma, k = storage_roots[k-1]
    TestMode mode = TestMode::Quick;         // BENCH v2: Hizli (~1 dk) / Tam (~5 dk)
    // Spec 2026-11-12 A5: Uygulama Testi kapsami (Genel test | Denge testi). Oturumluk; test islerken degismez.
    BuiltinScope scope = BuiltinScope::General;
    std::string stage_key;           // "measure.stage_warmup" vb.
    double elapsed_s = 0, total_s = 0;
    std::int64_t started_ms = 0;     // M04: tek duze saat (GetTickCount64, ms); UI 1 Hz ile gecen sureyi buradan yazar (0 = yok)
    std::string fail_reason;         // M11: Failed nedeni (ASCII neden kodu; bench_text reason_text ile metne cevrilir)
    std::optional<ResultView> result;
    std::vector<ResultView> history;
    ComparisonView comparison;
    // Spec 2026-11-12 A5: en yeni denge sonucu (Panel "Performansi ne sinirliyor?" basligi ve satirlari); yoksa bos.
    std::optional<BalanceView> balance_latest;
    EnduranceState endurance;  // R13/INS-03
    ProofView proof;           // P1
    // Spec 2026-11-12 A4: canli test ekrani. user_on = Test ayarlari -> "Canli test ekrani" (kalici tercih; varsayilan
    // acik). active = SUREN kosu canli ekranla basladi (onizleme + canli sayilar uretiliyor); kosu bitince false.
    // Kosu sirasinda user_on degismez (onizleme acik/kapali kosunun kosuludur).
    struct LiveViewState {
        bool user_on = true;
        bool active = false;
    };
    LiveViewState live_view;
};

// A4: ana pencerenin icerik alani canli test gorunumunu gosterir mi: Uygulama Testi isliyor (Durduruluyor dahil) ve bu
// kosu canli ekranla basladi.
bool live_view_shown(const MeasureState& s) noexcept;

// Kendi 3D sahnesi yalniz Uygulama Testi gercekten calisirken cizilir.
bool scene_visible(const MeasureState& s) noexcept;
// Baslat dugmesi: secili kaynak bagliysa ve test calismiyorsa.
bool can_start(const MeasureState& s) noexcept;
// BENCH-05 + B10 (sahip istegi): envanterden olculebilen birimler gelince depolama secenekleri yenilenir (en fazla
// kMaxStorageRoots). Hicbir birim (sistem birimi dahil) otomatik secilmez: secim varsayilan "Yapma" (0) kalir; kullanici
// secimi listede hala varsa korunur, aralik disina dustuyse "Yapma"ya doner.
void adopt_storage_roots(MeasureState& s, std::vector<std::string> measurable_roots);
bool can_stop(const MeasureState& s) noexcept;
// M05/M07: test isliyor (Durduruluyor dahil): kaynak/sure/birim degistirilemez, Baslat yok.
bool busy(const MeasureState& s) noexcept;
// R13: uzun test yalniz bagliyken, oyun calismiyorken ve baska test yokken baslatilir (otomatik baslatma yok).
bool endurance_available(const MeasureState& s) noexcept;
bool endurance_can_start(const MeasureState& s) noexcept;
// P1: Performans etkisi deneyi baslatilabilir mi (kart dugmesi). Ayrintili on kosullar App'te (check_proof_preconditions).
bool proof_can_start(const MeasureState& s) noexcept;
// P3: Olcumler "Darbogaz" kartinin gosterdigi denge gorunumu (gorunen sonuc denge testiyse o; degilse en yeni denge kaydi;
// yoksa nullptr) ve "Raporu kaydet" dugmesi: kart "Bu PC'de olculemedi" gosteriyorsa (balance_view_unmeasured) cizilir.
const BalanceView* balance_card_view(const MeasureState& s) noexcept;
bool balance_report_offered(const MeasureState& s) noexcept;
// R13: gercek gecen sure (sn); calisirken started_ms tabanindan, yoksa son deger.
double endurance_elapsed_s(const MeasureState& s, std::int64_t now_ms) noexcept;
// M04: asama sirasi ("measure.stage_cpu" -> {2, 5}; denge adimi "measure.stage_balance_k1" -> {5, 9});
// hazirlik/bitis/bilinmeyen -> nullopt.
inline constexpr int kBuiltinStages = 5;
std::optional<std::pair<int, int>> stage_position(std::string_view stage_key) noexcept;
// M04: tamamlanan asama orani (asama n basladiysa (n-1)/5). Sure degil; yalniz Calisiyor + Uygulama Testi.
std::optional<double> stage_fraction(const MeasureState& s) noexcept;
// M04: gosterilecek gecen sure (sn). Is surerken started_ms varsa simdiki zamandan; yoksa son bildirilen deger.
double display_elapsed_s(const MeasureState& s, std::int64_t now_ms) noexcept;

// M06: ekran okuyucu duyurusu (durum degisiminde bir kez). key katalog anahtaridir; UI metni birlestirir.
enum class AnnounceKind { Started, Stage, Stopping, Done, Cancelled, Failed };
struct Announcement {
    AnnounceKind kind = AnnounceKind::Started;
    bool completed = false;  // UIA ActionCompleted (bitis/iptal/hata) mi
};
std::optional<Announcement> measure_announcement(const MeasureState& before, const MeasureState& after) noexcept;
// 0..1; toplam bilinmiyorsa nullopt (belirsiz ilerleme sahte yuzdeyle gosterilmez).
std::optional<double> progress_fraction(const MeasureState& s) noexcept;

// Sayi bicimi: tr-TR ondalik virgul ("143,2"), en-US nokta. decimals: 0..2
std::string format_number(double v, int decimals, bool turkish);
// Eksik nedenin katalog anahtari ("missing.not_supported" ...); deger varsa bos.
std::string_view missing_key(contracts::MissingReason r) noexcept;
std::string_view diagnosis_key(Diagnosis d) noexcept;
std::string_view verdict_key(Verdict v) noexcept;
std::string_view confidence_key(contracts::Confidence c) noexcept;

}  // namespace ef::core::measure
