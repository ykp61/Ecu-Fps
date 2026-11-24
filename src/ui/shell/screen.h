#pragma once
// UI-01: kabuk ekrani (kenar cubugu + sayfalar). Cizim ve isabet testi; Win32 mesajlari
// main_window'dadir. Ayni kod ekran goruntusu/denetim aracinda da kullanilir.

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/i18n/catalog.h"
#include "core/measure/live_view.h"
#include "ui/layout/layout.h"
#include "ui/motion/motion.h"
#include "ui/render/graphics.h"
#include "ui/shell/modal.h"
#include "ui/shell/panel_text.h"
#include "ui/shell/view_model.h"
#include "ui/theme/ramp.h"
#include "ui/theme/theme.h"

namespace ef::ui {

// Odaklanabilir/tiklanabilir ogeler.
// RDY-01: sabit kimlikler 100..299 blogundadir; dinamik (satir/indeks) kimlikler kHitRanges tablosundaki
// [taban, taban + adet) bloklaridir. Hicbir blok digeriyle cakismaz (static_assert): bir kimlik tek anlama
// gelir, sayfaya gore yorum gerekmez. Yeni dinamik aralik eklerken kHitRanges'e de ekleyin.
enum class Hit : int {
    None = -1, Nav0 = 0,
    Toggle = 100, Yes = 101, No = 102, Retry = 103,
    SysTabCleanup = 104,  // Sistem araclari: "Temizlik" alt sekmesi (Baslangic alt sekmesi nav_hit(Startup)'tir)
    SettingsStrip = 105,  // Ayarlar: RGB isik seridi hareketi anahtari
    TopbarStatus = 106,   // Ust menu: Performans acik/kapali gostergesi (salt okunur UIA Text)
    OpenReadiness = 107,  // Panel: Oyun hazirligi karti "Duzelt" (P12-B1: sayfa degismez; otomatik duzeltilebilen bulgular tek Evet/Hayir'da). Kimlik / UIA adi eski
    // Oyunlar: eski "Neden?" baglantisi. Spec 2026-11-12 B2 ile satir ekrandan kalkti (metinleri oyun kartinin UIA
    // HelpText'inde); kimlik ve UIA adi (games.why) korunur, artik cizilmez.
    GamesWhy = 108,
    UpdateStatus = 109,   // Zorunlu guncelleme: ust menunun altindaki durum seridi (salt okunur UIA Text)
    SourceBuiltin = 110, SourceGame = 111, TestStart = 112, TestStop = 113, ModeQuick = 114, ModeFull = 115,
    SpeedStart = 117,  // Olcumler: internet hiz testi "Testi baslat" / "Yeniden test et" (Evet/Hayir App'te)
    SpeedStop = 118,   // Olcumler: internet hiz testi "Testi durdur"
    Autostart = 120,
    AppAnimations = 121,  // UI-11/R33: uygulama animasyonlari anahtari
    // Islem Gecmisi: journal kapsam filtresi (Tumu / Performans / Hazirlik / Baslangic); yalniz gorunumu suzer
    HistoryFilterAll = 122, HistoryFilterPerf = 123, HistoryFilterFixes = 124, HistoryFilterStartup = 125,
    // Zorunlu guncelleme karti: Simdi guncelle / Cik / Onceki ayarlara don (Evet/Hayir App'te) / Indirme sayfasini ac
    UpdateNow = 126, UpdateQuit = 127, UpdateRestore = 128, UpdateOpenPage = 129,
    UpdateRepairService = 196,  // ADR-033: kurulu surumde hizmet yok/bozuk -> "Yardimci hizmeti onar" (Evet/Hayir App'te)
    WelcomeStart = 130,
    DetectionRetry = 131,  // oyun tespiti engel kartinda gercek yeniden tarama
    OpenMeasure = 132,     // Genel Bakis: Test merkezine git
    GamesGuides = 135,     // Oyunlar: uzun rehberler ac/kapa
    GamesAddExe = 136,     // Oyunlar: elle oyun EXE'si ekle (EF-X11)
    // Oyunlar baslik seridi (spec 2026-11-11 §5): gorunum suzgeci (Tumu / Korumali / Onerili; yalniz gorunen kartlari
    // suzer) ve "Yeniden tara" (kitaplik + surec taramasini simdi yeniler; DetectionRetry ile ayni gercek tarama).
    GamesFilterAll = 137, GamesFilterProtected = 138, GamesFilterAdvice = 139,
    GamesRescan = 141,
    PerfKeep = 134,        // Kurtarma: dis degisiklik icin "Mevcut degeri koru"
    PerfDriftReapply = 158,  // Koruma Modu (T07): sapan ayarlari Evet/Hayir onayiyla yeniden uygula
    PerfDriftKeep = 159,     // Koruma Modu (T07): "Boyle kalsin" (mevcut degeri koru; ayar yazilmaz)
    StartupRescan = 163,   // Baslangic: okuma hatasinda yeniden tara
    // Uygulama ici kip penceresi (spec 2026-11-12 E; modal.h). Modal: pencerenin kendisi (UIA Window + IsModal; ad =
    // baslik). ModalTitle / ModalBody0-1: salt okunur UIA Text. ModalCheck: isaret kutusu. ModalNo / ModalYes: dugmeler.
    // Madde ve secenek satirlari dinamik bloklardadir (ModalList0 / ModalChoice0).
    Modal = 164, ModalTitle = 165, ModalBody0 = 166, ModalBody1 = 167, ModalCheck = 168, ModalNo = 169, ModalYes = 173,
    Details = 140,
    // Olcumler: Uygulama Testi kapsami (spec 2026-11-12 A5): Genel test | Denge testi (radyo; test islerken devre disi)
    ScopeGeneral = 142, ScopeBalance = 143,
    // Canli test ekrani (spec 2026-11-12 A4): Test ayarlarindaki anahtar ve canli gorunumun salt okunur UIA metinleri
    // (adim, kare hizi, yuk). Durdurma dugmesi mevcut TestStop'tur; ilerleme cubugu Progress0.
    LiveView = 144, LiveStep = 145, LiveFps = 146, LiveLoad = 147,
    CleanScan = 150, CleanRun = 151, CleanYes = 152, CleanNo = 153, CleanStop = 154,
    CleanScanCancel = 155,     // EXT-CLN: tarama surerken "Iptal"
    CleanStorageSettings = 156,  // EXT-CLN: yalniz ms-settings:storagesense acar (temizlik sayilmaz)
    CleanMore = 157,  // Spec 2026-11-14 B12: uzun kategori listesini ac/kapa ("+N kategori daha"; UIA Expander)
    // Guncellemeler (spec 2026-11-12 H): ust kartin "Denetle" / "Yeniden dene" dugmesi ve arama surerken "Durdur".
    UpdatesCheck = 148, UpdatesCancel = 149,
    StartupApply = 160, StartupYes = 161, StartupNo = 162,
    CheckStart = 170, CheckYes = 171, CheckNo = 172,
    ChkRescan = 180, ChkNetTest = 181,  // CHK: Oyun Hazirligi
    ChkCheckup = 174,  // Oyun hazirligi: "Sistem kontrolu · N bulgu" bolumunu ac/kapa (varsayilan kapali)
    // Ayarlar (spec 2026-11-12 B7): "Oyun onaylari · N" ve "Lisanslar" satirlarinin Goster / Gizle dugmeleri.
    SettingsGamesShow = 175, SettingsLicenses = 176,
    // Spec 2026-11-12 G: ilk acilis bildirimi seridi — "Kapat" (Windows ile baslat anahtarini kapatir) ve metin (UIA Text).
    AutostartNoticeOff = 177, AutostartNotice = 178,
    SupportDumps = 182, SupportCreate = 183,  // R21: destek paketi
    HistoryExport = 187,  // R16: secili yerel test gecmisini disa aktar (kullanici baslatir)
    // R13/INS-03: uzun test (isinma) baslat/durdur
    EnduranceStart = 188, EnduranceStop = 189,
    // P1: Performans etkisini olc (ABBA deneyi) baslat/durdur
    ProofStart = 218, ProofStop = 219,
    // P3: Darbogaz kartinda "Bu PC'de olculemedi" iken "Raporu kaydet" (destek paketi yolu; Masaustune yazar, gonderim yok)
    BalanceReport = 220,
    // Sistem araclari: ust seritteki "Bolumler" atlama dugmeleri (Temizlik / Baslangic / Hizmetler / Sistem).
    SysJump0 = 190, SysJump1 = 191, SysJump2 = 192, SysJump3 = 193,
    // ADR-033 / spec 3: kurulum penceresi (ayri pencere, ayni kimlik uzayi; sabit blok 100..299). SetupLicenseClose:
    // lisans ve gizlilik panelini kapatir.
    SetupCancel = 200, SetupInstall = 201, SetupRetry = 202, SetupInstallAnyway = 203, SetupStop = 204, SetupClose = 205,
    SetupOpen = 206, SetupAutostart = 207, SetupLicense = 208, SetupLicenseClose = 209,
    // Spec 8: Ui kurulum yeri disinda calisirken ust menunun altindaki "Kurulum gerekli" seridi: "Kur" ve metin (UIA Text).
    InstallOfferInstall = 210, InstallOfferText = 211,
    SettingsUpdateCheck = 212,  // Ayarlar: "Yazilim guncellemesi · Denetle" (Ecu Fps el ile guncelleme denetimi)
    SettingsLanguage = 213,     // Ayarlar: "Dil · Degistir" (denetim 5.2; secim kip penceresinde)
    // Spec 2026-11-14 B13: Gecmis baslik seridindeki gorunum cipleri (Tumu / Degisiklikler / Testler; yalniz gorunum)
    HistoryTabAll = 215, HistoryTabChanges = 216, HistoryTabTests = 217,
    // EF-X25 T04: Performans plani "Gelismis (N)" satiri (varsayilani Hayir gruplari ac/kapa; yalniz gorunum durumu; UIA Expander)
    PerfAdvanced = 221,
    // EF-X25 T20: Panel perf kartinda "Oyun ici oneriler" satiri (yalniz gorunum durumu; UIA Expander); Oyun hazirligi
    // "Sistem kontrolu" kartinda XMP bulgusunun "Adim adim" BIOS rehberi (yalniz okunur metin; UIA Expander); Ayarlar'da
    // yasal sayfa baglantilari (kullanici tiklayinca tarayicida acilir).
    PerfTips = 222, BiosGuideToggle = 223,
    SettingsLinkEula = 224, SettingsLinkPrivacy = 225, SettingsLinkTransparency = 226,
    // P12-B3: Ayarlar "Gelismis" acilir satiri (varsayilan kapali; yalniz gorunum durumu; UIA Expander settings.advanced.toggle)
    SettingsAdvanced = 227,
    SettingsCleanupHint = 228,  // R-C2: Ayarlar: oyun kapaninca temizlik bildirimi anahtari
    ModalInput = 229,           // R-C4: kip penceresinde tek satir yazi alani (UIA Edit; modal.h input_*)
    SpeedServerPing = 230,      // R-C4: Olcumler: hiz karti "Sunucu ping" (adres kip penceresinde sorulur)
    // Dinamik bloklar (taban + indeks; adetler kHitRanges'te)
    ConsentToggle0 = 300,   // + satir (kMaxConsentRows)
    ConsentReset0 = 600,    // + satir (kMaxConsentRows)
    Group0 = 1000,          // + grup (kMaxGroupRows)
    CleanToggle0 = 1100,    // + kategori satiri (kMaxCleanupRows)
    CleanFiles0 = 1200,     // + kategori satiri: dosya listesini ac/kapa (EXT-CLN-03; kMaxCleanupRows)
    CleanVolume0 = 1300,    // + birim (EXT-CLN-01; kMaxCleanupVolumeHits)
    CheckToggle0 = 1700,    // + satir (OPT-07; kMaxCheckRows)
    StorageChoice0 = 1800,  // + secim (0 = yapma, k = birim k-1; BENCH-05)
    GameGuide0 = 2000,      // + oyun satiri (GUIDE-01; kMaxGameRows)
    // + bulgu (kutucugun ana bulgusu): Oyun hazirligi kutucugundaki "Ayrinti" ac/kapa (spec 2026-11-14 B11; UIA Expander
    // readiness.detail.N; kMaxReadinessRows)
    ChkDetail0 = 2200,
    ChkUndo0 = 2300,        // + bulgu: "Geri al" (UIA readiness.undo.N; kMaxReadinessRows)
    ChkKeep0 = 2400,        // + bulgu: "Mevcut degeri koru" (UIA readiness.keep.N; kMaxReadinessRows)
    ChkAction0 = 2500,      // + bulgu: "Duzelt" (UIA readiness.action.N; kMaxReadinessRows)
    Progress0 = 2600,  // + indeks: salt okunur ilerleme cubugu (UIA ProgressBar + RangeValue; etkilesimsiz)
    Status0 = 2700,    // + indeks: salt okunur durum metni (UIA Text; etkilesimsiz)
    StartupToggle0 = 3000,  // + satir (WIN-09; kMaxStartupRows)
    HistorySelect0 = 4500,  // + test gecmisi satiri: sec (R16; kMaxHistoryTestRows)
    StartupServiceToggle0 = 4600,  // + hizmet satiri: oturumluk durdurma secimi (WIN-10; kMaxServiceRows)
    GameFullscreen0 = 5000,  // + oyun satiri: tam ekran iyilestirmeleri kapat/geri al (F-47; kMaxGameRows)
    GameNvidia0 = 5300,      // + oyun satiri: NVIDIA oyun profili uygula/geri al (A1.1; kMaxGameRows)
    GameManualRemove0 = 5600,  // + oyun satiri: elle ekleneni listeden cikar (EF-X11; kMaxGameRows)
    HistoryRow0 = 6000,  // + journal kaydi (view.rows indeksi): satir ayrintisini ac/kapa (spec 2026-11-11 §5; kMaxHistoryRows)
    ModalList0 = 7000,    // + madde: kip penceresi madde listesi satiri (salt okunur UIA Text; kModalMaxList)
    ModalChoice0 = 7100,  // + secenek: kip penceresi tek secimli liste (UIA RadioButton; kModalMaxChoices)
    GameCard0 = 7300,     // + oyun satiri: oyun kartini sec (spec 2026-11-12 B2; UIA RadioButton games.card.N; kMaxGameRows)
    // + kayit: Olcumler "Son 3 test" satiri; basinca o testin "Sonuc ayrintisi" satirin altinda acilir / kapanir
    // (spec 2026-11-12 C; UIA Button measure.recent.N, Selected = acik; kMaxMeasureRecent).
    MeasureRecent0 = 7600,
    // + journal kaydi (view.rows indeksi): acilan satirdaki "Geri almak icin Panel'e git" (sahibi olan sayfaya gecer;
    // spec 2026-11-12 B6; UIA Button history.go.N; kMaxHistoryRows).
    HistoryGo0 = 7700,
    // Guncellemeler (spec 2026-11-12 H): + baglanti: satirdaki "<Uretici> sayfasi" dugmesi (resmi sayfa varsayilan
    // tarayicida acilir; UIA Button updates.link.N; kMaxUpdatesLinks) ve + satir: salt okunur satir metni (UIA Text
    // updates.row.N; kMaxUpdatesRows).
    UpdatesLink0 = 8200,
    UpdatesRow0 = 8300,
    // Spec H2: + satir (scan.updates indeksi): Windows Update onerisinin "Kur" dugmesi (UIA Button updates.install.N;
    // kMaxUpdatesRows). Firmware satirinda yoktur.
    UpdatesInstall0 = 8400,
    // ADR-032: + satir (gpus indeksi): ekran karti satirindaki "Dosyadan kur" (kopya surerken "Durdur"; UIA Button
    // updates.local.N; kMaxUpdatesLinks).
    UpdatesLocal0 = 8500,
    // + grup (FindingGroup): "Sistem kontrolu" grup kutucugu — o grubun bulgularini ac/kapa (spec 2026-11-14 B11; UIA
    // Expander readiness.group.N; kFindingGroupCount).
    ChkGroup0 = 8600,
};
inline constexpr int kMaxUpdatesLinks = 8;
inline constexpr int kMaxUpdatesRows = 64;
inline constexpr int kMaxConsentRows = 250;
inline constexpr int kMaxReadinessRows = 99;
inline constexpr int kMaxGameRows = 200;
inline constexpr int kMaxGroupRows = 100;
inline constexpr int kMaxCleanupRows = 100;
inline constexpr int kMaxCleanupVolumeHits = 50;
inline constexpr int kMaxCheckRows = 100;
inline constexpr int kMaxStartupRows = 1000;
inline constexpr int kMaxServiceRows = 8;  // WIN-10: derlenmis hizmet izin listesi kucuk; UI siniri ayri
inline constexpr int kMaxHistoryRows = 400;  // build_history varsayilan ust siniri 200 kayit
inline constexpr int kMaxHistoryTestRows = static_cast<int>(ViewModel::HistoryPageView::kMaxHistoryTests);
inline constexpr int kMaxStorageChoices = static_cast<int>(core::measure::kMaxStorageRoots) + 1;  // 0 = yapma
inline constexpr int kMaxMeasureRecent = 3;  // spec 2026-11-12 C: en yeni uc test (core::insights::kMaxEntries)
inline constexpr int kFixedHitCount = 200;  // sabit kimlik blogu: 100..299
inline constexpr int kMaxProgressItems = 100;
inline constexpr int kMaxStatusItems = 100;
// Spec 2026-11-14 B11: "Sistem kontrolu" bulgu gruplari (gercek bulgu kaynaklarina gore; sira = ekrandaki sira).
enum class FindingGroup : std::uint8_t { Power, Security, Hardware, Storage, Updates, Other };
inline constexpr int kFindingGroupCount = 6;

// RDY-01: kimlik bloklari. Fixed: tek tek adlandirilan sabit kimliklerin blogu.
enum class HitKind : std::uint8_t {
    None, Nav, Fixed, ConsentToggle, ConsentReset, Group, CleanToggle, CleanFiles, CleanVolume, CheckToggle,
    StorageChoice, GameGuide, GameFullscreen, GameNvidia, ChkUndo, ChkKeep, ChkAction, Progress, Status, StartupToggle,
    HistorySelect, StartupServiceToggle, GameManualRemove, HistoryRow, ModalList, ModalChoice, GameCard, MeasureRecent,
    HistoryGo, UpdatesLink, UpdatesRow, UpdatesInstall, UpdatesLocal, ChkDetail, ChkGroup,
};
struct HitRange {
    HitKind kind;
    int base;
    int count;
    const wchar_t* uia_prefix;  // dinamik UIA AutomationId oneki (+ indeks); Nav/Fixed icin nullptr
};
inline constexpr HitRange kHitRanges[] = {
    {HitKind::Nav, static_cast<int>(Hit::Nav0), static_cast<int>(kPageCount), nullptr},
    {HitKind::Fixed, static_cast<int>(Hit::Toggle), kFixedHitCount, nullptr},
    {HitKind::ConsentToggle, static_cast<int>(Hit::ConsentToggle0), kMaxConsentRows, L"settings.consent."},
    {HitKind::ConsentReset, static_cast<int>(Hit::ConsentReset0), kMaxConsentRows, L"settings.reset."},
    {HitKind::Group, static_cast<int>(Hit::Group0), kMaxGroupRows, L"perf.group."},
    {HitKind::CleanToggle, static_cast<int>(Hit::CleanToggle0), kMaxCleanupRows, L"cleanup.category."},
    {HitKind::CleanFiles, static_cast<int>(Hit::CleanFiles0), kMaxCleanupRows, L"cleanup.files."},
    {HitKind::CleanVolume, static_cast<int>(Hit::CleanVolume0), kMaxCleanupVolumeHits, L"cleanup.volume."},
    {HitKind::CheckToggle, static_cast<int>(Hit::CheckToggle0), kMaxCheckRows, L"check.item."},
    {HitKind::StorageChoice, static_cast<int>(Hit::StorageChoice0), kMaxStorageChoices, L"measure.storage."},
    {HitKind::GameGuide, static_cast<int>(Hit::GameGuide0), kMaxGameRows, L"games.pro."},
    {HitKind::GameFullscreen, static_cast<int>(Hit::GameFullscreen0), kMaxGameRows, L"games.fs."},
    {HitKind::GameNvidia, static_cast<int>(Hit::GameNvidia0), kMaxGameRows, L"games.nv."},
    {HitKind::ChkUndo, static_cast<int>(Hit::ChkUndo0), kMaxReadinessRows, L"readiness.undo."},
    {HitKind::ChkKeep, static_cast<int>(Hit::ChkKeep0), kMaxReadinessRows, L"readiness.keep."},
    {HitKind::ChkAction, static_cast<int>(Hit::ChkAction0), kMaxReadinessRows, L"readiness.action."},
    {HitKind::Progress, static_cast<int>(Hit::Progress0), kMaxProgressItems, L"progress."},
    {HitKind::Status, static_cast<int>(Hit::Status0), kMaxStatusItems, L"status."},
    {HitKind::StartupToggle, static_cast<int>(Hit::StartupToggle0), kMaxStartupRows, L"startup.item."},
    {HitKind::StartupServiceToggle, static_cast<int>(Hit::StartupServiceToggle0), kMaxServiceRows, L"startup.service."},
    {HitKind::HistorySelect, static_cast<int>(Hit::HistorySelect0), kMaxHistoryTestRows, L"history.test."},
    {HitKind::GameManualRemove, static_cast<int>(Hit::GameManualRemove0), kMaxGameRows, L"games.remove."},
    {HitKind::HistoryRow, static_cast<int>(Hit::HistoryRow0), kMaxHistoryRows, L"history.op."},
    {HitKind::ModalList, static_cast<int>(Hit::ModalList0), static_cast<int>(kModalMaxList), L"modal.list."},
    {HitKind::ModalChoice, static_cast<int>(Hit::ModalChoice0), static_cast<int>(kModalMaxChoices), L"modal.choice."},
    {HitKind::GameCard, static_cast<int>(Hit::GameCard0), kMaxGameRows, L"games.card."},
    {HitKind::MeasureRecent, static_cast<int>(Hit::MeasureRecent0), kMaxMeasureRecent, L"measure.recent."},
    {HitKind::HistoryGo, static_cast<int>(Hit::HistoryGo0), kMaxHistoryRows, L"history.go."},
    {HitKind::UpdatesLink, static_cast<int>(Hit::UpdatesLink0), kMaxUpdatesLinks, L"updates.link."},
    {HitKind::UpdatesRow, static_cast<int>(Hit::UpdatesRow0), kMaxUpdatesRows, L"updates.row."},
    {HitKind::UpdatesInstall, static_cast<int>(Hit::UpdatesInstall0), kMaxUpdatesRows, L"updates.install."},
    {HitKind::UpdatesLocal, static_cast<int>(Hit::UpdatesLocal0), kMaxUpdatesLinks, L"updates.local."},
    {HitKind::ChkDetail, static_cast<int>(Hit::ChkDetail0), kMaxReadinessRows, L"readiness.detail."},
    {HitKind::ChkGroup, static_cast<int>(Hit::ChkGroup0), kFindingGroupCount, L"readiness.group."},
};

constexpr bool hit_ranges_disjoint() {
    constexpr std::size_t n = std::size(kHitRanges);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& a = kHitRanges[i];
        if (a.base < 0 || a.count <= 0) return false;
        for (std::size_t j = i + 1; j < n; ++j) {
            const auto& b = kHitRanges[j];
            if (a.kind == b.kind) return false;
            if (a.base < b.base + b.count && b.base < a.base + a.count) return false;
        }
    }
    return true;
}
// UI-16: sabit (adlandirilmis) kontrollerin tek kaynagi. Her oge MainWindow::activate'te islenir ve
// hit_automation_id'de UIA kimligi vardir; birim testi listeyi bu iki sozlesmeye karsi dogrular.
inline constexpr Hit kFixedHits[] = {
    Hit::Toggle, Hit::Yes, Hit::No, Hit::Retry, Hit::SysTabCleanup, Hit::SettingsStrip, Hit::SettingsCleanupHint, Hit::TopbarStatus, Hit::OpenReadiness, Hit::GamesWhy,
    Hit::SourceBuiltin, Hit::SourceGame, Hit::ScopeGeneral, Hit::ScopeBalance, Hit::LiveView, Hit::LiveStep, Hit::LiveFps, Hit::LiveLoad,
    Hit::TestStart, Hit::TestStop, Hit::ModeQuick, Hit::ModeFull, Hit::SpeedStart, Hit::SpeedStop, Hit::Autostart,
    Hit::AppAnimations, Hit::HistoryFilterAll, Hit::HistoryFilterPerf, Hit::HistoryFilterFixes, Hit::HistoryFilterStartup,
    Hit::UpdateStatus, Hit::UpdateNow, Hit::UpdateQuit, Hit::UpdateRestore, Hit::UpdateOpenPage, Hit::UpdateRepairService,
    Hit::WelcomeStart, Hit::DetectionRetry, Hit::OpenMeasure, Hit::PerfKeep, Hit::PerfDriftReapply, Hit::PerfDriftKeep, Hit::StartupRescan, Hit::GamesGuides, Hit::GamesAddExe,
    Hit::GamesFilterAll, Hit::GamesFilterProtected, Hit::GamesFilterAdvice, Hit::GamesRescan,
    Hit::Details, Hit::CleanScan, Hit::CleanRun, Hit::CleanYes, Hit::CleanNo, Hit::CleanStop,
    Hit::CleanScanCancel, Hit::CleanStorageSettings, Hit::CleanMore,
    Hit::StartupApply, Hit::StartupYes, Hit::StartupNo, Hit::CheckStart, Hit::CheckYes,
    Hit::CheckNo, Hit::ChkRescan, Hit::ChkNetTest, Hit::ChkCheckup,
    Hit::SupportDumps, Hit::SupportCreate,
    Hit::SettingsGamesShow, Hit::SettingsLicenses, Hit::AutostartNoticeOff, Hit::AutostartNotice,
    Hit::HistoryExport, Hit::EnduranceStart, Hit::EnduranceStop, Hit::ProofStart, Hit::ProofStop, Hit::BalanceReport, Hit::SysJump0, Hit::SysJump1, Hit::SysJump2, Hit::SysJump3,
    Hit::Modal, Hit::ModalTitle, Hit::ModalBody0, Hit::ModalBody1, Hit::ModalCheck, Hit::ModalNo, Hit::ModalYes,
    Hit::UpdatesCheck, Hit::UpdatesCancel,
    Hit::SetupCancel, Hit::SetupInstall, Hit::SetupRetry, Hit::SetupInstallAnyway, Hit::SetupStop, Hit::SetupClose, Hit::SetupOpen,
    Hit::SetupAutostart, Hit::SetupLicense, Hit::SetupLicenseClose, Hit::InstallOfferInstall, Hit::InstallOfferText,
    Hit::SettingsUpdateCheck, Hit::SettingsLanguage,
    Hit::HistoryTabAll, Hit::HistoryTabChanges, Hit::HistoryTabTests, Hit::PerfAdvanced,
    Hit::PerfTips, Hit::BiosGuideToggle, Hit::SettingsLinkEula, Hit::SettingsLinkPrivacy, Hit::SettingsLinkTransparency,
    Hit::SettingsAdvanced, Hit::ModalInput, Hit::SpeedServerPing,
};
constexpr bool hit_fixed_ids_in_fixed_block() {
    const auto& f = kHitRanges[1];
    if (f.kind != HitKind::Fixed) return false;
    for (const Hit h : kFixedHits) {
        const int v = static_cast<int>(h);
        if (v < f.base || v >= f.base + f.count) return false;
    }
    return true;
}
static_assert(hit_ranges_disjoint(), "RDY-01: Hit kimlik bloklari ust uste biniyor");
static_assert(hit_fixed_ids_in_fixed_block(), "RDY-01: sabit Hit kimligi 100..299 blogu disinda");
static_assert(static_cast<int>(ViewModel::kMaxCleanupVolumes) <= kMaxCleanupVolumeHits, "RDY-01: temizlik birim blogu dar");
static_assert(ViewModel::HistoryPageView::kMaxHistoryTests <= static_cast<std::size_t>(kMaxHistoryTestRows),
              "RDY-01: gecmis test satiri blogu dar");

// Kimlik -> (blok, indeks); bloklar disi -> {None, 0}. Fixed icin indeks tabandan farktir.
struct HitSlot {
    HitKind kind = HitKind::None;
    std::size_t index = 0;
    constexpr bool operator==(const HitSlot&) const = default;
};
constexpr HitSlot classify_hit(Hit h) {
    const int v = static_cast<int>(h);
    for (const auto& r : kHitRanges)
        if (v >= r.base && v < r.base + r.count) return {r.kind, static_cast<std::size_t>(v - r.base)};
    return {};
}
// (blok, indeks) -> kimlik; indeks blok disindaysa Hit::None.
constexpr Hit hit_of(HitKind k, std::size_t i) {
    for (const auto& r : kHitRanges)
        if (r.kind == k) return i < static_cast<std::size_t>(r.count) ? static_cast<Hit>(r.base + static_cast<int>(i)) : Hit::None;
    return Hit::None;
}
// Dinamik bloklarin gercekten ayri oldugunu derleme aninda ornekle de dogrula (RDY-01 senaryosu).
static_assert(classify_hit(Hit::ChkAction0) == HitSlot{HitKind::ChkAction, 0});
static_assert(classify_hit(Hit::CleanVolume0) == HitSlot{HitKind::CleanVolume, 0});
static_assert(hit_of(HitKind::ChkAction, static_cast<std::size_t>(kMaxReadinessRows)) == Hit::None);
// UIA AutomationId (sabit ve dinamik kimlikler; bilinmeyen -> "unknown"). Saf islev: birim testte dogrulanir.
std::wstring hit_automation_id(Hit h);
inline Hit consent_toggle_hit(std::size_t i) { return static_cast<Hit>(static_cast<int>(Hit::ConsentToggle0) + static_cast<int>(i)); }
inline Hit consent_reset_hit(std::size_t i) { return static_cast<Hit>(static_cast<int>(Hit::ConsentReset0) + static_cast<int>(i)); }
// Hit -> (tur, satir): tur 1 = anahtar, 2 = kaldir; satir disi -> {0, 0}
std::pair<int, std::size_t> consent_hit_row(Hit h);
inline Hit nav_hit(std::size_t i) { return static_cast<Hit>(static_cast<int>(i)); }

// UI-04: UI Automation rolu (ekran okuyucu icin kontrol turu). ProgressBar/Text/Window etkilesimsizdir:
// UIA agacinda gorunur, Tab sirasina girmez, fareyle etkinlestirilmez. CheckBox: isaret kutusu (Toggle deseni).
// Window: uygulama ici kip penceresi (spec 2026-11-12 E; UIA Window + IsModal; diger kip ogeleri onun cocuklaridir).
// Denetim 5.4: Expander = acilir bolum basligi (UIA Button + Invoke + ExpandCollapse; selected = acik).
// Denetim 5.3: Edit = yazilabilir alan (UIA Edit + Value; deger HitMeta::value, yazma UiaModel::set_value). B10'dan beri
// bu rolde oge yok (hedef FPS alani kaldirildi) ve pencere set_value baglamaz; rol yeni bir alan icin altyapidir.
enum class AccRole { Button, TabItem, ToggleSwitch, RadioButton, ProgressBar, Text, CheckBox, Window, Expander, Edit };
inline constexpr bool interactive(AccRole r) { return r != AccRole::ProgressBar && r != AccRole::Text && r != AccRole::Window; }

// DS-12: alfa olcegi adlandirildi. Degerler tasarim token'i degil, ayni yuzeyde tutarlilik icin kullanilan
// karisim oranlaridir (yeni bir yuzey eklerken bu adlardan secin; ciplak 0.5/0.6 yazmayin).
namespace alpha_scale {
inline constexpr float kDisabled = 0.5f;     // devre disi kontrol
inline constexpr float kHoverTint = 0.6f;    // hover karisim orani
inline constexpr float kPressTint = 0.18f;   // basma vurgusu
inline constexpr float kSelectionTint = 0.12f;  // secili cip zemini
inline constexpr float kQuietSelectionTint = 0.06f;
inline constexpr float kBadgeTint = 0.16f;   // durum rozeti zemini
inline constexpr float kSoftOutline = 0.55f;  // yumusak vurgu kenari
inline constexpr float kOutline = 0.6f;      // vurgu/uyari kenari
// DS-12: bilesen ici karisim oranlari (token degil; ayni tur ogelerde tutarlilik icin adlandirildi).
inline constexpr float kBusyText = 0.6f;         // is surerken ikincil metin
inline constexpr float kSoftHoverTint = 0.4f;    // ikincil dugme hover tonu
inline constexpr float kPressFill = 0.6f;        // basili baglanti dolgusu
inline constexpr float kLiftTint = 0.22f;        // birincil dugme basma kaldirmasi
inline constexpr float kRingTint = 0.3f;         // guc halkasi basma tonu
inline constexpr float kQuietSurfaceMix = 0.5f;  // pasif yuzey karisimi
inline constexpr float kRecommendedTint = 0.18f; // onerilen eylem dolgusu
inline constexpr float kChipHoverTint = 0.08f;   // secim cipi/radyo hover tonu
inline constexpr float kWarningChipTint = 0.18f; // uyari rozeti zemini
inline constexpr float kQuietOutline = 0.5f;     // basari kartinin yumusak kenari
}  // namespace alpha_scale

// DS-01: yari saydam durum rozeti zemininde (tone %16) okunur metin rengi. Metin, gercek karisim zemini
// uzerinde >= 4.5:1 olacak sekilde gerekirse ana metin rengine dogru karistirilir.
// Buyuk harf (Turkce: i -> İ, ı -> I). Mono teknik etiketler (maket "D3D11 ARENA · 15 SN", "BAZ: ... · ARENA").
std::wstring locale_upper(std::wstring_view s, bool turkish);
inline Color badge_text_color(const Palette& p, const Color& tone) {
    return ensure_contrast(tone, mix(p.surface, tone, alpha_scale::kBadgeTint), p.text_primary, 4.5);
}

// Zehir yesili (spec §2/§4): durum etiketi (chip) tonu. Renk tek basina anlam tasimaz; chip her zaman kelime tasir.
enum class ChipTone { Good, Warn, Danger, Neutral };
namespace alpha_scale {
inline constexpr float kChipTint = 0.12f;         // chip zemini: yuzey + ton %12
inline constexpr float kChipNeutralTint = 0.08f;  // notr chip zemini: yuzey + ikincil metin %8
inline constexpr float kPrimaryHoverLift = 0.25f; // ana dugme hover: accent -> acik ton
inline constexpr float kPrimaryPressDim = 0.2f;   // ana dugme basili: accent -> koyu ton
inline constexpr float kPrimaryGlowIdle = 0.6f;   // ana dugme parlama gucu (hover'da 1.0)
inline constexpr float kToggleOnTrack = 0.35f;    // acik anahtar izi: yuzey + accent %35
inline constexpr float kDangerCardFill = 0.06f;   // tehlike karti zemini: arka plan + hata %6
inline constexpr float kDangerCardBorder = 0.35f; // tehlike karti cizgisi: yuzey + hata %35
inline constexpr float kIconBoxTint = 0.12f;      // ikon kutusu zemini: yuzey + ikon rengi %12 (spec 2026-11-11 §4)
inline constexpr float kHudScrim = 0.86f;         // canli test gorunumu: onizleme ustundeki gosterge paneli zemini (arka plan rengi)
inline constexpr float kHudBorder = 0.7f;         // ayni panelin 1 DIP cizgisi (kenarlik rengi)
inline constexpr float kDivider = 0.35f;          // kart ici ayirici cizgi: zemin + kenarlik %35
inline constexpr float kModalScrim = 0.72f;       // kip penceresi: icerigin ustundeki karartma (arka plan rengi)
inline constexpr float kCardHoverTint = 0.5f;     // oyun karti hover: yuzey -> yukseltilmis yuzey ve kenarlik -> metin orani
inline constexpr float kPulseDim = 0.7f;          // "Calisiyor" noktasi: nabzin en sonuk aninda opaklik kaybi
inline constexpr float kSourceOffText = 0.5f;     // kapali / desteklenmeyen oyun kaynagi cipi: metin -> zemin orani (soluk)
// Koyu tema kart imzasi: kenar cizgisi yuzey + kenarlik %85. Yumusak, ama metin disi sinir esigi (>= 3:1, hem kart
// yuzeyine hem arka plana karsi) korunur; %65 bu esigin altina dusuyordu (test_live_tiles: card_signature_border).
inline constexpr float kSignatureCardBorder = 0.85f;
}  // namespace alpha_scale
// Koyu tema kart cizgisi (draw_card imzasi). Testler bu turetilmis rengin karsitligini denetler, yalniz token'i degil.
inline Color signature_card_border(const Palette& p) { return mix(p.surface, p.border, alpha_scale::kSignatureCardBorder); }
// Koyu tema derinlik efektleri (sahip onayli BOOST maketi, index.html <style>; degerler CSS'ten birebir). Yalniz koyu
// tema; yuksek kontrastta hicbiri cizilmez. Efekt / ek pencere yok: gradyan ve katmanli dolgu (Direct2D temel ilkeleri).
namespace depth {
inline constexpr Color rgba(int r, int g, int b, float a) { return {r / 255.f, g / 255.f, b / 255.f, a}; }
// body: radial-gradient(ellipse 65% 40% at 50% 0%, rgba(61,255,122,.07), transparent 65%),
//       radial-gradient(ellipse 50% 35% at 85% 20%, rgba(0,212,255,.06), transparent 60%)
inline constexpr Color kGlowGreen = rgba(61, 255, 122, 0.07f);
inline constexpr float kGlowGreenRx = 0.65f, kGlowGreenRy = 0.40f, kGlowGreenCx = 0.50f, kGlowGreenCy = 0.f, kGlowGreenEnd = 0.65f;
inline constexpr Color kGlowCyan = rgba(0, 212, 255, 0.06f);
inline constexpr float kGlowCyanRx = 0.50f, kGlowCyanRy = 0.35f, kGlowCyanCx = 0.85f, kGlowCyanCy = 0.20f, kGlowCyanEnd = 0.60f;
// .card-v2: linear-gradient(170deg, rgba(19,25,37,.92), rgba(10,14,21,.97)); box-shadow 0 10px 26px rgba(0,0,0,.40),
// inset 0 1px 0 rgba(255,255,255,.06); ::before / ::after 7x7, 1.5px, radius 9 (kutuya sigdirilir), opaklik .48.
inline constexpr float kCardAngleDeg = 170.f;
inline constexpr Color kCardTop = rgba(19, 25, 37, 0.92f), kCardBottom = rgba(10, 14, 21, 0.97f);
inline constexpr float kShadowDy = 10.f, kShadowBlur = 26.f;
inline constexpr Color kShadow = rgba(0, 0, 0, 0.40f);
inline constexpr Color kCardInsetTop = rgba(255, 255, 255, 0.06f);
inline constexpr float kCornerBox = 7.f, kCornerStroke = 1.5f, kCornerOpacity = 0.48f;
// .well-v2: rgba(4,6,10,.82) + radial-gradient(rgba(255,255,255,.045) 1px, transparent 0) / 10px 10px;
// border 1px rgba(255,255,255,.055); inset 0 2px 8px rgba(0,0,0,.65); radius 8px.
inline constexpr Color kWellFill = rgba(4, 6, 10, 0.82f), kWellDot = rgba(255, 255, 255, 0.045f), kWellBorder = rgba(255, 255, 255, 0.055f);
inline constexpr float kWellDotR = 1.f, kWellPitch = 10.f, kWellRadius = 8.f, kWellInsetDy = 2.f, kWellInsetBlur = 8.f, kWellInsetA = 0.65f;
// #hero-glow: w-80 h-36 rounded-full blur-3xl (320 x 144, 64px); Kapali bg-[#FF5C7A]/10, Acik bg-[#3DFF7A]/18 (JS).
inline constexpr float kHeroGlowW = 320.f, kHeroGlowH = 144.f, kHeroGlowBlur = 64.f, kHeroGlowOff = 0.10f, kHeroGlowOn = 0.18f;
// .btn-primary: linear-gradient(180deg, #52FF8B, #2EE66B); 0 0 18px rgba(61,255,122,.22) + inset 0 1px 0 rgba(255,255,255,.35);
// hover: brightness(1.06), 0 0 26px rgba(61,255,122,.40) + inset .45.
inline constexpr Color kBtnTop = rgba(0x52, 0xFF, 0x8B, 1.f), kBtnBottom = rgba(0x2E, 0xE6, 0x6B, 1.f);
inline constexpr Color kBtnGlow = rgba(61, 255, 122, 0.22f), kBtnGlowHover = rgba(61, 255, 122, 0.40f);
inline constexpr float kBtnGlowBlur = 18.f, kBtnGlowBlurHover = 26.f, kBtnInset = 0.35f, kBtnInsetHover = 0.45f, kBtnHoverBright = 1.06f;
// .sec-rule-line: linear-gradient(90deg, rgba(61,255,122,.28), rgba(0,212,255,.12), rgba(255,255,255,.02)).
inline constexpr Color kRuleA = rgba(61, 255, 122, 0.28f), kRuleB = rgba(0, 212, 255, 0.12f), kRuleC = rgba(255, 255, 255, 0.02f);
// Kart gradyaninin arka plan uzerindeki opak karsiligi (karti opak cizer: hizli yollar ayni pikselleri verir; maketteki
// %8 / %3 saydamlik altindaki zemin isiltisi < 1 renk adimi). Metin karsitligi testleri bu iki uc rengi denetler.
inline Color over(const Color& bg, const Color& c) { return mix(bg, c.with_alpha(1.f), c.a).with_alpha(1.f); }
inline Color card_top(const Palette& p) { return over(p.background, kCardTop); }
inline Color card_bottom(const Palette& p) { return over(p.background, kCardBottom); }
// Panel (BOOST maketi, 2026-11-16 birebir): .card-v2 border-radius 10px (diger sayfalar radius_card token'inda kalir).
inline constexpr float kPanelCardRadius = 10.f;
// .btn-primary border-radius 7px; .btn-secondary 6px, h 28, px 12, 12px/500, zemin beyaz %3, cizgi beyaz %12
// (hover: cizgi + metin vurgu, zemin vurgu %8).
inline constexpr float kPanelPrimaryRadius = 7.f;
inline constexpr Color kQuietFill = rgba(255, 255, 255, 0.03f), kQuietBorder = rgba(255, 255, 255, 0.12f);
// Panel buton disi rozet (bg-white/[0.04], rounded 4px) ve #btn-power-circle zemini (#080C13, hover #101724).
inline constexpr Color kBadgeFill = rgba(255, 255, 255, 0.04f);
inline constexpr Color kPowerDisc = rgba(0x08, 0x0C, 0x13, 1.f), kPowerDiscHover = rgba(0x10, 0x17, 0x24, 1.f);
// Kadran SVG'si (viewBox 96, 92 DIP dugmeye olcekli): r 45 noktali halka beyaz %14 (cizgi 2.5, kesik 1.5 / 6.35), r 40 iz
// beyaz %7 (cizgi 4) ve durum halkasi; guc simgesi 32 DIP kutuda (viewBox 40: yay r 11, cizgi 3.2).
inline constexpr Color kDialDots = rgba(255, 255, 255, 0.14f), kDialTrack = rgba(255, 255, 255, 0.07f);
// #hero card enerji hatlari (viewBox 1100 x 130, preserveAspectRatio none): Kapali rgba(255,92,122,.28); uc noktalari
// camgobegi / vurgu %60, r 2.5.
inline constexpr float kConduitViewW = 1100.f, kConduitViewH = 130.f, kConduitW = 1.5f, kConduitDotR = 2.5f;
// Ayrintili yazi renkleri (Tailwind slate-200 / slate-300).
inline constexpr Color kSlate200 = rgba(0xE2, 0xE8, 0xF0, 1.f), kSlate300 = rgba(0xCB, 0xD5, 0xE1, 1.f);
}  // namespace depth
struct ChipColors {
    Color bg, fg;
};
// Chip zemini yuzey uzerinde opak karisim; metin gercek zemin uzerinde >= 4.5:1 (gerekirse ana metne dogru acilir).
inline ChipColors chip_colors(const Palette& p, ChipTone tone) {
    Color c = p.text_secondary;
    float k = alpha_scale::kChipNeutralTint;
    if (tone == ChipTone::Good) c = p.accent, k = alpha_scale::kChipTint;
    else if (tone == ChipTone::Warn) c = p.warning, k = alpha_scale::kChipTint;
    else if (tone == ChipTone::Danger) c = p.error, k = alpha_scale::kChipTint;
    const Color bg = mix(p.surface, c, k);
    return {bg, ensure_contrast(c, bg, p.text_primary, 4.5)};
}
// Dugme gorunumu. Recommended: onerilen guvenli satir eylemi (accent cerceve/metin, parlamasiz; RDY-04).
// DangerPrimary: kip penceresinde yikici islemin ana dugmesi (hata rengiyle dolu; spec 2026-11-12 E).
enum class ButtonKind { Primary, Secondary, Danger, Recommended, DangerPrimary };

struct HitMeta {
    std::wstring name;        // ekran okuyucunun okuyacagi ad (gorunen metin)
    AccRole role = AccRole::Button;
    bool selected = false;    // sekme/secim
    int toggle = -1;          // anahtar: 0 kapali, 1 acik, -1 yok
    bool enabled = true;      // false: UIA'da IsEnabled=false kalir; tiklanmaz/etkinlesmez, Tab sirasinda atlanir
    bool tab_stop = true;     // TH-03: roving radyo grubunda secili olmayan secenek Tab duragi degildir (ok tuslariyla gelinir)
    bool offscreen = false;   // govdenin gorunur alani disinda (kaydirmayla gelir): UIA IsOffscreen, fare isabeti yok
    double range_value = -1;  // ProgressBar: 0..100 (UIA RangeValue); <0 yok
    std::wstring help;        // RDY-12: ek durum metni (UIA HelpText); Name'e eklenmez. Bos = yok
    std::wstring value;       // Edit: alanin metni (UIA Value)
};

struct HitMap {
    std::vector<std::pair<Hit, RectF>> items;  // DIP, pencere koordinati (UIA agac sirasi)
    std::map<Hit, HitMeta> meta;
    bool focus_drawn = false;  // odak halkasi bu karede cizildi mi (denetim)
    // Simetri denetimi (spec 2026-11-12 §2/§5; pencere koordinati): cizilen her esit satirin hucre dikdortgenleri ve her
    // kart cercevesi (draw_card / log_card). Etkilesim tasimaz; yalniz ui_snapshot denetimi okur.
    RowLog rows;
    std::vector<RectF> cards;
    // Fare hedefi: yalniz ekrandaki, etkin ve etkilesimli ogeler (devre disi oge tiklanmaz, el imleci gostermez).
    Hit at(float x, float y) const;
    // UIA ElementProviderFromPoint: ekrandaki her oge (devre disi ve salt okunur dahil).
    Hit element_at(float x, float y) const;
    // Klavye/UIA/fare ile etkinlestirilebilir mi: kayitli, etkin ve etkilesimli.
    bool activatable(Hit id) const;
    // TH-03: Tab duragi mi: etkinlestirilebilir ve tab_stop (roving radyo secenekleri hariç).
    bool tabbable(Hit id) const;
    const RectF* rect_of(Hit id) const;
    // Hit::None (blok sinirini asan satir, hit_of) kaydedilmez: kimliksiz oge etkilesimsiz cizilir.
    void add(Hit id, const RectF& r, std::wstring name, AccRole role, bool selected = false, int toggle = -1, bool enabled = true) {
        if (id == Hit::None) return;
        items.push_back({id, r});
        HitMeta m;
        m.name = std::move(name);
        m.role = role;
        m.selected = selected;
        m.toggle = toggle;
        m.enabled = enabled;
        meta[id] = std::move(m);
    }
    // Salt okunur ilerleme (0..100); ekran okuyucu RangeValue ile okur.
    void add_progress(Hit id, const RectF& r, std::wstring name, double percent) {
        if (id == Hit::None) return;
        add(id, r, std::move(name), AccRole::ProgressBar);
        meta[id].range_value = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    }
    // Salt okunur durum metni (UIA Text).
    void add_text(Hit id, const RectF& r, std::wstring name) { add(id, r, std::move(name), AccRole::Text); }
};

// EF-A11Y: cizilen salt okunur metin satirlari icin sirali Status kimligi. Sayfa kendi sabit indekslerinden
// sonra baslatir; blok dolarsa Hit::None doner (HitMap::add kaydetmez, cizim degismez).
struct StatusSeq {
    std::size_t next = 0;
    Hit take() { return hit_of(HitKind::Status, next++); }
};
// EF-A11Y: UIA adinda cumleleri birlestirir; onceki parca zaten noktalama ile bitiyorsa ". " yerine " " (".." olmaz).
inline std::wstring a11y_join(std::wstring a, std::wstring_view b) {
    if (b.empty()) return a;
    if (a.empty()) return std::wstring(b);
    const wchar_t last = a.back();
    a += (last == L'.' || last == L'!' || last == L'?' || last == L':' || last == L'…') ? L" " : L". ";
    a += b;
    return a;
}

// Islem Gecmisi filtresi: journal kaydinin kapsamina gore (veri degismez, yalniz gorunen kartlar suzulur).
// Perf = "user"/"machine" (ve bilinmeyen kapsam: sayfa onu Performans olarak etiketler), Fixes = "fixes"/"machine_fixes",
// Startup = "startup".
enum class HistoryFilter { All, Perf, Fixes, Startup };
bool history_filter_match(HistoryFilter f, std::string_view scope);
// Filtreye uyan rows indeksleri (sira korunur).
std::vector<std::size_t> history_filter_rows(const core::recovery::HistoryView& v, HistoryFilter f);
inline Hit history_filter_hit(HistoryFilter f) { return static_cast<Hit>(static_cast<int>(Hit::HistoryFilterAll) + static_cast<int>(f)); }
// Spec 2026-11-14 B13: Gecmis gorunumu (baslik seridi cipleri). Changes: yalniz degisiklik karti, Tests: yalniz test karti.
// HistoryFilter (Performans / Hazirlik / Baslangic) degisiklik kartinin alt suzgecidir.
enum class HistoryTab { All, Changes, Tests };
inline Hit history_tab_hit(HistoryTab v) { return static_cast<Hit>(static_cast<int>(Hit::HistoryTabAll) + static_cast<int>(v)); }
inline constexpr bool history_shows_changes(HistoryTab v) { return v != HistoryTab::Tests; }
inline constexpr bool history_shows_tests(HistoryTab v) { return v != HistoryTab::Changes; }
// Gun basligi anahtari (yerel takvim gunu, yyyymmdd; 0 = gecersiz zaman). Bugun / Dun karsilastirmasi icin.
int history_day_key(std::int64_t unix_ms);
// Journal satirinin oturum ici kimligi (acik/kapali ayrinti durumu icin): kapsam + islem kimligi. Satir indeksi yeni
// kayit eklenince kayar; bu anahtar kaymaz.
inline std::string history_row_key(const core::recovery::HistoryTxRow& r) { return r.scope + "/" + r.id; }

// Siki yerlesim (spec 2026-11-11 §5) sayfa izgaralari. Hepsi saf: birim testte dogrulanir.
// Olcumler ust satiri: govde >= kMeasureTop3MinW ise uc es kart (Sonuc | Kare suresi | Internet hizi); daha darsa
// Sonuc ile Internet hizi yan yana, Kare suresi altlarinda tam genislikte; iki kart da sigmiyorsa alt alta (1).
inline constexpr float kMeasureTop3MinW = 1000.f;
inline constexpr float kMeasureTopCardMinW = 300.f;
inline int measure_top_columns(float body_w, float gap) {
    if (body_w >= kMeasureTop3MinW) return 3;
    return (body_w - gap) / 2 >= kMeasureTopCardMinW ? 2 : 1;
}
// Temizlik kategorileri: tek kart icinde iki sutun satir; sutun kCleanupColMinW altina dusuyorsa tek sutun.
inline constexpr float kCleanupColMinW = 380.f;
inline int cleanup_columns(float inner_w, float col_gap) { return (inner_w - col_gap) / 2 >= kCleanupColMinW ? 2 : 1; }
// Ayarlar (spec 2026-11-12 §3): esit satirlar GENEL | GORUNUM ve Destek paketi | Kaldirma; oyun onaylari (liste) ve
// HAKKINDA tam genislik. Sutun kSettingsColMinW altina dusuyorsa tek sutun (kartlar dogal boyunda alt alta).
inline constexpr float kSettingsColMinW = 400.f;
inline int settings_columns(float body_w, float gap) { return (body_w - gap) / 2 >= kSettingsColMinW ? 2 : 1; }
// Spec 2026-11-12 B7: Ayarlar tek, ortali liste. Genislik icerik alaninin kSettingsListShare'i (1180 DIP pencerede
// ~760 DIP), en cok kSettingsListMaxW; bu oran kSettingsListMinW'nin altina dusen dar pencerede tam genislik.
inline constexpr float kSettingsListShare = 1.0f;
inline constexpr float kSettingsListMaxW = 780.f;  // maket: max-w-[780px]
inline constexpr float kSettingsListMinW = 560.f;
inline float settings_list_width(float body_w) {
    if (body_w <= 0) return 0.f;
    const float w = body_w * kSettingsListShare;
    if (w < kSettingsListMinW) return body_w;
    return std::floor(std::min(w, kSettingsListMaxW));
}
// Esit satir eslestirmesi (spec 2026-11-12 §1a/b): icerigi degisken bilgi kartlari yalniz benzer boydaysa yan yana
// eslenir — uzun kart kisa kartin en cok bu kati. Aksi halde kisa kart bos uzatilmaz; eslenmeyen kart tam genislikte alt alta
// durur (CardFlow::pair_max_ratio). Panel alt kartlari, Oyunlar alt kartlari ve Olcumler sonuc kartlari bu kurali
// kullanir; etkilesimli kartlar (Olcumler "Test ayarlari", Ayarlar, Gecmis'te test gecmisi) olculen boya gore degil
// sabit kuralla yerlesir (tiklarken / yazarken yerlesim degismez).
inline constexpr float kPairMaxRatio = 1.6f;
// Gecmis: test gecmisi karti satir tasiyorsa liste kartidir -> tam genislik (akran karti altinda). Kural olculen boya
// degil satir sayisina baglidir: kart icindeki "Sec" dugmesi / disa aktarma durumu kartin boyunu degistirdiginde
// yerlesim yan yana <-> alt alta degismez. Satir yokken iki kisa bilgi karti her zaman esit satirda eslenir.
inline constexpr bool history_tests_full_width(std::size_t test_rows) { return test_rows > 0; }
// Kare suresi cubuklari: saniye basina FPS serisinden o saniyenin ortalama kare suresi (ms = 1000 / FPS). Gecersiz
// (<= 0, sonlu olmayan) ornekler atlanir; uydurma deger uretilmez. En fazla max_bars cubuk (esit kovali ortalama).
std::vector<double> frame_time_bars(const std::vector<double>& fps_series, std::size_t max_bars);
// Cubuk grafigi icin gereken en az seri uzunlugu (daha kisa seride "veri yok").
inline constexpr std::size_t kFrameBarsMinPoints = 4;

// Oyunlar suzgeci (spec 2026-11-11 §5): yalniz gorunen oyun kartlarini suzer; veri ve satir kimlikleri (oyun indeksi)
// degismez. Protected = bilinen koruma sistemi olan oyun ("Korumali" cipi); Advice = salt okunur risk taramasinda
// onerisi olan oyun ("{n} oneri" cipi).
enum class GamesFilter { All, Protected, Advice };
bool games_filter_match(GamesFilter f, const ViewModel::GameRowView& g);
// Suzgece uyan vm.games indeksleri (sira korunur).
std::vector<std::size_t> games_filter_rows(const std::vector<ViewModel::GameRowView>& games, GamesFilter f);
inline Hit games_filter_hit(GamesFilter f) { return static_cast<Hit>(static_cast<int>(Hit::GamesFilterAll) + static_cast<int>(f)); }
// Spec 2026-11-12 B2: oyun karti en cok iki durum etiketi tasir. Oncelik: Calisiyor > Korumali > "N oneri" > Bir kez
// baslat > Hazir (ilk ikisi alinir); gorunen sira: once yasam durumu (Calisiyor / Bir kez baslat / Hazir), sonra
// Korumali, sonra "N oneri". Calisiyor: izleyici oyunu acik goruyor. Bir kez baslat: oynanabilir EXE yolu henuz
// cozulmedi (fs_known == false; oyun bir kez baslatilinca cozulur). Hazir: EXE yolu biliniyor, oyun kapali.
// Kartta gorunmeyen durumlar kaybolmaz: kartin UIA HelpText'inde ve secili oyun seridinde durur.
enum class GameChip : std::uint8_t { Running, StartOnce, Ready, Protected, Advice };
struct GameChips {
    std::array<GameChip, 2> items{};
    std::size_t count = 0;
};
GameChips game_status_chips(const ViewModel::GameRowView& g);
// Ince oyun karti (spec 2026-11-12 B2): 32 DIP ikon kutusu + ad / magaza (tek satir) + tek satir durum etiketi.
// Butun kartlar ayni boydadir.
// BOOST maketi (2026-11-16 birebir, .game-card): card-v2 p-3 (12) + 1 DIP kenarlik -> icerik kenardan kGameCardInset;
// ust satir: 32 DIP kutu (r 6) + ad 13px/600 + magaza · surum 11px; gap-2.5 (10); etiket satiri 11px + py 2 (20.5).
inline constexpr float kGameIconTile = 32.f;
inline constexpr float kGameCardInset = 13.f, kGameCardGap = 10.f, kGameChipPadY = 2.f;
inline float game_chip_h(const Theme& t) { return t.role(Role::Small11).line + 2 * kGameChipPadY; }
inline float game_card_h(const Theme& t) {
    const float head = std::max(kGameIconTile, t.role(Role::Semi13).line + t.role(Role::Small11).line);
    return kGameCardInset + head + kGameCardGap + game_chip_h(t) + kGameCardInset;
}
// Secili oyunun oturum ici anahtari: kaynak + kurulum klasoru + ad. Oyun indeksi yeniden taramada kayabilir; anahtar
// kaymaz (liste degisince secim ayni oyunda kalir, oyun listeden cikarsa secim duser).
std::string game_row_key(const ViewModel::GameRowView& g);
// Anahtarin gosterdigi oyunun vm.games indeksi (bos anahtar / oyun yok -> nullopt).
std::optional<std::size_t> games_selected_row(const std::vector<ViewModel::GameRowView>& games, std::string_view key);
// Ok tuslari (spec 2026-11-12 B2): odak bir oyun kartindayken sol / sag komsu karta, yukari / asagi ayni sutunda bir
// satir yukari / asagi gecer (uclarda sarmaz). order: gorunen kart sirasi (oyun indeksleri); cols: izgara sutun sayisi.
// Odak kart degilse ya da o yonde kart yoksa Hit::None (cagiran eski davranisi surdurur: kaydirma / sekme).
Hit games_arrow_target(const std::vector<std::size_t>& order, int cols, Hit focus, int dx, int dy);
// Oyun karti kisaltma kutusu: adin ilk iki sozcugunun ilk harfleri, buyuk harf ("Counter-Strike 2" -> "CS"); tek
// sozcukte ilk iki harf ("Fortnite" -> "FO"). Sozcuk = harf/rakam dizisi (kesme isareti sozcugu bolmez). UTF-8;
// turkish=true: Turkce buyuk harf kurali (i -> İ, ı -> I), aksi halde i -> I. Harf/rakam yoksa bos.
std::string game_initials(std::string_view name_utf8, bool turkish);
// Oyun karti surum metni: launcher'in ham surum dizgisi cok uzun olabilir ("++Fortnite+Release-42.40-CL-59086525-Windows").
// Kartta en cok max_chars karakter (UTF-8 kod noktasi) ve ardindan "…" gosterilir; kisa surum oldugu gibi kalir. Tam
// metin kaybolmaz: kartin UIA metin ogesinin adinda kesilmeden durur. Cok baytli karakter bolunmez; kesim yerindeki
// sondaki bosluk atilir. Gecersiz UTF-8 bayti tek karakter sayilir.
inline constexpr std::size_t kGameVersionMaxChars = 24;
std::string game_version_label(std::string_view version_utf8, std::size_t max_chars = kGameVersionMaxChars);
// Oyun karti dugme satiri: dugmeler soldan saga dizilir, sigmayan alt satira iner (kart genisligi = avail). Dogal ic
// boslukla (pad) dizilim compact_pad ile dizilimden DAHA COK satir tutuyorsa siki ic bosluk kullanilir (kart kisalir);
// satir sayisi degismiyorsa dogal genislikler kalir. Genislik = max(min_w, ceil(metin) + 2 * ic bosluk), avail ile sinirli.
std::vector<float> game_button_widths(const std::vector<float>& text_w, float avail, float gap, float pad, float compact_pad, float min_w);
// Ayni dizilim kuraliyla satir sayisi (0 dugme -> 0).
std::size_t game_button_rows(const std::vector<float>& widths, float avail, float gap);

// Siki yerlesim (spec 2026-11-11 §4): Panel canli kutucuklari. Sira: CPU, RAM, GPU, Disk, Ag. Kutucuk guncel ornegi
// gosterir (ikon kutusu, deger, 12 parcali LED cubuk, iki satir ayrinti); sahip onayli maket (2026-11-16) ile LED'in
// altinda son canli orneklerin ince grafigi (ViewModel::live_history) ve deger satirinda zaman tabanli tepe yazilir.
enum class LiveTile : std::size_t { Cpu = 0, Ram, Gpu, Disk, Net, Count_ };
inline constexpr std::size_t kLiveTileCount = static_cast<std::size_t>(LiveTile::Count_);
static_assert(kLiveTileCount == ViewModel::kLivePeakCount, "ViewModel::live_peak_pct kutucuk sirasini izler");
// Tepe izi (tek istisna "yalniz guncel ornek" kuralina): koyu temada LED cubugunda son tepenin parcasi soluk yanar.
// MainWindow her canli ornekte (set_live) cagirir; zaman ornek gelisinde okunur, ek kare istenmez. Sonuc
// ViewModel::live_peak_pct'ye yazilir (cizim yalniz modele bakar: tam kare ve bolge cizimi ayni). still: azaltilmis
// / duraklatilmis harekette tepe kademeli inmez (ramp.h led_peak_update).
void update_live_peaks(std::array<LedPeak, kLiveTileCount>& peaks, std::array<double, kLiveTileCount>& shown,
                       const core::metrics::LiveSnapshot& l, std::int64_t now_ms, bool still);
// Kutucuk ince grafigi (sahip onayli maket 2026-11-16): her canli ornekte kutucuk yuzdesi (LED ile ayni kaynak; ag:
// indirme / baglanti) kutucugun halkasina eklenir, en fazla ViewModel::kLiveHistoryMax ornek tutulur. Olculemeyen ornek
// NaN olarak eklenir (grafikte bosluk). Cizim yalniz modele bakar.
void push_live_history(std::array<std::vector<float>, kLiveTileCount>& history, const core::metrics::LiveSnapshot& l);
// Kutucuk gecisi: Full = tam karenin parcasi (bolge ve yerlesim kaydedilir); Region = hizli yol (yalniz cizim; kayit
// degismez); Measure = yalniz yerlesim (hicbir sey cizilmez / kaydedilmez; satir boyu degisti mi denetimi).
enum class TilePass { Full, Region, Measure };
// Kutucuk izgarasi: tek sirada bes esit sutun; kutucuk kLiveTileMinW'den dar kaliyorsa 3 + 2, o da darsa tek sutun.
inline constexpr float kLiveTileMinW = 170.f;
inline int live_tile_columns(float body_w, float gap) {
    if ((body_w - 4 * gap) / 5 >= kLiveTileMinW) return 5;
    if ((body_w - 2 * gap) / 3 >= kLiveTileMinW) return 3;
    return 1;
}
// Ag kutucugu degeri: Mbit/s -> MB/s (= / 8), her zaman bir ondalik (Turkcede ','; yarim yukari yuvarlanir):
// 1000 Mbit/s -> "125,0". Gecersiz/negatif -> "—".
std::wstring net_mbs_text(double mbit_per_s, bool turkish);
// Baglanti hizi metni: >= 1000 Mbit/s Gbps ("1", "2,5", "10"), aksi halde tam sayi Mbps. Gecersiz / <= 0 -> bos deger.
struct LinkText {
    std::wstring value;  // yalniz sayi; bos = bilinmiyor
    bool gbps = false;
};
LinkText net_link_text(double mbit_per_s, bool turkish);

// Panel Sistem karti hucresi: ana satir (model/ad) + ikinci satir (ayrinti). Bos ana satir = okunamadi.
struct SysCell {
    std::string main, detail;
    friend bool operator==(const SysCell&, const SysCell&) = default;
};
// Envanter metnini boler: ilk satirin ilk " · " ayiricisina kadar ana satir, kalani ayrinti; sonraki satirlar
// (ikinci GPU) ayrintiya " · " ile eklenir: "A · B" + yeni satir + "C" -> {"A", "B · C"}.
SysCell sys_cell_split(std::string_view text);
// Windows metni: "Windows 11 Pro 26H2 (26300.9711)" -> {"Windows 11 Pro 26H2", "26300.9711"}; parantez yoksa ayrinti bos.
SysCell sys_os_split(std::string_view os);

struct UiState {
    Page page = Page::Overview;
    Hit hover = Hit::None;
    Hit focus = Hit::None;
    bool focus_visible = false;  // klavye ile gelindiyse odak halkasi
    bool reveal_focus = false;   // RDY-10: sonraki cizimde odakli ogeyi gorunur alana kaydir (Tab/UIA ile odak)
    bool reduced_motion = false;
    float scroll = 0;            // govde kaydirma (DIP)
    float content_height = 0;    // son cizimde govde icerik yuksekligi
    float view_top = 0, view_bottom = 0;  // gorunur govde araligi (icerik koordinati; sanal listeler icin)
    std::size_t rows_drawn = 0;           // son cizimde cizilen liste satiri (denetim/olcum)
    std::array<Tween, kPageCount> nav_hover{};
    // DS-02: her etkilesimli ogenin kendi hover/basma gecisi (paylasilan slotlar capraz hover uretiyordu).
    std::map<Hit, Tween> hover_tween;
    std::map<Hit, Tween> press_tween;
    Hit pressed = Hit::None;   // fare basili oge (basma geri bildirimi; birakinca temizlenir)
    std::size_t readiness_rows = 0;          // CHK: cizilen bulgu sayisi
    std::vector<bool> readiness_actions;     // bulgu basina dugme var mi (odak sirasi)
    // Panel "Oyun hazirligi" kartindaki bulgu satirlarinin eylem dugmeleri (Oyun hazirligi sayfasiyla ayni kimlik:
    // ChkAction k); gorunen sirayla (odak sirasi: "Duzelt"in ardindan).
    std::vector<std::size_t> panel_readiness_actions;
    std::vector<bool> readiness_undos;       // bulgu basina "Geri al" dugmesi var mi (odak sirasi)
    std::vector<bool> readiness_keeps;       // bulgu basina "Mevcut degeri koru" dugmesi var mi (odak sirasi)
    std::vector<std::size_t> readiness_order;  // gorunen satir sirasi (ONERILER sonra IYI DURUMDA); bos = indeks sirasi
    bool readiness_busy = false;             // tarama/ag testi/yonetici islemi suruyor
    bool readiness_net = false;              // ag testi dugmesi var mi
    std::vector<bool> game_guides;           // GUIDE-01: oyun satirinda "Profesyonel ayarlar" dugmesi
    std::vector<bool> game_nv;               // A1.1: oyun satirinda NVIDIA profil dugmesi
    std::vector<bool> game_fs;               // F-47: oyun satirinda tam ekran iyilestirmeleri dugmesi
    std::vector<bool> game_manual;           // EF-X11: elle eklenen oyun satirinda "Listeden cikar"
    Tween toggle_hover{0}, toggle_knob{0}, page_fade{1};
    Tween progress_fill{0};  // ilerleme cubugu (gercek adim orani hedef)
    Tween card_fade{1};      // asama degisiminde kart icerigi gecisi
    Tween brand_reveal{1};   // UI-08: pencere acilisinda logo bir kez belirir (sonra statik)
    // Canli kutucuk sayilari (LiveTile sirasi). Azaltilmis hareket / oyun acikken sure 0 (aninda).
    std::array<Tween, kLiveTileCount> live_value{};
    std::array<bool, kLiveTileCount> live_seen{};  // onceki karede deger var miydi (eksikten donuste gecis yok)
    // Task 12: hiz testi kadran ibresi (azaltilmis harekette aninda) ve odak sirasi durumu (cizimde yazilir).
    Tween speed_needle{0};
    bool speed_running = false, speed_can_start = false;
    bool server_ping_can_start = false;  // R-C4: "Sunucu ping" dugmesi etkin (kart cizilince yazilir)
    // Guncellemeler (spec 2026-11-12 H; cizimde yazilir): arama suruyor mu (ust kart dugmesi "Durdur") ve cizilen resmi
    // uretici sayfasi dugmesi sayisi (UpdatesLink0..; odak sirasi).
    bool updates_checking = false;
    std::size_t updates_links = 0;
    std::vector<std::size_t> updates_installs;  // spec H2: cizilen "Kur" dugmeleri (scan.updates indeksi; gorunen sira)
    std::vector<Hit> updates_actions;  // ADR-032: cizilen satir eylemleri (Kur / Indir ve kur / Dosyadan kur; gorunen sira)
    bool strip_live = false;  // RGB serit kayiyor mu (MainWindow strip_should_animate ile yazar); false: sabit gradyan
    bool show_welcome = false;  // odak sirasi icin
    bool games_guides_open = true;  // pencere kapali baslatir; cizim denetimi en genis durumu gorur
    bool readiness_checkup_open = false;  // Oyun hazirligi: "Sistem kontrolu" bolumu acik mi (varsayilan kapali)
    // Spec 2026-11-14 B11 (yalniz gorunum durumu; oturum ici): "Ayrinti"si acik kutucuklarin ana bulgu kimlikleri ve
    // acik bulgu gruplari (bit = FindingGroup). Asagidaki iki dizi cizimde yazilir (odak sirasi): kutucuk ana bulgusu
    // (rv.checks indeksi) basina "Ayrinti" cizildi mi; cizilen grup kutucuklari (gorunen sira).
    std::set<std::string> readiness_detail_open;
    std::uint32_t readiness_group_open = 0;
    std::vector<bool> readiness_details;
    std::vector<FindingGroup> readiness_groups;
    bool games_why = false;         // Oyunlar: eski "Neden?" baglantisi cizildi mi (spec 2026-11-12 B2: artik hep false)
    GamesFilter games_filter = GamesFilter::All;  // Oyunlar gorunum suzgeci (oturum ici; kalici degil)
    bool games_filters = false;     // suzgec cipleri baslik seridinde cizildi mi (odak sirasi; cizilmediyse suzgec Tumu sayilir)
    bool games_rescan = false;      // baslik seridinde "Yeniden tara" cizildi mi (odak sirasi)
    // BOOST maketi: "Oyun EXE'si ekle" baslik seridinde "Yeniden tara"nin solunda; serit dar ise kaynak kartinda (odak sirasi).
    bool games_add_header = false;
    // Spec 2026-11-12 B2: secili oyun (game_row_key; oturum ici, kalici degil; bos = secim yok). Karta basinca / odaktayken
    // Enter ile secilir; secili oyunun seridi izgaranin altinda acilir. Asagidakiler cizimde yazilir:
    // game_cards[i]: i. oyunun karti cizildi (secilebilir); game_card_order: gorunen kart sirasi (oyun indeksleri; odak
    // sirasi ve ok tuslari); games_cols: izgara sutun sayisi (yukari / asagi ok bir satir atlar).
    std::string games_selected;
    std::vector<bool> game_cards;
    std::vector<std::size_t> game_card_order;
    int games_cols = 1;
    // "Calisiyor" noktasinin nabzi (MainWindow pulse_should_animate ile yazar; false: sabit nokta). pulse_regions:
    // tamami gorunur govdedeki noktalarin kutulari (pencere koordinati, DIP); zamanlayici yalniz bunlari gecersiz kilar
    // (Screen::paint_pulse_region).
    bool pulse_live = false;
    std::vector<RectF> pulse_regions;
    // Secili oyun seridinin cercevesi (pencere koordinati, DIP; serit yoksa bos). games_reveal_strip: secim yeni yapildi
    // (MainWindow yazar) — sonraki cizimde serit gorunur alana kaydirilir (izgara uzunsa serit ekranin altinda kalmasin).
    RectF games_strip;
    bool games_reveal_strip = false;
    bool readiness_rescan = true;   // Oyun hazirligi baslik seridinde "Yeniden tara" cizildi mi (odak sirasi)
    bool perf_conflict = false;
    bool perf_drift = false;  // Koruma Modu: sapma bildirimi dugmeleri gorunur (Kapali/Acik, ayrinti kapali)
    bool detection_retry = false;  // oyun tespiti engel kartinda "Yeniden tara" gorunur
    bool game_blocked = false;  // EXT-AC-02: oyun calisirken VEYA tespit guvenilir degilken Evet/Uygula cizilmez
    bool details_open = false;  // UI-10: "neleri degistirir" paneli acik mi
    bool perf_tips_open = false;   // T20: "Oyun ici oneriler" acik mi (yalniz gorunum durumu; kalici degil)
    bool perf_tips_shown = false;  // T20: son cizimde satir gorunuyor mu (odak sirasi)
    bool bios_guide_open = false;  // T20: XMP bulgusunun "Adim adim" BIOS rehberi acik mi (yalniz gorunum durumu)
    bool bios_guide_shown = false; // T20: son cizimde rehber dugmesi gorunuyor mu (odak sirasi)
    bool advanced_open = false;  // T04: "Gelismis (N)" grup listesi acik mi (yalniz gorunum durumu; kalici degil)
    std::size_t group_advanced_count = 0;      // T04: son cizimde gorunen gelismis grup sayisi (0 = satir yok)
    std::vector<bool> group_advanced;          // T04: grup indeksi -> gelismis mi (odak sirasi: ana gruplar, satir, gelismis)
    std::size_t group_count = 0;
    std::vector<bool> group_enabled;  // odak sirasi icin: degistirilebilir grup anahtarlari
    core::cleanup::SessionPhase cleanup_phase = core::cleanup::SessionPhase::Idle;  // WIN-08
    std::vector<bool> cleanup_toggles;
    std::size_t cleanup_volumes = 0;      // odak sirasi: secilebilir birim cipleri
    std::vector<bool> cleanup_file_buttons;  // satir basina "dosyalar" dugmesi var mi
    bool cleanup_can_run = false;
    // Spec 2026-11-14 B12 (yalniz gorunum durumu): uzun kategori listesi acik mi; "+N kategori daha" dugmesi cizildi mi
    // (odak sirasi).
    bool cleanup_more_open = false;
    bool cleanup_more = false;
    core::startup::StartupPhase startup_phase = core::startup::StartupPhase::Idle;  // WIN-09
    std::vector<bool> startup_toggles;
    std::vector<bool> startup_service_toggles;  // WIN-10: hizmet satiri secimi (UIA startup.service.N)
    bool startup_can_apply = false;
    core::perf::CheckPhase check_phase = core::perf::CheckPhase::Idle;  // OPT-07
    std::vector<bool> check_toggles;
    core::perf::Phase perf_phase = core::perf::Phase::Off;  // odak sirasi icin son asama
    core::measure::MeasureState measure;                    // odak sirasi icin son olcum durumu
    std::size_t consent_rows = 0;                           // odak sirasi icin ayarlar satir sayisi
    // Ayarlar (spec 2026-11-12 B7): "Oyun onaylari" ve "Lisanslar" acik mi. Pencere kapali baslatir; cizim denetimi
    // en genis durumu gorur.
    bool settings_games_open = true;
    bool settings_licenses_open = true;
    // P12-B3: "Gelismis" acilir (Windows ile baslat, animasyonlar, serit, oyun onaylari, destek paketi); pencere kapali baslatir.
    bool settings_advanced_open = true;
    bool autostart_notice = false;  // spec G: ilk acilis bildirimi seridi cizildi (odak sirasi icin; her cizimde yazilir)
    bool install_offer = false;     // ADR-033 spec 8: "Kurulum gerekli" seridi cizildi (odak sirasi; her cizimde yazilir)
    // R16: Islem Gecmisi sayfasindaki test kayitlari (secim) ve disa aktarma dugmesi (odak sirasi icin;
    // cizimde her karede yazilir).
    std::size_t history_test_rows = 0;
    HistoryFilter history_filter = HistoryFilter::All;  // journal kapsam filtresi (oturum ici; kalici degil)
    // Spec 2026-11-14 B13: alt suzgec cipleri degisiklik kartinin baslik seridinde (govde; odak sirasi satirlardan once).
    // "Tumu" alt cipi ilk cip olarak cizilir (tum kayitlara acik donus; secili cipe yeniden basmak bir sey degistirmez).
    bool history_filters = false;                       // filtre cipleri cizildi mi (odak sirasi)
    HistoryTab history_tab = HistoryTab::All;           // B13 gorunum (oturum ici; kalici degil)
    bool history_tabs = false;                          // gorunum cipleri cizildi mi (odak sirasi)
    // B13: degisiklik / test kartlarinin son cizimdeki yuksekligi (cerceve icerikten once); degisince ayni kare yeniden cizilir.
    std::array<float, 2> history_card_h{};
    bool history_relayout = false;
    // Spec 2026-11-12 B6: sayisi 0 olan suzgec cipi cizilmez; cizilenler (HistoryFilter sirasiyla; odak sirasi).
    std::array<bool, 4> history_filter_shown{true, true, true, true};
    bool history_export_enabled = false;
    // Journal satir ayrintilari: acik satirlarin anahtarlari (history_row_key; oturum ici). history_all_open cizim
    // denetimi icin en genis durumdur (varsayilan); pencere kapali baslatir.
    bool history_all_open = true;
    std::set<std::string> history_open;
    std::vector<std::size_t> history_op_rows;           // ac/kapa dugmesi cizilen journal satirlari (view.rows indeksi; gorunen sira)
    // Olcumler "Son 3 test" (spec 2026-11-12 C): cizilen satir sayisi (odak sirasi) ve ayrintisi acik satir (-1 = yok;
    // oturum ici). Sonuc karti sayilari sayfaya girince / deger degisince sayac gibi dolar (motion.result_count);
    // darbogaz cubuklari acilista dolar (motion.bars_fill). seen: bu sayfa gosteriminde deger cizildi mi.
    std::size_t measure_recent_rows = 0;
    int measure_recent_open = -1;
    std::array<Tween, 4> measure_num{};
    std::array<bool, 4> measure_num_seen{};
    Tween measure_bars{1};
    bool measure_bars_seen = false;
    bool balance_report_busy = false;  // P3: "Raporu kaydet" hazirlaniyor (kart yazar; dugme devre disi, Tab almaz)
    // Zorunlu guncelleme karti (cizimde yazilir; odak sirasi yalniz kart dugmeleridir).
    bool update_gate = false, update_not_writable = false, update_busy = false, update_restore = false, update_repair = false;
    // Canli test ekrani (spec 2026-11-12 A4). live_test: cizilecek en yeni ornek (Screen::set_live_sample yazar).
    // live_view: bu karede icerik alani canli gorunum olarak cizildi (odak sirasi: sekmeler + "Testi durdur").
    // live_region: canli gorunumun yeniden cizilen pencere bolgesi (DIP; ust menunun alti). Yuk cubuklari ornekler
    // arasinda kayar (azaltilmis harekette aninda); kendi zamanlayicilari yoktur, onizleme karesiyle birlikte cizilir.
    core::measure::LiveSample live_test;
    bool live_view = false;
    RectF live_region;
    Tween live_cpu{0}, live_gpu{0};
    bool live_cpu_seen = false, live_gpu_seen = false;
    bool measure_live_toggle = false;  // Test kartinda "Canli test ekrani" anahtari cizildi mi (odak sirasi)
    // Test kartinda secenek notu cizildi mi ve notlarin alt kenari (isabet koordinati: kaydirma dusulmus; anlik denetim)
    bool measure_option_note = false;
    float measure_option_note_bottom = 0;
    // ---- Panel hareketi (spec 2026-11-12 F) ----
    // motion_paused: MainWindow yazar — pencere gorunmuyor, oyun acik, test / olcum suruyor ya da pilde: deger
    // gecisleri aninda oturur (zamanlayici kare istemez). Azaltilmis hareket ayri bayraktir (reduced_motion).
    bool motion_paused = false;
    // Performans bolumu sayilari (SON TEST FPS, %1 DUSUK, HAZIRLIK iyi sayisi): sayfaya girince / deger degisince
    // sayac gibi akar (motion.count). seen: bu sayfa gosteriminde deger cizildi mi.
    std::array<Tween, 3> panel_num{};
    std::array<bool, 3> panel_num_seen{};
    // Oyun hazirligi cubugu: ilk gosterimde dolar (motion.value_ease).
    Tween readiness_fill{1};
    bool readiness_fill_seen = false;
    // Oyun hazirligi halkasi (spec 2026-11-12 B3): sayfaya her giriste yay oranina dolar (motion.ring_fill).
    Tween readiness_ring{1};
    bool readiness_ring_seen = false;
    // Yuvarlak Performans dugmesinin hareketi (MainWindow breathe_should_animate ile yazar): acikken nefes alan dis
    // halka, kapaliyken kalp atisi; false: sabit halka. power_region: dugme + halka bolgesi (pencere koordinati, DIP);
    // yalniz tamami gorunur govdedeyken dolu. Zamanlayici yalniz bu bolgeyi gecersiz kilar (paint_power_region).
    bool breathe_live = false;
    RectF power_region;
    // Dugmeyi tasiyan kart (pencere koordinati; hizli yol kart yuzeyinin gradyanini ayni kutuyla yeniden cizer) ve
    // Panel kahraman parlamasi cizildi mi (yalniz Panel; Olcumler Test kartinda yok).
    RectF power_card;
    bool power_glow = false;
    // Panel genis duzeninde kahraman kartin enerji hatlari cizildi (hizli yol ayni hatlari dugmenin altinda yeniden cizer).
    bool power_conduits = false;
    // Koyu tema kart golgeleri (pencere koordinati, cizim sirasi): bolgesel cizim komsu kartlarin bolgeye dusen
    // golgesini ayni sirayla yeniden cizer. tiles_shadow_begin / end: kutucuklarin kendi golgeleri bu araliktadir.
    std::vector<RectF> shadow_casters;
    std::size_t tiles_shadow_begin = 0, tiles_shadow_end = 0;
    // Canli kutucuk satir(lar)i (pencere koordinati, DIP; yalniz tamami gorunur govdedeyken dolu) ve son tam cizimde
    // kutucuklarin yerlestigi govde / dikey konum: 1 Hz ornek ve deger gecisi yalniz bu bolgeyi yeniden cizer
    // (Screen::paint_tiles_region).
    RectF tiles_region;
    RectF tiles_body;
    float tiles_top = 0;
    float tiles_h = 0;          // kutucuk satir(lar)inin kullandigi yukseklik (satir araliklariyla)
    std::size_t tiles_seq = 0;  // kutucuklarin ilk Status indeksi
    // ---- Uygulama ici kip penceresi (spec 2026-11-12 E) ----
    // modal: acik pencerenin durumu (sahibi MainWindow::run_modal cercevesi ya da cizim denetimi; nullptr = yok). Acikken
    // sayfa ogeleri isabet haritasina girmez (yalniz kip ogeleri); odak sirasi kartin icinde doner. modal_fade: acilis
    // gecisi (motion.dialog_open; azaltilmis harekette aninda).
    const ModalState* modal = nullptr;
    Tween modal_fade{1};
    // ---- Sistem araclari (spec 2026-11-12 B4; sekmesiz tek sayfa) ----
    // Temizlik halkasi: secili kategorilerin toplam boyutunun taranan toplama orani; secim degisince motion.cleanup_ring
    // ile doner. cleanup_ring_to: son hedef (-1 = henuz cizilmedi; ilk cizimde aninda oturur).
    Tween cleanup_ring{0};
    float cleanup_ring_to = -1.f;
    // Sayfaya Baslangic / Sistem kimligiyle gelindi (eski alt sekme baglantilari): sonraki cizimde o bolum basligi
    // gorunur alana kaydirilir (0 = yok, 2 = Baslangic, 4 = Sistem). systools_sections: bolum basliklarinin govde
    // koordinatindaki ust kenari (Temizlik, Baslangic, Hizmetler, Sistem; cizimde yazilir).
    int systools_reveal = 0;
    std::array<float, 4> systools_sections{};
    // Spec 2026-11-14 B12: bolum kartlarinin son cizimdeki yuksekligi (kart cercevesi icerikten once cizilir). Degisince
    // systools_relayout kalkar ve kabuk ayni karede bir kez daha cizilir (dogru yukseklikle).
    std::array<float, 4> systools_card_h{};
    bool systools_relayout = false;
    // ---- Gecmis (spec 2026-11-12 B6) ----
    // Son acilan / kapanan journal satiri (history_row_key) ve acilma gecisi (motion.row_expand; 0 -> 1 acik).
    std::string history_anim_key;
    Tween history_anim{1};
    std::vector<std::size_t> history_go_rows;  // "Panel'e git" dugmesi cizilen satirlar (view.rows indeksi; gorunen sira)
    // Karsilastirma seridinin cubuklari (motion.compare_bars): secim cifti degisince 0'dan dolar. key: cizilen cift.
    Tween history_bars{1};
    std::string history_bars_key;
};

// Kip penceresi ogesi <-> isabet kimligi. Kip ogesi olmayan kimlik -> {ModalItem::None}.
Hit modal_hit(const ModalFocus& f);
ModalFocus modal_focus_of(Hit h);

// Zorunlu guncelleme: tam ekran kart gosterilir mi (yeni surum zorunlu veya klasor yazilamiyor).
inline bool update_gate_shown(const ViewModel& vm) {
    using core::update::UpdateOutcome;
    return vm.update.outcome == UpdateOutcome::Mandatory || vm.update.outcome == UpdateOutcome::NotWritable;
}
// Kart govdesi: NotWritable kurulu surumde (K27 standart hesap) "indirip calistir; kurulum yonetici izni ister";
// tasinabilir surumde "eskisinin yerine koy"; aksi "update.gate_body".
inline const char* update_gate_body_key(const ViewModel& vm) {
    if (vm.update.outcome != core::update::UpdateOutcome::NotWritable) return "update.gate_body";
    return vm.update.installed ? "update.not_writable_installed" : "update.not_writable";
}
// Kartta yalniz bu kimlikler etkinlesir (gezinme sekmeleri ve sayfa ogeleri yok).
inline bool update_gate_hit(Hit h) {
    return h == Hit::UpdateNow || h == Hit::UpdateQuit || h == Hit::UpdateRestore || h == Hit::UpdateOpenPage || h == Hit::UpdateRepairService;
}
// "Onceki ayarlara don": Performans acik, etkin degisiklik kaydi var veya kurtarma/geri alma suruyor.
bool show_restore(const ViewModel& vm);

// Zehir yesili RGB serit hareketi (spec §3). Serit yalniz hepsi uygunsa kayar; aksi halde sabit gradyan (HC'de tek
// renk cizgi) cizilir. Serit kendi 20 kare/sn zamanlayicisini kullanir; Screen::animating() yalniz gecisleri bildirir.
struct StripInputs {
    bool visible = false, foreground = false, game_blocked = false, test_running = false, on_battery = false,
         reduced_motion = false, app_animations = false, strip_setting = false, high_contrast = false;
    bool modal = false;  // kip penceresi acik: icerik karartilmis ve duragandir (serit / dugme hareketi durur)
};
inline bool strip_should_animate(const StripInputs& in) {
    return in.visible && in.foreground && !in.game_blocked && !in.test_running && !in.on_battery && !in.reduced_motion &&
           in.app_animations && in.strip_setting && !in.high_contrast && !in.modal;
}
// Serit kaymasi [0,1): (now % period) / period. period <= 0 -> 0.
inline float strip_phase(std::int64_t now_ms, int period_ms) {
    if (period_ms <= 0) return 0.f;
    std::int64_t m = now_ms % period_ms;
    if (m < 0) m += period_ms;
    return static_cast<float>(m) / static_cast<float>(period_ms);
}

// Panel hareketi (spec 2026-11-12 B.1 / F). Yuvarlak Performans dugmesi yalniz hepsi uygunsa hareket eder (acikken
// dis halka nefes alir, kapaliyken dugme kalp atisi yapar): dugme etkin (Performans Acik ya da Kapali) ve gorunur
// govdede (ring_shown), pencere gorunur ve on planda, oyun / test / olcum yok, pilde degil, hareket azaltilmamis,
// uygulama animasyonlari acik, yuksek kontrast degil. RGB serit ayari (strip_setting) bu kurala girmez: hareket
// "Animasyonlar" anahtarina baglidir.
inline bool breathe_should_animate(const StripInputs& in, bool ring_shown) {
    return ring_shown && in.visible && in.foreground && !in.game_blocked && !in.test_running && !in.on_battery && !in.reduced_motion &&
           in.app_animations && !in.high_contrast && !in.modal;
}
// Deger gecisleri (LED, cubuk, sayac) duraklar mi: pencere gorunmuyor, oyun acik, test / olcum suruyor ya da pilde.
// (Azaltilmis hareket ayri kuraldir: effective_ms.)
// Oyunlar (spec 2026-11-12 B2): calisan oyunun "Calisiyor" noktasi nabiz atar. Nokta yalniz bir oyun acikken vardir;
// bu yuzden "oyun acik" kapisi burada uygulanmaz — onun yerine pencere ON PLANDA olmalidir (oyun on plandayken
// zamanlayici calismaz). Diger kapilar aynidir: gorunur pencere, test / olcum yok, pilde degil, hareket azaltilmamis,
// uygulama animasyonlari acik, yuksek kontrast degil, kip penceresi yok. dots: ekranda en az bir nokta var.
inline bool pulse_should_animate(const StripInputs& in, bool dots) {
    return dots && in.visible && in.foreground && !in.test_running && !in.on_battery && !in.reduced_motion && in.app_animations &&
           !in.high_contrast && !in.modal;
}
inline bool motion_paused_for(const StripInputs& in) { return !in.visible || in.game_blocked || in.test_running || in.on_battery; }
// Nefes dalgasi [0,1]: 0 -> 1 -> 0, dongu basina bir kez (kosinus; baslangic ve bitis yumusak). period <= 0 -> 0.
inline float breathe_wave(std::int64_t now_ms, int period_ms) {
    return 0.5f - 0.5f * std::cos(6.2831853f * strip_phase(now_ms, period_ms));
}
// Noktanin opakligi [1 - kPulseDim, 1]: dongu basinda tam opak, ortasinda en sonuk (kosinus). live=false: 1 (sabit).
inline float pulse_alpha(bool live, std::int64_t now_ms, int period_ms) {
    return live ? 1.f - alpha_scale::kPulseDim * breathe_wave(now_ms, period_ms) : 1.f;
}
// Kalp atisi (spec 2026-11-12 B.1): Performans kapaliyken dugme dongu basina (motion.heartbeat_period = 1,5 sn) cift
// vurus yapar — olcek 1 -> 1,10 -> 1 -> 1,07 -> 1 — ve ilk vurusla birlikte disa acilip sonen bir halka cikar; dongunun
// kalani dinlenmedir (olcek 1, halka yok). scale: dugmenin cizim olcegi. ring: halkanin ilerlemesi [0,1] (0 = dugmenin
// kenarinda, tam opak; 1 = sonmus, cizilmez). period <= 0 -> dinlenme.
struct HeartbeatFrame {
    float scale = 1.f;
    float ring = 1.f;
};
inline constexpr float kHeartbeatPeak1 = 0.10f, kHeartbeatPeak2 = 0.07f;  // vurus tepeleri (olcek artisi)
inline constexpr float kHeartbeatBeat = 0.16f;    // bir vurusun suresi (dongu orani; 1,5 sn'de 240 ms)
inline constexpr float kHeartbeatSecond = 0.20f;  // ikinci vurusun basladigi an (dongu orani)
inline constexpr float kHeartbeatRing = 0.60f;    // halkanin disa acilip sondugu sure (dongu orani)
inline HeartbeatFrame heartbeat_frame(std::int64_t now_ms, int period_ms) {
    HeartbeatFrame f;
    if (period_ms <= 0) return f;
    const float p = strip_phase(now_ms, period_ms);
    constexpr float kPi = 3.14159265f;
    if (p < kHeartbeatBeat) f.scale = 1.f + kHeartbeatPeak1 * std::sin(kPi * p / kHeartbeatBeat);
    else if (p >= kHeartbeatSecond && p < kHeartbeatSecond + kHeartbeatBeat)
        f.scale = 1.f + kHeartbeatPeak2 * std::sin(kPi * (p - kHeartbeatSecond) / kHeartbeatBeat);
    if (p < kHeartbeatRing) f.ring = p / kHeartbeatRing;
    return f;
}
// Dinlenme: bu anda cizilecek kare duragan kareyle aynidir (zamanlayici dugme bolgesini yeniden cizdirmez).
inline bool heartbeat_resting(std::int64_t now_ms, int period_ms) {
    constexpr float kEnd = kHeartbeatRing > kHeartbeatSecond + kHeartbeatBeat ? kHeartbeatRing : kHeartbeatSecond + kHeartbeatBeat;
    return period_ms <= 0 || strip_phase(now_ms, period_ms) >= kEnd;
}

// Guncellemeler (spec 2026-11-12 H; ADR-032): uretici satirlari (gorunen sira: ekran karti(lari), yonga seti, BIOS).
// Disari yonlendirme YOK: link her zaman bos (UpdatesLink kimligi korunur, cizilmez). Ekran karti / yonga seti satiri
// Windows Update onerisiyle eslesirse wu = scan.updates indeksi ("Indir ve kur"; o oneri ayri satir olmaz). Ekran karti
// satirinda resmi paket yolu (local = gpus indeksi, "Dosyadan kur") NVIDIA / AMD / Intel icin vardir. BIOS yalniz bilgidir.
// Sayfa cizimi ve MainWindow ayni listeyi kullanir. Envanter okunurken bos. updates_page.cpp.
enum class UpdatesVendorKind { Gpu, Chipset, Bios };
struct UpdatesVendorRow {
    UpdatesVendorKind kind = UpdatesVendorKind::Gpu;
    std::string name, version;          // UTF-8; bos = okunamadi ("—")
    int date_y = 0, date_m = 0, date_d = 0;  // BIOS tarihi; 0 = yok
    core::drivers::VendorPage page;
    std::optional<std::size_t> link;   // ADR-032: her zaman bos (disari yonlendirme yok)
    std::optional<std::size_t> wu;     // eslesen Windows Update onerisi (scan.updates indeksi)
    std::optional<std::size_t> local;  // "Dosyadan kur" (gpus indeksi)
};
std::vector<UpdatesVendorRow> updates_vendor_rows(const ViewModel& vm);
// Bayt boyutu (KB / MB / GB; yerel ondalik isareti). cleanup_page.cpp.
std::wstring format_bytes(std::uint64_t b, bool tr);
// Ust menu secimi: "Sistem araclari" (Cleanup yuvasi) Sistem, Temizlik ve Baslangic sayfalarinda secilidir.
inline bool systools_page(Page p) { return p == Page::Cleanup || p == Page::Startup || p == Page::System; }
// Spec 2026-11-12 B4: Sistem araclari sekmesiz tek sayfadir; bolum sirasi Temizlik -> Baslangic uygulamalari -> Oyun
// sirasinda durdurulacak hizmetler -> Sistem. Eski alt sekme kimlikleri (asagida) korunur ama cizilmez ve Tab sirasinda
// yoktur. Temizlik halkasi 84 DIP. Kategori kutulari / baslangic satirlari iki sutun, hizmet kutulari uc sutun; sutun
// bu genisligin altina dusuyorsa sutun sayisi azalir.
inline constexpr float kCleanupRingD = 84.f;
inline constexpr float kSecHeadMinH = 22.f, kSecHeadGap = 14.f;  // .sec-rule basligi: asgari satir + alt bosluk (gap-3.5)
inline constexpr float kSysToolsTileMinW = 300.f;
inline constexpr float kSysToolsServiceMinW = 220.f;
// Spec 2026-11-14 B12: kapali Temizlik listesinde gorunen kategori sayisi. Liste en cok kCleanupCollapsedRows satir
// (cols sutun) gosterir; secili ya da dosya listesi acik son kategori (last_needed; yoksa n) her zaman gorunur kalir.
// Gizlenecek kategori yoksa (ya da liste aciksa) n. Sonuc sutun sayisinin katina yuvarlanir.
inline constexpr std::size_t kCleanupCollapsedRows = 3;
inline std::size_t cleanup_visible_rows(std::size_t n, std::size_t cols, std::size_t last_needed, bool open) {
    if (open || cols == 0) return n;
    std::size_t shown = kCleanupCollapsedRows * cols;
    if (last_needed < n && last_needed + 1 > shown) shown = (last_needed + cols) / cols * cols;
    return shown >= n ? n : shown;
}
inline int systools_columns(float inner_w, float gap, int max_cols, float min_w) {
    for (int c = max_cols; c > 1; --c)
        if ((inner_w - gap * static_cast<float>(c - 1)) / static_cast<float>(c) >= min_w) return c;
    return 1;
}
// Temizlik halkasi orani: secili boyut / taranan (hazir) kategorilerin toplam boyutu; toplam 0 ise 0.
inline float cleanup_ring_ratio(std::uint64_t selected, std::uint64_t total) {
    return total == 0 ? 0.f : static_cast<float>(static_cast<double>(selected > total ? total : selected) / static_cast<double>(total));
}
// Spec 2026-11-12 B6: zaman cizgisi noktasinin rengi (durum renginde): Etkin yesil, Kismen geri alindi sari,
// Kurtarma gerekli kirmizi; digerleri notr.
enum class HistoryDot { Good, Partial, Recovery, Neutral };
HistoryDot history_dot(core::recovery::HistoryStatus st);
// Gecmis satirindaki "Geri almak icin ... git" dugmesinin gidecegi sayfa (kayit kapsamina gore): Baslangic ->
// Sistem araclari, Hazirlik -> Oyun hazirligi, diger (Performans) -> Panel. Dugme yalniz Etkin / Kurtarma gerekli
// kayitta cizilir (geri alinacak bir sey var).
Page history_undo_page(std::string_view scope);
bool history_undo_shown(core::recovery::HistoryStatus st);
inline bool nav_tab_selected(Page tab, Page current) {
    return tab == current || (tab == Page::Cleanup && systools_page(current));
}
// Sistem araclari alt sekmeleri (gorunen sira): nav_hit(System) -> Sistem, SysTabCleanup -> Temizlik,
// nav_hit(Startup) -> Baslangic. Ust menudeki "Sistem araclari" sekmesi (nav_hit(Cleanup)) Temizlik'i acar.
inline constexpr std::size_t kSysToolsTabCount = 3;
inline std::array<Hit, kSysToolsTabCount> systools_tab_hits() {
    return {nav_hit(static_cast<std::size_t>(Page::System)), Hit::SysTabCleanup, nav_hit(static_cast<std::size_t>(Page::Startup))};
}
inline bool systools_subtab_selected(Hit tab, Page current) {
    if (tab == Hit::SysTabCleanup) return current == Page::Cleanup;
    if (tab == nav_hit(static_cast<std::size_t>(Page::Startup))) return current == Page::Startup;
    if (tab == nav_hit(static_cast<std::size_t>(Page::System))) return current == Page::System;
    return false;
}

// Gorunum modeli degisince animasyon hedeflerini gunceller (anahtar, ilerleme, kart gecisi).
void sync_perf_animations(UiState& s, const PerfView& before, const PerfView& after, const Theme& t, std::int64_t now_ms);

// Odak sirasi (Tab): kenar cubugu ogeleri, sonra sayfa ogeleri. Asama/durum bilgisinden uretilen niyet sirasi.
std::vector<Hit> focus_order(const UiState& s);
// Gercek Tab sirasi: niyet sirasinin son cizimde kayitli ve etkinlestirilebilir ogeleri (devre disi veya
// hic cizilmemis/ogesi olmayan duraklar atlanir; TH-01, DS-05). Gorunur alan disindaki ogeler de kayitlidir.
std::vector<Hit> focus_order(const UiState& s, const HitMap& hits);
// Guncellemeler sayfasi odak duzeltmesi (yeni durum v gelince): "Durdur" <-> "Denetle" ayni yerde yer degistirir; odak bir
// "Kur" dugmesindeyken satiri kalktiysa (kurulumdan sonra liste yeniden araniyor / liste degisti) ust karttaki dugmeye
// gecer (spec H2; odak kaybolmaz). Diger odaklar degismez.
Hit updates_focus_after(Hit focus, const ViewModel::UpdatesView& v);
// RDY-09: Tab/Shift+Tab adimi. Odak gercek sirada degilse (ornek: is surerken devre disi kalan dugme) niyet
// sirasindaki konumundan devam edilir; odak basa atlamaz. real bos -> Hit::None.
Hit focus_step(const std::vector<Hit>& intent, const std::vector<Hit>& real, Hit current, int delta);
// TH-04: UIA Selection deseni icin secili ogeler (TabItem/RadioButton, agac sirasi).
std::vector<Hit> selected_hits(const HitMap& hits);

// --- Oyun Hazirligi satir yerlesimi ve dugme kurallari (UI denetimi Track C) ---
// RDY-03 + spec 2026-11-12 B3: bulgu satiri tek satirdir; dugmeler (ozel durumlarda 2-3 dugme) satirin saginda durur.
// Dugmelerden sonra metne kalan genislik bu degerden darsa (dar pencere) dugmeler metnin altina iner.
inline constexpr float kReadinessMinTextW = 280.f;
inline bool readiness_buttons_below(float inline_text_w, int buttons) {
    return buttons > 0 && inline_text_w < kReadinessMinTextW;
}
// RDY-04: dugme vurgu duzeyi. Recommended = onerilen guvenli eylem (otomatik Duzelt, Geri al / Yeniden dene;
// accent tonlu); Neutral = ayar/sayfa ac, ag testi; Caution = geri donussuz "Mevcut degeri koru" (sakin, uyari isareti,
// onerilen eylemden daha genis aralikla ayrilir).
enum class RowButtonStyle { Recommended, Neutral, Caution };
enum class RowButton { Action, Undo, Keep };
RowButtonStyle readiness_button_style(RowButton b, core::readiness::ActionKind action);
// RDY-07: geri alma dugmesi etiketi. Yalniz Retry (onceki geri alma dogrulanamadi) "Geri al / Yeniden dene";
// Undo / Interrupted / Conflict "Geri al".
core::i18n::StringId readiness_undo_label(core::readiness::UndoState u);
// Satir eylem dugmesi etiketi (Duzelt / Ayarlari ac / Sayfayi ac / Agi test et; EF-X01 olay penceresi: hedef gun).
core::i18n::StringId readiness_action_label(const core::readiness::Check& c);
// RDY-12: satir durum metni (dugmenin UIA HelpText'i; tam cumle); None/Undo icin yok.
std::optional<core::i18n::StringId> readiness_undo_state_text(core::readiness::UndoState u);
// Spec 2026-11-12 B3: ozel durum satirinda gorunen tek kisa uyari satiri (tam cumle HelpText'te kalir); None/Undo icin yok.
std::optional<core::i18n::StringId> readiness_undo_warning(core::readiness::UndoState u);
// RDY-08: son islem bildiriminin gosterilecegi satir (bulgu kimligi eslesirse); yoksa nullopt (yalniz ozet karti).
std::optional<std::size_t> readiness_notice_row(const ViewModel::ReadinessView& rv, std::size_t rows);
// P12-B1: bildirim cumlesindeki {n} / {m} (toplu duzeltme sonucu) doldurulur.
std::wstring readiness_notice_fill(std::wstring text, const ViewModel::ReadinessView& rv);
// Zehir yesili (spec §5): hazirlik skoru. iyi = Good; oneri = Advice + Problem; olculmedi = Unknown; Info bilgi
// satiridir, sayilmaz. total = iyi + oneri + olculmedi ("7/10" = good/total). Yalniz gercek tarama bulgularindan.
// problem: oneri sayisinin "Sorun" olan kismi (Oyun hazirligi sayim satiri sorun ve oneriyi ayri yazar).
struct ReadinessScore {
    int good = 0, advice = 0, unmeasured = 0, total = 0, problem = 0;
};
ReadinessScore readiness_score(const ViewModel::ReadinessView& rv);
// Spec 2026-11-12 §1(c): Panel hazirlik ozetinin bos kalan alanina yazilacak bulgular — gorunen sirada ilk `max`
// Sorun/Oneri bulgusunun rv.checks indeksi. Olculmeyen, iyi ve bilgi satirlari girmez; bulgu yoksa bos (uydurma yok).
std::vector<std::size_t> readiness_top_advice(const ViewModel::ReadinessView& rv, std::size_t max);
// Gruplu satir sirasi: once "ONERILER" (Problem, Advice, sonra Unknown; kendi icinde tarama sirasi), sonra
// "IYI DURUMDA" (Good, Info). Ilk `rows` bulgu; donen dizi rv.checks indeksleridir (isabet kimligi indeksle kalir).
bool readiness_advice_group(core::readiness::Level l);
std::vector<std::size_t> readiness_display_order(const ViewModel::ReadinessView& rv, std::size_t rows);
// Spec 2026-11-12 B3: ana (dolu) dugme yalniz "Sorun" satirinin eylem dugmesidir; oneride cerceveli, olculmeyen ve
// bilgi satirlarinda mevcut eylem (ikincil).
inline bool readiness_action_primary(core::readiness::Level l) { return l == core::readiness::Level::Problem; }
// Hazirlik halkasi (BOOST maketi: w-[84px], SVG r 38 / cizgi 7 viewBox 88); yay rengi iyi/toplam oranina gore: < %40
// turuncu (Low), < %80 sari (Mid), aksi yesil (High). Sonuc yoksa None (yalniz iz halkasi; sayi uretilmez).
inline constexpr float kReadinessRingD = 84.f;
enum class ReadinessBand { None, Low, Mid, High };
ReadinessBand readiness_ring_band(int good, int total);
// Spec 2026-11-12 B3: bulgu satiri tek satirdir — kisa baslik + soluk kisa ek. Bulgu cumlesi ilk cumle / yan cumle
// sinirinda (". " / "; " / ", "; parantez ici sayilmaz) ikiye ayrilir; sondaki nokta atilir. Sinir yoksa ek bostur.
// Cumlenin tamami UIA adinda kalir.
struct ReadinessSplit {
    std::wstring title, extra;
};
ReadinessSplit readiness_split(std::wstring_view text);
// Bulgu satirinin soluk kisa gerekcesi (gate P3b): basliktaki ozne ("DELL S2721DGF: ") atilir, cumle ilk parca ve
// kalan olarak " · " ile birlesir ("60 Hz'de calisiyor · 165 Hz destekleniyor"). Ozne bos ya da cumle onunla
// baslamiyorsa cumle aynen bolunur.
std::wstring readiness_reason(std::wstring_view text, std::wstring_view subject);
// Spec 2026-11-14 B11: alan kutucuklari. Dugmeli / onerili / bildirimli her bulgu (loud[k]) kendi kutucugunun ana
// bulgusudur (gorunen sirada); dugmesiz iyi / bilgi bulgulari ayni alanin ilk kutucuguna katlanir (Ayrinti'da okunur),
// alanin kutucugu yoksa o alan icin yeni kutucuk acilir. Hicbir bulgu dusmez.
struct ReadinessTile {
    std::size_t k = 0;               // ana bulgu (rv.checks indeksi)
    std::vector<std::size_t> extra;  // katlanan bulgular (gorunen sira)
};
std::vector<ReadinessTile> readiness_tiles(const ViewModel::ReadinessView& rv, const std::vector<std::size_t>& order,
                                           const std::vector<bool>& loud);
// Spec 2026-11-14 B11: "Sistem kontrolu" bulgusunun grubu (bulgu anahtarinin kaynagina gore: guc / guvenlik notlari,
// donanim sagligi, disk alani, guncelleme denetimi; digerleri Other).
FindingGroup finding_group(std::string_view text_key);
// Oyunlar kart izgarasi (spec 2026-11-11 §5): govde >= 1100 DIP 4 sutun, >= 820 3, >= 540 2, aksi 1.
inline constexpr float kGamesGrid4MinW = 1100.f;
inline constexpr float kGamesGrid3MinW = 820.f;
inline constexpr float kGamesGrid2MinW = 540.f;
inline int games_grid_columns(float body_w) {
    return body_w >= kGamesGrid4MinW ? 4 : body_w >= kGamesGrid3MinW ? 3 : body_w >= kGamesGrid2MinW ? 2 : 1;
}
// RDY-09/08: odak bir bulgu dugmesindeyken o dugme artik yoksa (is bitti, satir degisti) ayni satirin kalan dugmesine,
// yoksa "Yeniden tara"ya gecer. Diger durumlarda odak degismez.
Hit readiness_focus_fallback(Hit focus, const HitMap& hits);

// RDY-10: govde kaydirmasi; oge (pencere koordinati, mevcut kaydirmayla) [view_top+margin, view_bottom-margin]
// araligina sigacak en kucuk degisiklik. Oge alandan buyukse ust kenari hizalanir. Sinirlama cagirana aittir.
float reveal_scroll(float scroll, const RectF& item, float view_top, float view_bottom, float margin);

struct MeasureCtx;  // P9/B5: Olcumler sayfasi baglami (measure_page.h)

class Screen {
public:
    Screen(const Theme& theme, const core::i18n::Catalog& catalog) : theme_(theme), cat_(catalog), pal_(&theme.dark) {}
    // UI-04/07: etkin palet (koyu, acik veya yuksek kontrast sistem renkleri).
    void set_palette(const Palette* p) { pal_ = p ? p : &theme_.dark; }
    // Marka logosu varyanti: koyu palet -> lockup-dark, acik -> lockup-light, diger (yuksek kontrast) -> tek renk.
    const Palette& pal() const { return *pal_; }

    // Tum pencereyi cizer; isabet haritasini dondurur.
    std::wstring check_text(const core::readiness::Check& c) const;  // CHK: bulgu metni (katalog + parametre)
    std::wstring check_name(const core::readiness::Check& c) const;  // CHK: bulgu adi (alani: Ekran, Grafik ...)
    std::wstring component_status_text(const core::measure::ComponentCard& c) const;  // BENCH v2
    // M01: neden kodu -> yerellestirilmis metin (tam anahtar / onek ailesi / genel metin; ham kod asla).
    std::wstring bench_reason_text(std::string_view code) const;
    // M02: sonuc basligi (Uygulama Testi: alt test durumlarindan; Oyunda Test: teshis).
    std::wstring result_headline(const core::measure::ResultView& r) const;
    // M03: sonuc/gecmis basligi; Uygulama Testi'nde yerellestirilmis kademe, ic kimlik ("scene_v3 medium") asla.
    std::wstring result_title(const core::measure::ResultView& r) const;
    // M11: hata metni (neden + "hicbir ayar degismedi") ve sonraki adim.
    std::wstring measure_failure_text(const core::measure::MeasureState& m) const;
    // M04: "Asama 2/5 · Islemci" (asama bilinmiyorsa yalniz asama adi).
    std::wstring measure_stage_text(const core::measure::MeasureState& m) const;
    // DIAG-08: takilma raporu metinleri (yerellestirilmis alanlar + kanit kodlari; ham kod yok).
    struct DropTexts {
        std::wstring headline;
        std::vector<std::wstring> fields;
        std::vector<std::wstring> evidence;
    };
    DropTexts drop_report_texts(const core::diagnosis::DropReport& r) const;
    // Spec 2026-11-12 B (Oyunda Test): "bu oyunda ne sinirladi" karti metinleri. Hukum "Bu oyunda, bu ayarlarda"
    // kapsamiyla; olculen yuzde / FPS yerel bicimle yazilir, olculmeyen deger icin satir uretilmez.
    struct GameLimitUiTexts {
        std::wstring headline;
        std::vector<std::wstring> lines;  // neden + olculen ek satirlar
        std::vector<std::wstring> notes;  // kare siniri / HAGS / kapsam notu
    };
    GameLimitUiTexts game_limit_ui_texts(const core::measure::GameLimitView& v) const;
    // M06: ekran okuyucu duyurusu metni.
    std::wstring measure_announcement_text(const core::measure::Announcement& a, const core::measure::MeasureState& m) const;
    // Task 12: hiz testi durum satiri ve bitis/iptal/hata duyurusu (ekran okuyucu).
    std::wstring speed_state_text(const ViewModel::SpeedView& v) const;
    std::wstring speed_announcement_text(const ViewModel::SpeedView& v) const;
    // Spec 2026-11-12 H: Guncellemeler ust kartinin metni — buyuk durum ("2 guncelleme bulundu" / "Guncel" / "Denetlenmedi"
    // / "Denetlenemedi") ve tek alt satir (kaynak + son denetim, ya da neden / oyun kapisi; warn = uyari rengi).
    struct UpdatesStatus {
        std::wstring big, sub;
        bool warn = false;
    };
    UpdatesStatus updates_status(const ViewModel& vm) const;
    std::wstring updates_status_text(const ViewModel& vm) const;  // big + sub (ekran okuyucu duyurusu)
    // Spec H2: kurulacak oge satiri (kurulum onayi govdesi); k gecersizse bos.
    std::wstring updates_install_what(const ViewModel& vm, std::size_t k) const;
    // Spec H2: son kurulum sonucu metni (sonuc + yeniden baslatma / geri yukleme noktasi notlari); sonuc yoksa bos.
    std::wstring updates_install_text(const ViewModel& vm) const;
    // Kip penceresi (s.modal) aciksa sayfa cizildikten sonra karartma + kart cizilir ve isabet haritasinda yalniz kip
    // ogeleri kalir (spec 2026-11-12 E).
    HitMap paint(Graphics& g, float w, float h, UiState& s, const ViewModel& vm, std::int64_t now_ms);
    // Kip penceresi acilirken: acilis gecisini baslatir (motion.dialog_open; azaltilmis harekette ve hareketin durdugu
    // kosullarda — s.motion_paused: oyun acik, test / olcum, pil — aninda).
    void open_modal_motion(UiState& s, std::int64_t now_ms) const;
    // Canli test ekrani (spec 2026-11-12 A4): yeni ornegi durum nesnesine yazar ve yuk cubugu gecislerini hedefler.
    void set_live_sample(UiState& s, const core::measure::LiveSample& v, std::int64_t now_ms) const;
    // Hizli yol: yalniz canli gorunum bolgesi (s.live_region; son tam cizimde yazildi) yeniden cizilir. Ust menu ve
    // isabet kutulari degismez; hits'e canli ogelerin guncel UIA adlari yazilir (cagiran mevcut haritaya aktarir).
    void paint_live_region(Graphics& g, UiState& s, const ViewModel& vm, std::int64_t now_ms, HitMap& hits);
    // include_live=false: canli kutucuk deger gecisleri haric (onlar yalniz kutucuk bolgesini yeniden cizer).
    bool animating(const UiState& s, std::int64_t now_ms, bool include_live = true) const;
    bool live_animating(const UiState& s, std::int64_t now_ms) const;
    // Panel hizli yollari (spec 2026-11-12 F: yalniz hareket eden bolge gecersiz kilinir). Bolge son tam cizimde
    // yazildi; ust menu, diger kartlar ve isabet kutulari degismez. Bolge bossa hicbir sey cizilmez.
    // Kutucuklar: hits'e kutucuklarin guncel UIA adlari yazilir (cagiran mevcut haritaya aktarir).
    void paint_tiles_region(Graphics& g, UiState& s, const ViewModel& vm, std::int64_t now_ms, HitMap& hits);
    // Yeni ornekle kutucuk satir(lar)inin boyu son tam cizimdekiyle ayni mi (yalniz olcum; cizim yok). false: satir boy
    // degistirdi (ornek: olculemeyen degerin neden metni iki satira sardi) -> alttaki kartlar da kayar, tam kare gerekir.
    bool tiles_layout_unchanged(Graphics& g, UiState& s, const ViewModel& vm);
    // Yuvarlak Performans dugmesi + nefes halkasi / kalp atisi.
    void paint_power_region(Graphics& g, UiState& s, const ViewModel& vm, std::int64_t now_ms);
    // Oyunlar hizli yolu (spec 2026-11-12 B2): yalniz "Calisiyor" noktalari (s.pulse_regions; son tam cizimde yazildi)
    // yeniden cizilir — cip zemini + guncel opaklikta nokta. Isabet haritasi ve sayfanin geri kalani degismez.
    void paint_pulse_region(Graphics& g, const UiState& s, std::int64_t now_ms) const;
    // Yalniz RGB serit (y=0, tam genislik): serit zamanlayicisinin hizli yolu; isabet haritasi degismez.
    void paint_strip(Graphics& g, float w, const UiState& s, std::int64_t now_ms) const;

    void set_hover(UiState& s, Hit h, std::int64_t now_ms) const;
    // DS-02: fare basma durumu (basma gecisi motion.press; azaltilmis harekette aninda).
    void set_pressed(UiState& s, Hit h, std::int64_t now_ms) const;
    float hover_value(const UiState& s, Hit h, std::int64_t now_ms) const;
    float press_value(const UiState& s, Hit h, std::int64_t now_ms) const;
    void go(UiState& s, Page p, std::int64_t now_ms) const;
    // Zorunlu guncelleme seridi metni (bos = serit yok); tone: zemin/yazi tonu.
    std::wstring update_banner_text(const ViewModel& vm, ChipTone* tone = nullptr) const;

private:
    // Kabuk + sayfa (kip penceresi haric); paint() bunun ustune kip katmanini cizer.
    HitMap paint_shell(Graphics& g, float w, float h, UiState& s, const ViewModel& vm, std::int64_t now_ms);
    // Uygulama ici kip penceresi (spec 2026-11-12 E): karartma + ortali kart (baslik, govde, madde / secim listesi,
    // isaret kutusu, sag altta Hayir + Evet). hits'teki sayfa ogeleri silinir; yalniz kip ogeleri kaydedilir.
    void modal_layer(Graphics& g, float w, float h, UiState& s, std::int64_t now, HitMap& hits);
    // Ust menunun altindaki guncelleme seridi; kullanilan yuksekligi dondurur (yoksa 0). Zemin area genisligindedir
    // (pencere); metin icerik sutununa (column: x ve w; logo, sayfa basligi ve kartlarla ayni sol kenar) hizalanir.
    float update_banner(Graphics& g, const RectF& area, const RectF& column, const ViewModel& vm, HitMap& hits);
    // Tam ekran guncelleme karti (sekme yok); paint() yerine tum pencereyi cizer.
    void update_gate(Graphics& g, float w, float h, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    TopbarMetrics topbar_metrics(Graphics& g);
    void topbar(Graphics& g, const ShellLayout& l, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    // Baslik seridi (spec 2026-11-11 §2; tek satir, theme.header_h): solda sayfa basligi, ardindan sayfanin
    // page_header() ile doldurdugu yuva. Serit kaydirilmaz: ogeleri kabuk isabet haritasina (hits) yazilir.
    void header_strip(Graphics& g, const RectF& strip, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    // Sayfanin baslik seridi ogeleri. bar.take(w, h): sag yuvadan (sagdan sola) kutu; bar.take_leading(w, h): basligin
    // hemen sagindan kutu. Donen kutu bos (w == 0) ise oge sigmamistir: cizilmez (ya da sayfa govdesinde cizilir).
    // Onerilen yukseklikler: chip = Secondary satiri, dugme = theme.button_row_h. Yeni sayfa ogesi buraya eklenir;
    // odak sirasi (focus_order) ust sekmelerden hemen sonra ayni sirayla guncellenir.
    void page_header(Graphics& g, HeaderBar& bar, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    // Sistem araclari alt sekmeleri (Temizlik / Baslangic): baslik seridinde, basligin hemen saginda.
    void systools_tabs(Graphics& g, HeaderBar& bar, UiState& s, std::int64_t now, HitMap& hits);
    // Temizlik sekmesi: baslik seridinin saginda kirmizi "Temizlik geri alinamaz" cipi (salt okunur UIA Text).
    void cleanup_header(Graphics& g, HeaderBar& bar, HitMap& hits);
    // Oyunlar baslik seridi (spec 2026-11-12 B2): suzgec cipleri (Tumu n / Korumali n / Onerili n) basligin saginda;
    // sagda "Yeniden tara". ("Canli olcum kapali · Neden?" satiri kalkti; metinleri oyun kartinin UIA HelpText'inde.)
    void games_header(Graphics& g, HeaderBar& bar, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    // Olcumler baslik seridi: secili degerlerin ozeti (secenekler Test kartinin icindedir).
    void measure_header(Graphics& g, HeaderBar& bar, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    // Gecmis baslik seridi: kapsam suzgeci cipleri (Tumu n / Performans n / Hazirlik n / Baslangic n) ve sagda
    // "Disa aktar" (Hit::HistoryExport; veri yokken devre disi).
    void history_header(Graphics& g, HeaderBar& bar, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    // Ayarlar baslik seridi: sagda surum satiri (salt okunur UIA Text).
    void settings_header(Graphics& g, HeaderBar& bar, const ViewModel& vm, HitMap& hits);
    // Spec 2026-11-12 G: ilk acilista bir kez, ust menunun altinda engellemeyen tek satir "Windows acilisinda tepside
    // baslar" + "Kapat" (Hit::AutostartNoticeOff; Windows ile baslat anahtarini kapatir). Cizilen yuksekligi dondurur.
    float autostart_notice(Graphics& g, const RectF& area, const RectF& column, UiState& s, const ViewModel& vm, std::int64_t now,
                           HitMap& hits);
    // ADR-033 spec 8: Ui kurulum yeri disinda: ayni bicimde engellemeyen tek satir "Kurulum gerekli ..." + "Kur"
    // (Hit::InstallOfferInstall; kendini --setup ile baslatir, Ui cikar). Cizilen yuksekligi dondurur.
    float install_offer_notice(Graphics& g, const RectF& area, const RectF& column, UiState& s, const ViewModel& vm, std::int64_t now,
                               HitMap& hits);
    friend class SetupView;  // kurulum penceresi ayni dugme / anahtar / kart / kip penceresi cizimlerini kullanir
    // Ac/kapa dugmesi (ok isareti; satir ici 28x28): r dugme kutusu. UIA adi name; selected = acik. visible=false:
    // cizilmeden kaydedilir (sanal liste; RDY-10).
    void expander(Graphics& g, const RectF& r, Hit id, bool open, const std::wstring& name, UiState& s, std::int64_t now, HitMap& hits,
                  bool visible = true);
    // Baslik seridi suzgec cipi (hap bicimli radyo dugmesi): etiket + sayi. r: HeaderBar'dan alinan kutu.
    void filter_chip(Graphics& g, const RectF& r, Hit id, std::wstring_view label, std::wstring_view count, bool selected,
                     const std::wstring& name, UiState& s, std::int64_t now, HitMap& hits);
    // filter_chip genisligi (etiket + sayi + ic bosluk).
    float filter_chip_width(Graphics& g, std::wstring_view label, std::wstring_view count) const;
    float overview(Graphics& g, const RectF& body, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    // Sistem kontrolu bulgulari (liste). Panel'den Oyun hazirligi sayfasina tasindi; icerik ve metinler ayni.
    void findings_card(Graphics& g, CardSlot& slot, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq);
    // Spec 2026-11-12 B4: Sistem araclari tek sayfa (Cleanup / Startup / System kimliklerinin hepsi bunu cizer). Bolum
    // basliklari ince cizgili (section_rule). Bolumler area.y'den baslar; kullanilan yuksekligi dondurur.
    float system_tools_page(Graphics& g, const RectF& body, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    float section_rule(Graphics& g, float x, float y, float w, const std::wstring& title, HitMap& hits, StatusSeq& seq,
                       const std::wstring& help = {}, float right_reserved = 0);
    // Spec 2026-11-14 B12: Sistem sayfasi bolum kartinin baslik seridi. card: kart cercevesi (x, y, w). Accent tonlu ikon
    // rozeti, baslik, kisa ozet (Secondary; sigmazsa "…" / cizilmez — UIA metninde tam) ve sagda eylem yuvasi (right_w x
    // button_h). Yuva baslikla ayni satira sigmazsa basligin altina saga yasli iner. Seridin altinda kart genisliginde
    // ince cizgi (section_card_rule; govdesi bos kartta cizilmez). slot: eylem yuvasi (right_w > 0 ise). Dondurur: ince
    // cizginin y'si (govdesiz kartin alt kenari).
    float section_card_head(Graphics& g, const RectF& card, Icon icon, const std::wstring& title, const std::wstring& summary,
                            const Color& summary_c, float right_w, HitMap& hits, StatusSeq& seq, const std::wstring& help, RectF* slot);
    // Baslik seridinin altindaki kart genisligindeki ince cizgi; govdenin ust kenarini dondurur.
    float section_card_rule(Graphics& g, const RectF& card, float line_y);
    // BOOST maketi .sec-rule basligi (kartin DISINDA, kartin hemen ustunde: kSecHeadH + kSecHeadGap): "NN //" mono kod (vurgu;
    // violet = 04) + baslik + gradyan cizgi + sagda ozet (12px). slot_in_heading: eylem yuvasi baslik satirinin sagina
    // gelir (Baslangic "Uygula"); degilse yuva kartin ic ust satirinda (Temizlik dugmeleri; ozet solda). Govdesiz kartta
    // kart ici satir yoktur: dondurulen y kart ust kenaridir (section_card_rule cizgi cizmez).
    // .sec-rule baslik satirinin yuksekligi: 22 DIP ya da buyuk metinde Section satiri.
    // Bolum basligi (.sec-rule) ile karti arasi bosluk: Sistem gap-3.5 (14), Gecmis gap-3 (12); sayfa kendi degerini yazar.
    float sec_head_gap_ = kSecHeadGap;
    float sec_head_h() const { return std::max(kSecHeadMinH, theme_.role(Role::Section).line); }
    float section_head_v2(Graphics& g, const RectF& card, std::wstring_view code, bool violet, const std::wstring& title, const std::wstring& summary,
                          const Color& summary_c, float right_w, HitMap& hits, StatusSeq& seq, const std::wstring& help, RectF* slot,
                          bool slot_in_heading);
    // Sistem araclari "Bolumler:" atlama seridi (+ "Temizlik geri alinamaz" rozeti). Dondurur: serit yuksekligi.
    float systools_jump_strip(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq);
    float cleanup_section(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq);
    float startup_section(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq);
    float services_section(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq);
    float inventory_section(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, HitMap& hits, StatusSeq& seq);
    // Kart islevleri (spec 2026-11-12 §2): CardSlot ile iki gecisli calisir. Olcum gecisinde slot.place() false doner ve
    // kart cizim / isabet / StatusSeq / sayac yan etkisi olmadan geri doner; cizim gecisinde satir yuksekligindeki
    // cerceveyi alir (icerik ustte, eylem satiri altta).
    void game_banner(Graphics& g, Stack& st, const RectF& body, const ViewModel& vm, UiState& s, std::int64_t now, HitMap& hits);  // EXT-AC-02
    // Panel: "Darbogaz" (spec 2026-11-12 B.3 / D: uc kisa hukum satiri) + "Test et" (Hit::OpenMeasure). Hazirlik
    // kartiyla ayni satirda, ayni yukseklikte cizilir. Satirlar UIA Text olarak kaydedilir.
    // room: Panel'de bu satira kalan gorunur yukseklik; dogal boy sigmazsa satirlar sikisir (bolme cubugu cizilmez).
    void limit_card(Graphics& g, CardSlot& slot, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq, float room);
    // Sistem araclari "Sistem" alt sayfasi (Panel'den tasindi): Sistem karti (tam genislik, 3x2 ikonlu izgara: CPU, GPU,
    // RAM, Depolama, Ekran, Windows). Yalniz envanterin
    // okudugu alanlar; okunamayan "—". Hucreler UIA Text olarak kaydedilir. Kullanilan yuksekligi dondurur.
    float system_card(Graphics& g, const RectF& area, const ViewModel& vm, HitMap& hits, StatusSeq& seq);
    float games_page(Graphics& g, const RectF& body, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);  // EXT-AC-01/02, GUIDE-01
    float readiness_page(Graphics& g, const RectF& body, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);  // CHK
    float history_page(Graphics& g, const RectF& body, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);  // REC-01/02, R15/R16
    std::wstring history_value(std::string_view raw) const;
    void live_tiles(Graphics& g, Stack& st, const RectF& body, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits,
                    StatusSeq& seq, TilePass pass = TilePass::Full);
    void before_after_card(Graphics& g, CardSlot& slot, const ViewModel& vm);
    void components_card(Graphics& g, CardSlot& slot, const ViewModel& vm, HitMap& hits, StatusSeq& seq);  // DIAG-05
    // Spec 2026-11-12 C / D: Olcumler "Darbogaz" bolumu — baslik satiri ("bu test sahnesinde" + renk aciklamasi) ve yan
    // yana uc es kutu (1080p · 1440p · 4K): cozunurluk, D hukum kelimesi (renkli), olculen pay (tek satir), iki ince
    // cubuk (islemci FPS kehribar, ekran karti FPS camgobegi; sayilar sagda). Birincil ekrana denk gelen kutu camgobegi
    // kenarlik + "senin ekranin" alir. Kaldirilan notlar (kapsam, bagdastirici, PCIe, ipuclari) UIA metnindedir.
    void balance_card(Graphics& g, CardSlot& slot, const core::measure::BalanceView& v, UiState& s, const ViewModel& vm, std::int64_t now,
                      HitMap& hits, StatusSeq& seq);
    // Spec 2026-11-12 C: Test karti yuvarlak baslat dugmesi (theme.test_button_d). Bosta oynat simgesi + camgobegi halka
    // (breathe_live ise yavas nefes alir); test surerken durdur karesi + yesil ilerleme yayi (gercek oran; yoksa yay
    // yok). Isabet: TestStart (bosta) / TestStop (surerken; Durduruluyor'da devre disi). Hizli yol gecici haritayla cagirir.
    void test_button(Graphics& g, float cx, float cy, const core::measure::MeasureState& m, UiState& s, std::int64_t now, bool enabled,
                     HitMap& hits, const std::wstring& name, const std::wstring& help);
    void check_card(Graphics& g, CardSlot& slot, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);  // OPT-07
    void welcome_card(Graphics& g, Stack& st, const RectF& body, UiState& s, std::int64_t now, HitMap& hits);
    // Panel yuvarlak Performans dugmesi (spec 2026-11-12 B.1; Hit::Toggle, UIA perf.toggle, ToggleSwitch). Guc simgesi
    // cizim ilkelleriyle; acikken vurgu rengi halka + parlama + dis halka (breathe_live ise nefes alir), kapaliyken
    // hata rengi (kirmizi) halka (breathe_live ise kalp atisi: cift vurus + disa acilip sonen halka). Isabet kutusu
    // olcekle degismez. Hizli yol (paint_power_region) gecici bir isabet haritasiyla cagirir.
    void power_button(Graphics& g, float cx, float cy, const PerfView& pv, UiState& s, std::int64_t now, bool enabled, HitMap& hits,
                      const std::wstring& name, const std::wstring& help);
    // admin_without_uac: grup aciklamalarinin hizmet esi (core::service::admin_copy_key; "UAC sorulmaz").
    float groups_panel(Graphics& g, float x, float y, float w, const PerfView& pv, UiState& s, std::int64_t now, bool editable,
                       bool measure_only, HitMap* hits, bool admin_without_uac);
    // Panel Performans bolumu (spec 2026-11-12 B.1): kartsiz, ortali yuvarlak dugme; altinda durum + tek kisa satir
    // (+ "Ayrinti"); solunda SON TEST FPS / %1 DUSUK, saginda HAZIRLIK ve "Testi baslat" (dar pencerede yan gruplar
    // dugmenin altina iner). Ayrinti, onay (Evet/Hayir), ilerleme ve kurtarma icerigi bolumun altinda kart olarak
    // acilir (akis, metinler, kimlikler degismez). area.y'den baslar; kullanilan yuksekligi dondurur.
    float perf_card(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq);
    // Panel: "Oyun hazirligi" karti (iyi/toplam + cubuk, iyi / oneri sayilari, "Duzelt" = Hit::OpenReadiness). Satir
    // yuksekligi kartin dogal boyundan fazlaysa artan alana ilk iki onerinin adi yazilir (yalniz gercek bulgular).
    // room: Panel'de bu satira kalan gorunur yukseklik (pencere kaydirmasiz sigsin diye bulgu satiri sayisi buna gore).
    void readiness_summary(Graphics& g, CardSlot& slot, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, float room);
    // danger=true: geri donusuz islem (DS-04). Hata token'i ile dolu; etiket de ayri ("Evet, kalici sil").
    // in_row=true: tablo/liste satiri icindeki dugme (theme.button_row_h = 28; ic bosluk 12). Aksi theme.button_h = 32.
    // Dugme kurali (spec 2026-11-12 B7, butun uygulama): etiket kutunun yatay ve dikey tam ortasindadir (button_face);
    // ayni listedeki dugmeler es genislik ve es yuksekliktedir — liste, button_group_w ile buldugu ortak genisligi
    // width olarak verir (width <= dogal genislik ise dogal genislik kullanilir).
    float button(Graphics& g, float x, float y, Hit id, std::wstring_view label, bool primary, UiState& s, std::int64_t now,
                 HitMap& hits, bool enabled = true, bool danger = false, bool in_row = false,
                 float width = 0);  // enabled=false: soluk, UIA IsEnabled=false (RDY-09)
    // Ayni listede / satirda duran dugmelerin ortak genisligi: en genis etiketin dugme genisligi.
    float button_group_w(Graphics& g, std::initializer_list<std::wstring_view> labels, bool in_row = false) const;
    // Dugme yuzu (dolgu/cerceve/etiket/parlama/odak halkasi); isabet kaydi cagirana aittir (UIA adi farkli olabilir).
    // label bos ise etiket cizilmez; cizilecek etiket rengi doner. Etiket kutuda yatay ve dikey ortalanir.
    Role button_role(ButtonKind kind, bool enabled, float h) const;
    Color button_face(Graphics& g, const RectF& r, Hit id, std::wstring_view label, ButtonKind kind, bool enabled, UiState& s,
                      std::int64_t now, HitMap& hits);
    // Durum etiketi: at = sol ust; yukseklik Secondary satiri. Genisligi doner.
    float chip(Graphics& g, Point at, std::wstring_view text, ChipTone tone) const;
    // Sabit kutuya ortalanmis chip (hizali sutunlar icin).
    void chip(Graphics& g, const RectF& r, std::wstring_view text, ChipTone tone) const;
    float chip_width(Graphics& g, std::wstring_view text) const;
    // Noktali durum cipi ("Calisiyor"; spec 2026-11-12 B2): metnin solunda status_dot capinda nokta. dot_alpha:
    // noktanin opakligi (nabiz). Noktanin kutusu doner (hizli yol bolgesi; kutunun tamami cip zeminindedir).
    float dot_chip_width(Graphics& g, std::wstring_view text) const;
    RectF dot_chip(Graphics& g, const RectF& r, std::wstring_view text, ChipTone tone, float dot_alpha) const;
    // Kart: yuzey dolgu, 1 DIP cizgi, radius_card. danger: geri alinamaz/hata karti tonu. Cerceve simetri denetimi
    // icin kaydedilir (HitMap::cards).
    // radius < 0: theme.radius_card (Panel kartlari depth::kPanelCardRadius verir; golge kutusu her zaman token yaricapinda).
    void draw_card(Graphics& g, const RectF& r, bool danger = false, float radius = -1.f) const;
    // Kart cizimi kart kaydi (card_log_) olmadan: Panel kahraman karti (bolum simetri denetiminin disinda kalir).
    void draw_card_face(Graphics& g, const RectF& r, bool danger = false, float radius = -1.f) const;
    // Panel dugmesi (BOOST maketi): primary = .btn-primary (h 32, px 16, 13px/600, yaricap 7; play = oynat simgesi 14 + 6),
    // degilse .btn-secondary (h 28, px 12, 12px/500, yaricap 6). Yuksek kontrastta ortak dugme yuzu. Genislik doner.
    // compact: birincil dugmenin satir ici bicimi (maket !h-[28px] !px-3 !text-xs: h 28, px 12, 12px/600, yaricap 7).
    // min_w: etiket degisince (ornegin "Taraniyor") dugme genisligi oynamasin diye alt sinir.
    RectF panel_button_rect(Graphics& g, std::wstring_view label, bool primary, bool play = false, bool compact = false) const;
    float panel_button(Graphics& g, float x, float y, Hit id, std::wstring_view label, bool primary, UiState& s, std::int64_t now,
                       HitMap& hits, bool enabled = true, bool play = false, bool compact = false, float min_w = 0.f);
    // Satir ici rozet dugme (maket .quick-fix-btn: px 8, 11px/500, ton %15 zemin + %30 cizgi, yaricap 4; yukseklik
    // touch_min). Genislik doner.
    float badge_button_w(Graphics& g, std::wstring_view label) const;
    void badge_button(Graphics& g, const RectF& r, Hit id, std::wstring_view label, const Color& tone, bool enabled, UiState& s,
                      std::int64_t now, HitMap& hits);
    // Kahraman kart enerji hatlari (maket SVG'si kart ic kutusuna yayilir); dugmenin altinda kalir (hizli yol da cizer).
    void hero_conduits(Graphics& g, const RectF& card, bool on) const;
    // Gomulu telemetri yuvasi (koyu temada cukur zemin + yumusak cizgi; digerlerinde yalniz cizgi). log_card cagirmaz.
    void draw_well(Graphics& g, const RectF& r, bool highlight = false) const;
    // Pencere zemini (maket #main-window-shell: duz var(--bg); isiltilar <body> uzerinde, pencere disinda). area: doldurulacak bolge
    // (pencere koordinati); taban tum pencereyi doldurur, bolgeye kirpilir (bolgesel cizim tam kareyle ayni).
    void paint_backdrop(Graphics& g, const RectF& area) const;
    // Koyu tema kart golgesi (CSS 0 10px 26px rgba(0,0,0,.40)).
    void card_shadow(Graphics& g, const RectF& r) const;
    // Kayitli golgeleri [from, to) bolgeye kirpilmis yeniden cizer (bolgesel cizim).
    void replay_shadows(Graphics& g, const UiState& s, std::size_t from, std::size_t to) const;
    // Panel kahraman parlamasi (#hero-glow): dugme merkezinde bulanik hap; Kapali hata %10, Acik vurgu %18 (on 0..1).
    void hero_glow(Graphics& g, const RectF& card, float cx, float cy, float on) const;
    // draw_card kullanmadan cizilen (ozel kenarli) ya da gorunur alan disinda oldugu icin cizilmeyen kart cercevesi.
    void log_card(const RectF& r) const { card_log_.push_back(r); }
    // Parlama yalniz koyu temada ve hareket azaltilmamisken (spec §2; HC'de ve reduced motion'da yok).
    bool glow_allowed(const UiState& s) const { return pal_ == &theme_.dark && !s.reduced_motion; }
    float empty_page(Graphics& g, const RectF& body, core::i18n::StringId text);
    float measure_page(Graphics& g, const RectF& body, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    // P9/B5: Olcumler sayfasi kartlari (measure_page.cpp / measure_result_card.cpp / measure_cards.cpp /
    // measure_history_card.cpp). Sayfa genelindeki olculer MeasureCtx'te (measure_page.h).
    void measure_chip(const MeasureCtx& mc, const RectF& r, Hit id, const std::wstring& text, const std::wstring& acc_name, bool selected,
                      Color idle_fill, float radius = -1.f, bool off = false);
    float measure_card_head(const MeasureCtx& mc, float hx, float hy, float hw, std::wstring_view code, const Color& tag,
                            const std::wstring& title, float h);
    float measure_opt_w(const MeasureCtx& mc, const std::wstring& text);
    void measure_test_card(const MeasureCtx& mc, CardSlot& slot);
    void measure_result_card(const MeasureCtx& mc, CardSlot& slot);
    void measure_game_limit_card(const MeasureCtx& mc, CardSlot& slot);
    void measure_stutter_card(const MeasureCtx& mc, CardSlot& slot);
    void measure_endurance_card(const MeasureCtx& mc, CardSlot& slot);
    void measure_proof_card(const MeasureCtx& mc, CardSlot& slot);
    void measure_cancel_card(const MeasureCtx& mc, CardSlot& slot);
    void measure_save_failed_card(const MeasureCtx& mc, CardSlot& slot);
    void measure_component_card(const MeasureCtx& mc, const core::measure::ComponentCard& c, CardSlot& slot);
    void measure_comparison_card(const MeasureCtx& mc, CardSlot& slot);
    void measure_history_card(const MeasureCtx& mc, CardSlot& slot);
    // Canli test gorunumu: sahne onizlemesi (alana sigdirilir, en-boy korunur) ve ustunde gosterge panelleri — adim +
    // ilerleme, kare hizi / kare suresi, CPU / GPU yuk cubuklari (kullanim rampasi), kare suresi cubuk grafigi,
    // "Durdurmak icin Esc" + "Testi durdur" (Hit::TestStop). area: pencere koordinati (DIP).
    void live_test_view(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    // Task 12 + spec 2026-11-11 §5: Olcumler ust satirindaki siki "Internet hizi" karti (kucuk kadran, saginda sonuc
    // satirlari, altta asama cipleri + dugme). Ust satirdaki es kart yuksekligini alir; eylem satiri kartin altina
    // yaslanir.
    void speed_card(Graphics& g, CardSlot& slot, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq);
    float settings_page(Graphics& g, const RectF& body, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    // Spec 2026-11-12 H (tespit): Guncellemeler. Ust kart (buyuk durum metni, son denetim / neden satiri, "Denetle" /
    // "Yeniden dene" / "Durdur") ve satir karti (ikon · ad + tur · surum ve tarih · boyut · cip · eylem): once Windows
    // Update onerileri (firmware "Elle denetlenir", Kur yok), sonra ekran karti / yonga seti / BIOS satirlari ("Elle
    // denetlenir" + resmi uretici sayfasi). updates_page.cpp.
    float updates_page(Graphics& g, const RectF& body, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits);
    void small_toggle(Graphics& g, const RectF& r, bool on, bool enabled, Hit id, const std::wstring& name, UiState& s,
                      std::int64_t now, HitMap& hits);
    // RDY-10: sanal listede gorunmeyen satirin ogesi cizilmeden kaydedilir (ayni isabet kutusu; Tab + UIA).
    void toggle_hit(const RectF& r, bool on, bool enabled, Hit id, const std::wstring& name, HitMap& hits) const;
    RectF button_rect(Graphics& g, float x, float y, std::wstring_view label, bool in_row = false) const;
    // Tek satir metin: w genisligine sigmiyorsa sondan kisaltilir ("…"; vekil cifti bolunmez).
    std::wstring fit_text(Graphics& g, Role role, const std::wstring& text, float w) const;
    void focus_ring(Graphics& g, const RectF& r, float radius, HitMap& hits);
    std::wstring t(core::i18n::StringId id) const;
    // Denge sonucu ozeti: "1080p: CPU · 1440p: dengeli · 4K: GPU — test sahnesi" (Panel basligi, gecmis satiri, duyuru).
    std::wstring balance_summary_text(const core::measure::BalanceView& v) const;
    // Cozunurluk basina tam hukum satirlari: "1080p: CPU sinirliyor" (Sonuc karti, Panel satirlari).
    std::vector<std::wstring> balance_verdict_lines(const core::measure::BalanceView& v) const;
    std::wstring tk(std::string_view key) const;
    // Gomulu JetBrains Mono'nun SIL OFL 1.1 tam metni (katalog metin siniri nedeniyle parcali anahtarlar; satirlarla birlesir).
    std::wstring ofl_text() const;

    const Theme& theme_;
    const core::i18n::Catalog& cat_;
    const Palette* pal_;
    mutable std::vector<RectF> card_log_;  // bu karede cizilen kart cerceveleri (paint sonunda HitMap::cards)
    mutable std::vector<RectF> shadow_log_;  // bu karede golge birakan kartlar (paint sonunda UiState::shadow_casters)
    float backdrop_w_ = 0, backdrop_h_ = 0;  // son tam cizimin pencere boyutu (zemin isiltilari; bolgesel cizim de kullanir)
    bool relayout_pass_ = false;           // B12: kart yuksekligi duzeltme cizimi suruyor (en cok bir kez)
    // fit_text onbellegi: kisaltilan metin (rol + genislik + metin -> sonuc). Canli kutucuklar ayni uzun adi her karede
    // yeniden kisaltmasin (ikili arama = bir dizi metin olcumu). Sinirli boy; dolunca bosaltilir.
    struct FitKey {
        Role role;
        float w;
        std::wstring text;
        friend bool operator<(const FitKey& a, const FitKey& b) {
            if (a.role != b.role) return a.role < b.role;
            if (a.w != b.w) return a.w < b.w;
            return a.text < b.text;
        }
    };
    mutable std::map<FitKey, std::wstring> fit_cache_;
};

}  // namespace ef::ui
