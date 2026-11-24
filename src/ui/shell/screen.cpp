#include "ui/shell/screen.h"
#include "core/readiness/fix_plan.h"

#include <windows.h>

#include "ui/brand/brand.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ef::ui {

using core::i18n::StringId;

namespace {

constexpr std::array<StringId, kPageCount> kNavText{StringId::NavOverview, StringId::NavReadiness, StringId::NavGames, StringId::NavMeasurements,
                                                    StringId::NavCleanup, StringId::NavStartup, StringId::NavHistory, StringId::NavSettings,
                                                    StringId::NavSystem, StringId::NavUpdates};

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

}  // namespace

Hit HitMap::at(float x, float y) const {
    for (const auto& [id, r] : items) {
        if (!r.contains(x, y)) continue;
        const auto it = meta.find(id);
        if (it != meta.end() && (it->second.offscreen || !it->second.enabled || !interactive(it->second.role))) continue;
        // Denetim 5.3: Edit ogesi (ozel hedef alani) yalniz ekran okuyucu icindir; fare tiklamasi odagi ona tasimaz
        // (Tab duragi degil, odak halkasi kaybolurdu). Klavye odagi ve yazma ozel hedef cipindedir.
        if (it != meta.end() && it->second.role == AccRole::Edit) continue;
        return id;
    }
    return Hit::None;
}

Hit HitMap::element_at(float x, float y) const {
    // Kip penceresi (AccRole::Window) kendi ogelerinin kapsayicisidir ve haritada onlardan once gelir: noktadaki en
    // derin oge (dugme, metin, secenek) onceliklidir; pencere yalniz kartin bos yerinde doner.
    Hit container = Hit::None;
    for (const auto& [id, r] : items) {
        if (!r.contains(x, y)) continue;
        const auto it = meta.find(id);
        if (it != meta.end() && it->second.offscreen) continue;
        if (it != meta.end() && it->second.role == AccRole::Window) {
            if (container == Hit::None) container = id;
            continue;
        }
        return id;
    }
    return container;
}

bool HitMap::activatable(Hit id) const {
    const auto it = meta.find(id);
    return it != meta.end() && it->second.enabled && interactive(it->second.role);
}

bool HitMap::tabbable(Hit id) const {
    const auto it = meta.find(id);
    return it != meta.end() && it->second.tab_stop && activatable(id);
}

std::vector<Hit> selected_hits(const HitMap& hits) {
    std::vector<Hit> out;
    for (const auto& [id, r] : hits.items) {
        const auto it = hits.meta.find(id);
        if (it != hits.meta.end() && it->second.selected &&
            (it->second.role == AccRole::TabItem || it->second.role == AccRole::RadioButton))
            out.push_back(id);
    }
    return out;
}

Hit focus_step(const std::vector<Hit>& intent, const std::vector<Hit>& real, Hit current, int delta) {
    if (real.empty()) return Hit::None;
    const auto n = static_cast<std::ptrdiff_t>(real.size());
    const auto in_real = [&](Hit h) { return std::find(real.begin(), real.end(), h) != real.end(); };
    if (const auto it = std::find(real.begin(), real.end(), current); it != real.end()) {
        const auto idx = ((it - real.begin()) + (delta > 0 ? 1 : -1) + n) % n;
        return real[static_cast<std::size_t>(idx)];
    }
    // Odak devre disi/eksik bir duraktaysa: niyet sirasindaki konumundan sonraki (veya onceki) gercek durak.
    if (const auto ii = std::find(intent.begin(), intent.end(), current); ii != intent.end()) {
        if (delta > 0) {
            for (auto j = ii + 1; j != intent.end(); ++j)
                if (in_real(*j)) return *j;
            return real.front();
        }
        for (auto j = ii; j != intent.begin();)
            if (in_real(*--j)) return *j;
        return real.back();
    }
    return delta > 0 ? real.front() : real.back();
}

Hit readiness_focus_fallback(Hit focus, const HitMap& hits) {
    const int v = static_cast<int>(focus);
    int k = -1;
    for (Hit base : {Hit::ChkAction0, Hit::ChkUndo0, Hit::ChkKeep0}) {
        const int b = static_cast<int>(base);
        if (v >= b && v < b + kMaxReadinessRows) k = v - b;
    }
    if (k < 0 || hits.meta.count(focus)) return focus;
    // Ayni satirda kalan dugme: once onerilen (Geri al), sonra Duzelt/Ac, en son Koru.
    for (Hit base : {Hit::ChkUndo0, Hit::ChkAction0, Hit::ChkKeep0}) {
        const Hit h = static_cast<Hit>(static_cast<int>(base) + k);
        if (hits.meta.count(h)) return h;
    }
    return Hit::ChkRescan;
}

const RectF* HitMap::rect_of(Hit id) const {
    for (const auto& [h, r] : items)
        if (h == id) return &r;
    return nullptr;
}

std::vector<Hit> focus_order(const UiState& s, const HitMap& hits) {
    auto v = focus_order(s);
    // TH-03: roving radyo grubunun secili olmayan secenekleri UIA'da etkin kalir ama Tab duragi degildir.
    std::erase_if(v, [&](Hit h) { return !hits.tabbable(h); });
    return v;
}

float reveal_scroll(float scroll, const RectF& item, float view_top, float view_bottom, float margin) {
    const float top = view_top + margin, bottom = view_bottom - margin;
    if (item.y < top) return scroll - (top - item.y);
    if (item.bottom() > bottom) {
        const float down = item.bottom() - bottom;
        const float max_down = item.y - top;  // ust kenar gorunur alanin ustune cikmasin
        return scroll + std::min(down, std::max(0.f, max_down));
    }
    return scroll;
}

Hit updates_focus_after(Hit focus, const ViewModel::UpdatesView& v) {
    const bool checking = v.scan.state == core::drivers::ScanState::Checking;
    if (focus == Hit::UpdatesCancel && !checking) return Hit::UpdatesCheck;
    if (focus == Hit::UpdatesCheck && checking) return Hit::UpdatesCancel;
    const auto slot = classify_hit(focus);
    if (slot.kind == HitKind::UpdatesInstall && (v.scan.state != core::drivers::ScanState::Found || slot.index >= v.scan.updates.size()))
        return checking ? Hit::UpdatesCancel : Hit::UpdatesCheck;
    if (slot.kind == HitKind::UpdatesLocal && slot.index >= v.gpus.size()) return checking ? Hit::UpdatesCancel : Hit::UpdatesCheck;
    return focus;
}

std::vector<Hit> focus_order(const UiState& s) {
    std::vector<Hit> v;
    // Kip penceresi (spec 2026-11-12 E): Tab kartin icinde doner; sayfa ve ust menu ogeleri sirada yoktur.
    if (s.modal) {
        for (const ModalFocus& f : modal_focus_order(*s.modal)) v.push_back(modal_hit(f));
        return v;
    }
    // Zorunlu guncelleme karti: yalniz kart dugmeleri (ana eylem, [onceki ayarlara don], cik); sekmeler yok.
    if (s.update_gate) {
        if (!s.update_busy) v.push_back(s.update_not_writable ? Hit::UpdateOpenPage : (s.update_repair ? Hit::UpdateRepairService : Hit::UpdateNow));
        if (s.update_restore) v.push_back(Hit::UpdateRestore);
        v.push_back(Hit::UpdateQuit);
        return v;
    }
    // Ust menu sekmeleri gorunur sirada (Baslangic ust seritte yok). Spec 2026-11-12 B4: Sistem araclarinda alt sekme yok.
    for (const Page pg : kNavOrder) v.push_back(nav_hit(static_cast<std::size_t>(pg)));
    if (systools_page(s.page))
        for (const Hit j : {Hit::SysJump0, Hit::SysJump1, Hit::SysJump2, Hit::SysJump3}) v.push_back(j);
    // ADR-033 spec 8: "Kurulum gerekli" seridinin "Kur" dugmesi (ust menunun hemen altinda; sayfadan once).
    if (s.install_offer) v.push_back(Hit::InstallOfferInstall);
    // Spec G: ilk acilis bildirimi seridinin "Kapat" dugmesi (ust menunun hemen altinda; sayfadan once).
    if (s.autostart_notice) v.push_back(Hit::AutostartNoticeOff);
    // Canli test gorunumu (spec 2026-11-12 A4): icerik alani onizleme + gostergelerdir; sekmelerden sonra tek durak
    // "Testi durdur" (Durduruluyor'da devre disi: Tab almaz).
    if (s.live_view) {
        if (core::measure::can_stop(s.measure)) v.push_back(Hit::TestStop);
        return v;
    }
    // Baslik seridi ogeleri (kaydirilmaz; gorunen sirayla soldan saga): sekmelerden hemen sonra, govdeden once.
    if (s.page == Page::Games) {
        if (s.games_filters)
            for (const auto f : {GamesFilter::All, GamesFilter::Protected, GamesFilter::Advice}) v.push_back(games_filter_hit(f));
        if (s.games_why) v.push_back(Hit::GamesWhy);
        if (s.games_add_header) v.push_back(Hit::GamesAddExe);
        if (s.games_rescan) v.push_back(Hit::GamesRescan);
    }
    if (s.page == Page::Readiness) {
        // RDY-09: is surerken dugmeler kaybolmaz, devre disi cizilir; gercek Tab sirasi (hits) onlari atlar.
        if (s.readiness_net) v.push_back(Hit::ChkNetTest);
        if (s.readiness_rescan) v.push_back(Hit::ChkRescan);
    }
    if (s.page == Page::History) {
        // Spec 2026-11-14 B13: gorunum cipleri basligin saginda, "Disa aktar" seridin sag ucunda; secili olmayan cipler de
        // Tab duragidir. Kapsam alt suzgeci degisiklik kartindadir (govde sirasi).
        if (s.history_tabs)
            for (const auto v2 : {HistoryTab::All, HistoryTab::Changes, HistoryTab::Tests}) v.push_back(history_tab_hit(v2));
        // R16: disa aktarma yalniz kayit varken (veri yokken dugme devre disi).
        if (s.history_export_enabled) v.push_back(Hit::HistoryExport);
    }
    if (s.detection_retry && (s.page == Page::Overview || systools_page(s.page) || s.page == Page::Games || s.page == Page::Measurements ||
                              s.page == Page::Readiness || s.page == Page::Updates))
        v.push_back(Hit::DetectionRetry);
    if (s.page == Page::Overview && s.show_welcome) v.push_back(Hit::WelcomeStart);
    if (s.page == Page::Overview) {
        using core::perf::Phase;
        // UI-10: panel ve grup anahtarlari (yalniz Kapali/Onay asamasinda degistirilebilir)
        auto groups = [&] {
            // T04: gorsel sira = ana gruplar, "Gelismis (N)" satiri, (aciksa) gelismis gruplar.
            const auto adv = [&](std::size_t i) { return i < s.group_advanced.size() && s.group_advanced[i]; };
            for (std::size_t i = 0; i < s.group_enabled.size(); ++i)
                if (s.group_enabled[i] && !adv(i)) v.push_back(hit_of(HitKind::Group, i));
            if (s.group_advanced_count > 0) v.push_back(Hit::PerfAdvanced);
            if (s.advanced_open)
                for (std::size_t i = 0; i < s.group_enabled.size(); ++i)
                    if (s.group_enabled[i] && adv(i)) v.push_back(hit_of(HitKind::Group, i));
        };
        if (s.perf_phase == Phase::Off || s.perf_phase == Phase::On) {
            v.push_back(Hit::Toggle);
            v.push_back(Hit::Details);
            if (s.details_open) groups();
            if (s.perf_tips_shown && s.details_open) v.push_back(Hit::PerfTips);  // T20: Ayrinti acikken panelin altindaki oyun ici oneriler
            if (s.perf_drift && !s.details_open) {  // Koruma Modu: sapma bildirimi (Evet/Hayir oncesi)
                if (!s.game_blocked) v.push_back(Hit::PerfDriftReapply);
                v.push_back(Hit::PerfDriftKeep);
            }
        } else if (s.perf_phase == Phase::Confirming) {
            groups();
        }
        switch (s.perf_phase) {
            case Phase::Confirming:
            case Phase::ConfirmRestore:
                if (!s.game_blocked) v.push_back(Hit::Yes);
                v.push_back(Hit::No);
                break;
            case Phase::RecoveryNeeded:
                v.push_back(Hit::Retry);
                if (s.perf_conflict) v.push_back(Hit::PerfKeep);
                break;
            default: break;  // mesgulken odaklanabilir oge yok
        }
        // Panel sirasi: Performans bolumu (yuvarlak dugme, Ayrinti, acilan icerik) -> "Testi baslat" -> "Darbogaz"
        // kartindaki "Test et" -> "Oyun hazirligi" kartindaki "Duzelt".
        if (core::measure::can_start(s.measure) && s.measure.source == core::measure::TestSource::Builtin) v.push_back(Hit::TestStart);
        v.push_back(Hit::OpenMeasure);
        v.push_back(Hit::OpenReadiness);
        // Kartin bulgu satirlarindaki eylem dugmeleri (Oyun hazirligi sayfasiyla ayni kimlik ve islem).
        for (const std::size_t k : s.panel_readiness_actions) v.push_back(hit_of(HitKind::ChkAction, k));
    }
    if (systools_page(s.page)) {
        using core::cleanup::SessionPhase;
        const auto ph = s.cleanup_phase;
        // Gorunen sira (spec 2026-11-14 B12): kart baslik seridindeki eylemler (Depolama ayarlari, Onizleme, Temizle /
        // Iptal) -> [onay: uygulama ici kip penceresi; sayfadaki eski Evet / Hayir yalniz kip penceresi gosterilemezse
        // niyet sirasinda kalir] -> ilerleme seridi (Durdur) -> surucu haplari -> kategori kutulari (anahtar, dosya
        // listesi) -> "+N kategori daha".
        // EXT-CLN: depolama ayarlari dugmesi yalniz acar; calistirma/onay sirasinda gosterilmez.
        if (ph != SessionPhase::Running && ph != SessionPhase::Confirming) v.push_back(Hit::CleanStorageSettings);
        if (ph == SessionPhase::Ready || ph == SessionPhase::Done || ph == SessionPhase::Idle || ph == SessionPhase::Failed)
            v.push_back(Hit::CleanScan);
        if (ph == SessionPhase::Ready && s.cleanup_can_run) v.push_back(Hit::CleanRun);
        if (ph == SessionPhase::Scanning) v.push_back(Hit::CleanScanCancel);  // EXT-CLN: tarama iptali
        if (ph == SessionPhase::Confirming) {
            if (!s.game_blocked) v.push_back(Hit::CleanYes);
            v.push_back(Hit::CleanNo);
        }
        if (ph == SessionPhase::Running) v.push_back(Hit::CleanStop);
        if (ph != SessionPhase::Scanning && ph != SessionPhase::Running && ph != SessionPhase::Confirming)
            for (std::size_t k = 0; k < s.cleanup_volumes; ++k) v.push_back(hit_of(HitKind::CleanVolume, k));
        if (ph == SessionPhase::Ready || ph == SessionPhase::Done) {
            for (std::size_t i = 0; i < s.cleanup_toggles.size(); ++i) {
                if (s.cleanup_toggles[i]) v.push_back(hit_of(HitKind::CleanToggle, i));
                if (i < s.cleanup_file_buttons.size() && s.cleanup_file_buttons[i])
                    v.push_back(hit_of(HitKind::CleanFiles, i));
            }
        }
        if (s.cleanup_more) v.push_back(Hit::CleanMore);
    }
    if (systools_page(s.page)) {
        // Baslangic bolumu (Temizlik bolumunun altinda): baslik satirindaki "Uygula" -> satir anahtarlari -> hizmetler.
        using core::startup::StartupPhase;
        const auto ph = s.startup_phase;
        if (ph == StartupPhase::Ready) {
            if (s.startup_can_apply) v.push_back(Hit::StartupApply);
            for (std::size_t i = 0; i < s.startup_toggles.size(); ++i)
                if (s.startup_toggles[i]) v.push_back(hit_of(HitKind::StartupToggle, i));
            for (std::size_t i = 0; i < s.startup_service_toggles.size(); ++i)
                if (s.startup_service_toggles[i]) v.push_back(hit_of(HitKind::StartupServiceToggle, i));
        }
        if (ph == StartupPhase::Failed) v.push_back(Hit::StartupRescan);
        if (ph == StartupPhase::Confirming) {
            if (!s.game_blocked) v.push_back(Hit::StartupYes);
            v.push_back(Hit::StartupNo);
        }
    }
    if (s.page == Page::Readiness) {
        // Gorunen sira (ONERILER -> IYI DURUMDA); kimlik bulgu indeksidir. Satir dugmeleri is surerken devre disidir.
        std::vector<std::size_t> rows = s.readiness_order;
        if (rows.empty())
            for (std::size_t k = 0; k < s.readiness_actions.size(); ++k) rows.push_back(k);
        for (const std::size_t k : rows) {
            if (k >= s.readiness_actions.size()) continue;
            if (s.readiness_actions[k]) v.push_back(hit_of(HitKind::ChkAction, k));
            if (k < s.readiness_undos.size() && s.readiness_undos[k])
                v.push_back(hit_of(HitKind::ChkUndo, k));
            if (k < s.readiness_keeps.size() && s.readiness_keeps[k])
                v.push_back(hit_of(HitKind::ChkKeep, k));
            // B11: kutucugun "Ayrinti" ac/kapa dugmesi dugme satirinin saginda (kutucuk sirasi = gorunen sira).
            if (k < s.readiness_details.size() && s.readiness_details[k]) v.push_back(hit_of(HitKind::ChkDetail, k));
        }
        // "Sistem kontrolu · N bulgu" ac/kapa (bulgu listesinin altinda, tek tik kontrolun ustunde); altinda grup kutucuklari.
        v.push_back(Hit::ChkCheckup);
        for (const FindingGroup grp : s.readiness_groups) v.push_back(hit_of(HitKind::ChkGroup, static_cast<std::size_t>(grp)));
        if (s.bios_guide_shown) v.push_back(Hit::BiosGuideToggle);  // T20: acik grupta XMP bulgusunun "Adim adim" rehberi
        // OPT-07: tek tik sistem kontrolu (spec 2026-11-12 B.4: Panel'den bu sayfaya tasindi; kimlikler ve akis ayni).
        using core::perf::CheckPhase;
        const auto ph = s.check_phase;
        if (ph == CheckPhase::Idle || ph == CheckPhase::Report || ph == CheckPhase::Failed) v.push_back(Hit::CheckStart);
        if (ph == CheckPhase::Review) {
            for (std::size_t i = 0; i < s.check_toggles.size(); ++i)
                if (s.check_toggles[i]) v.push_back(hit_of(HitKind::CheckToggle, i));
            if (!s.game_blocked) v.push_back(Hit::CheckYes);
            v.push_back(Hit::CheckNo);
        }
    }
    if (s.page == Page::Games) {
        // Gorunen sira (spec 2026-11-12 B2): (baslik seridi yukarida) oyun kartlari -> kaynaklar -> rehberler. Her kart
        // bir duraktir; secili oyunun serit islemleri (yalniz onun bayraklari doludur) kartinin HEMEN ardindan gelir:
        // gorunen dugme sirasi (BOOST maketi) NVIDIA -> Ekran iyilestirme -> Profesyonel ayarlar -> Listeden cikar.
        const std::size_t cards =
            std::max({s.game_cards.size(), s.game_guides.size(), s.game_fs.size(), s.game_manual.size(), s.game_nv.size()});
        const auto stop = [&](std::size_t k) {
            if (k < s.game_cards.size() && s.game_cards[k]) v.push_back(hit_of(HitKind::GameCard, k));
            if (k < s.game_nv.size() && s.game_nv[k]) v.push_back(hit_of(HitKind::GameNvidia, k));
            if (k < s.game_fs.size() && s.game_fs[k]) v.push_back(hit_of(HitKind::GameFullscreen, k));
            if (k < s.game_guides.size() && s.game_guides[k]) v.push_back(hit_of(HitKind::GameGuide, k));
            if (k < s.game_manual.size() && s.game_manual[k]) v.push_back(hit_of(HitKind::GameManualRemove, k));
        };
        if (s.game_card_order.empty())
            for (std::size_t k = 0; k < cards; ++k) stop(k);
        else
            for (const std::size_t k : s.game_card_order) stop(k);  // suzgecin gosterdigi kartlar, gorunen sirada
        if (!s.games_add_header) v.push_back(Hit::GamesAddExe);
        v.push_back(Hit::GamesGuides);
    }
    if (s.page == Page::Settings) {
        // P12-B3: gorunen sira — Dil, Yazilim guncellemesi, "Gelismis" (acilirsa: Windows ile baslat, Animasyonlar, RGB serit,
        // Oyun onaylari (Goster/Gizle + acikken Kaldir dugmeleri), Destek paketi), en sonda "Hakkinda ve lisanslar".
        v.push_back(Hit::SettingsLanguage);     // denetim 5.2: uygulama dili
        v.push_back(Hit::SettingsUpdateCheck);  // el ile guncelleme denetimi (denetlenirken devre disi)
        v.push_back(Hit::SettingsAdvanced);
        if (s.settings_advanced_open) {
            v.push_back(Hit::Autostart);
            v.push_back(Hit::AppAnimations);  // UI-11/R33
            v.push_back(Hit::SettingsStrip);  // RGB isik seridi
            v.push_back(Hit::SettingsCleanupHint);  // R-C2
            v.push_back(Hit::SettingsGamesShow);
            // Oyun satirinda yalniz "Kaldir" dugmesi cizilir (ayri izin anahtari yok); olu Tab duragi eklenmez.
            for (std::size_t i = 0; i < s.consent_rows && i < static_cast<std::size_t>(kMaxConsentRows); ++i) v.push_back(consent_reset_hit(i));
            // R21: destek paketi (paket hazirlanirken dugme devre disi kalir; gercek sira onu atlar). Cokme kaydi secenegi
            // Olustur'un kip penceresindedir (spec B7).
            v.push_back(Hit::SupportCreate);
        }
        v.push_back(Hit::SettingsLicenses);
    }
    if (s.page == Page::Updates) {
        // Spec 2026-11-12 H: ust kart ("Denetle" / "Yeniden dene" ya da arama surerken "Durdur") -> satirlardaki resmi
        // uretici sayfasi dugmeleri (gorunen sira). Oyun acikken "Denetle" devre disidir (gercek sira atlar).
        v.push_back(s.updates_checking ? Hit::UpdatesCancel : Hit::UpdatesCheck);
        // Spec H2 / ADR-032: satir eylemleri gorunen sirayla (Kur / Indir ve kur / Dosyadan kur).
        for (const Hit h : s.updates_actions) v.push_back(h);
        for (std::size_t k = 0; k < s.updates_links; ++k) v.push_back(hit_of(HitKind::UpdatesLink, k));
    }
    if (s.page == Page::History) {
        // Govde (baslik seridi yukarida): journal satirlarinin ac/kapa dugmeleri (gorunen sira), sonra R16 test
        // kayitlarinin sec dugmeleri.
        // Spec 2026-11-12 B6: satirin kendisi ac/kapa dugmesidir; acik satirin "Panel'e git" dugmesi hemen ardindan.
        // B13: degisiklik kartinin baslik seridindeki kapsam alt suzgeci satirlardan once.
        if (s.history_filters)
            for (const auto f : {HistoryFilter::All, HistoryFilter::Perf, HistoryFilter::Fixes, HistoryFilter::Startup})
                if (s.history_filter_shown[static_cast<std::size_t>(f)]) v.push_back(history_filter_hit(f));
        for (const std::size_t k : s.history_op_rows) {
            v.push_back(hit_of(HitKind::HistoryRow, k));
            if (std::find(s.history_go_rows.begin(), s.history_go_rows.end(), k) != s.history_go_rows.end())
                v.push_back(hit_of(HitKind::HistoryGo, k));
        }
        for (std::size_t i = 0; i < s.history_test_rows; ++i) v.push_back(hit_of(HitKind::HistorySelect, i));
    }
    if (s.page == Page::Measurements) {
        const auto& m = s.measure;
        // M05/M07: test islerken (Durduruluyor dahil) kaynak/sure/birim devre disi cizilir: UIA'da kalir, Tab almaz.
        // R13: uzun test surerken de ayni kilit gecerlidir (ikinci test ayni anda calismaz).
        const bool locked = core::measure::busy(m) || m.endurance.running;
        // Gorunen sira (spec 2026-11-12 C, B10): Test karti (yuvarlak baslat/durdur, kaynak ve kapsam haplari, kart ici
        // Sure / Depolama testi / Canli test ekrani satirlari) -> Internet hizi karti -> uzun test -> "Son 3 test" satirlari.
        if (!locked) {
            v.push_back(Hit::SourceBuiltin);
            v.push_back(Hit::SourceGame);
            // Spec 2026-11-12 A5: kapsam secimi yalniz Uygulama Testi seciliyken cizilir.
            if (m.source == core::measure::TestSource::Builtin) {
                v.push_back(Hit::ScopeGeneral);
                v.push_back(Hit::ScopeBalance);
            }
        }
        // B10: test secenekleri Test kartinin icinde (Sure, Depolama testi, Canli test ekrani); test islerken Tab almaz.
        if (m.source == core::measure::TestSource::Builtin && !locked) {
            v.push_back(Hit::ModeQuick);
            v.push_back(Hit::ModeFull);
        }
        if (m.source == core::measure::TestSource::Builtin && m.scope == core::measure::BuiltinScope::General && !locked && !m.storage_roots.empty())
            for (std::size_t k = 0; k <= std::min(m.storage_roots.size(), core::measure::kMaxStorageRoots); ++k)
                v.push_back(hit_of(HitKind::StorageChoice, k));
        // A4: "Canli test ekrani" anahtari yalniz Denge testi kapsaminda cizilir; test islerken devre disi (Tab almaz).
        if (s.measure_live_toggle && !locked) v.push_back(Hit::LiveView);
        // C: baslat / durdur dugmesi seridin sag ucunda (gorunen sirada son); Durduruluyor: dugme devre disi, Tab yok.
        if (core::measure::can_stop(m)) v.push_back(Hit::TestStop);
        else if (core::measure::can_start(m)) v.push_back(Hit::TestStart);
        // Task 12: internet hiz testi. Oyun kapisi kapaliyken baslat devre disi (Tab almaz).
        if (s.speed_running) v.push_back(Hit::SpeedStop);
        else if (s.speed_can_start) v.push_back(Hit::SpeedStart);
        if (s.server_ping_can_start) v.push_back(Hit::SpeedServerPing);  // R-C4
        // R13: uzun test yalniz kullanici baslatir; otomatik baslatma/tekrar yok.
        // Kart her zaman cizilir (B10); hata durumunda dugme yoktur.
        if (m.endurance.running) v.push_back(Hit::EnduranceStop);
        else if (core::measure::endurance_can_start(m) && !m.endurance.failed) v.push_back(Hit::EnduranceStart);
        // P1: Performans etkisini olc; yalniz kullanici baslatir.
        if (m.proof.running()) v.push_back(Hit::ProofStop);
        else if (core::measure::proof_can_start(m)) v.push_back(Hit::ProofStart);
        // P3: Darbogaz karti "Bu PC'de olculemedi" gosterirken (rapor hazirlanirken Tab almaz).
        if (core::measure::balance_report_offered(m) && !s.balance_report_busy) v.push_back(Hit::BalanceReport);
        for (std::size_t k = 0; k < s.measure_recent_rows; ++k) v.push_back(hit_of(HitKind::MeasureRecent, k));
    }
    std::erase(v, Hit::None);  // RDY-01: blok sinirini asan satirin kimligi yoktur (hit_of -> None)
    return v;
}

std::wstring Screen::t(StringId id) const { return widen(cat_.get(id)); }
std::wstring Screen::tk(std::string_view key) const {
    const auto id = core::i18n::id_of(key);
    return id ? t(*id) : std::wstring();
}

void Screen::set_hover(UiState& s, Hit h, std::int64_t now) const {
    if (s.hover == h) return;
    auto set = [&](Hit x, float target) {
        if (x == Hit::None) return;
        // Spec 2026-11-12 B2: oyun karti hover'da yukselir; gecisi motion.card_lift (200 ms), digerleri motion.hover.
        // Spec F: yukselme bir susleme gecisidir — oyun acikken / test surerken / pilde (s.motion_paused) zamanlayici
        // calistirmaz, kart aninda yerine oturur (deger gecisleri ve kip penceresi acilisiyla ayni kural).
        const bool card = classify_hit(x).kind == HitKind::GameCard;
        const int ms = card ? effective_ms(theme_.motion.card_lift, s.reduced_motion || s.motion_paused) : effective_ms(theme_.motion.hover, s.reduced_motion);
        const int i = static_cast<int>(x);
        if (i >= 0 && i < static_cast<int>(kPageCount)) {
            s.nav_hover[static_cast<std::size_t>(i)].retarget(target, now, ms);
            return;
        }
        if (x == Hit::Toggle) {
            s.toggle_hover.retarget(target, now, ms);
            return;
        }
        // DS-02: oge basina ayri gecis; Details/WelcomeStart/Yes gibi ayni slotu paylasan ogeler artik capraz etkilenmez.
        s.hover_tween[x].retarget(target, now, ms);
    };
    set(s.hover, 0.f);
    set(h, 1.f);
    s.hover = h;
}

void Screen::set_pressed(UiState& s, Hit h, std::int64_t now) const {
    if (s.pressed == h) return;
    const int ms = effective_ms(theme_.motion.press, s.reduced_motion);
    if (s.pressed != Hit::None) s.press_tween[s.pressed].retarget(0, now, ms);
    if (h != Hit::None) s.press_tween[h].retarget(1, now, ms);
    s.pressed = h;
}

float Screen::hover_value(const UiState& s, Hit h, std::int64_t now) const {
    if (h == Hit::None) return 0.f;
    const int i = static_cast<int>(h);
    if (i >= 0 && i < static_cast<int>(kPageCount)) return s.nav_hover[static_cast<std::size_t>(i)].value(now, theme_.easing);
    if (h == Hit::Toggle) return s.toggle_hover.value(now, theme_.easing);
    const auto it = s.hover_tween.find(h);
    return it == s.hover_tween.end() ? 0.f : it->second.value(now, theme_.easing);
}

float Screen::press_value(const UiState& s, Hit h, std::int64_t now) const {
    if (h == Hit::None) return 0.f;
    const auto it = s.press_tween.find(h);
    return it == s.press_tween.end() ? 0.f : it->second.value(now, theme_.easing);
}

void Screen::go(UiState& s, Page p, std::int64_t now) const {
    // Spec 2026-11-12 B4: eski alt sekme kimlikleri (Baslangic / Sistem) ayni tek sayfayi acar ve o bolume kaydirir.
    if (p == Page::Startup || p == Page::System) s.systools_reveal = p == Page::Startup ? 2 : 4;
    else if (p == Page::Cleanup) s.systools_reveal = 0;
    if (s.page == p) return;
    s.page = p;
    s.scroll = 0;
    s.page_fade = Tween(0);
    s.page_fade.retarget(1, now, effective_ms(theme_.motion.page, s.reduced_motion));
    // Spec 2026-11-12 F: Panel'e her giriste sayilar sayac gibi akar, hazirlik cubugu dolar.
    if (p == Page::Overview) {
        s.panel_num_seen = {};
        s.readiness_fill_seen = false;
    }
    if (p == Page::Readiness) s.readiness_ring_seen = false;  // B3: halka her giriste dolar
    if (systools_page(p)) s.cleanup_ring_to = -1.f;            // B4: halka giriste yerine oturur, secim degisince doner
    if (p == Page::Measurements) {                           // C: sonuc sayilari sayac gibi, darbogaz cubuklari dolar
        s.measure_num_seen = {};
        s.measure_bars_seen = false;
    }
}

bool Screen::live_animating(const UiState& s, std::int64_t now) const {
    for (const auto& v : s.live_value)
        if (v.active(now)) return true;
    return false;
}

bool Screen::animating(const UiState& s, std::int64_t now, bool include_live) const {
    if (s.toggle_hover.active(now) || s.toggle_knob.active(now) || s.page_fade.active(now) || s.progress_fill.active(now) ||
        s.card_fade.active(now) || s.brand_reveal.active(now) || s.readiness_fill.active(now) || s.readiness_ring.active(now) ||
        s.modal_fade.active(now) || s.measure_bars.active(now) || s.cleanup_ring.active(now) || s.history_anim.active(now) ||
        s.history_bars.active(now))
        return true;
    for (const auto& v : s.panel_num)
        if (v.active(now)) return true;
    for (const auto& v : s.measure_num)
        if (v.active(now)) return true;
    for (const auto& kv : s.hover_tween)
        if (kv.second.active(now)) return true;
    for (const auto& kv : s.press_tween)
        if (kv.second.active(now)) return true;
    for (const auto& n : s.nav_hover)
        if (n.active(now)) return true;
    if (s.speed_needle.active(now)) return true;
    return include_live && live_animating(s, now);
}

void Screen::focus_ring(Graphics& g, const RectF& r, float radius, HitMap& hits) {
    hits.focus_drawn = true;
    // TH-02/M08/DS-09: odak halkasi secim cercevesinden (ogenin kenarinda 2 DIP accent) ayrilir: iki tonlu.
    // Dis: focus_ring rengi, token genisligi (keyboard_focus_ring_width), ogenin secim cizgisinin disinda baslar.
    // Ic: arka plan renginde 1 DIP ince cizgi; halkayi ogeden ve secim cizgisinden ayirir, secim cizgisinin ustune
    // binmez. Renk + konum + iki ton; yalniz renge dayanmaz. Yaricap/konum sabitleri halkanin kendi geometrisidir
    // (token'lasacak tasarim olcusu degil; DS-13).
    constexpr float kSelectionStrokeW = 2.f;  // secim cercevesi kalinligi (cizim siteleriyle ayni)
    constexpr float kFocusSeparatorInset = 1.5f;
    constexpr float kFocusSeparatorW = 1.f;
    const auto& p = pal();
    const float w = theme_.focus_ring_w;
    const float o = kSelectionStrokeW + w / 2;
    g.stroke_round({r.x - o, r.y - o, r.w + 2 * o, r.h + 2 * o}, radius + o, p.focus_ring, w);
    g.stroke_round({r.x - kFocusSeparatorInset, r.y - kFocusSeparatorInset, r.w + 2 * kFocusSeparatorInset,
                    r.h + 2 * kFocusSeparatorInset},
                   radius + kFocusSeparatorInset, p.background, kFocusSeparatorW);
}

namespace {
// Ust menu sekme adi (UIA): Cleanup yuvasi "Sistem araclari" sayfasidir; ad her zaman tam addir.
StringId topbar_name(Page pg) {
    if (pg == Page::Cleanup) return StringId::NavSystemTools;
    return kNavText[static_cast<std::size_t>(pg)];
}
// Ust menude gorunen etiket (spec 2026-11-12 B5): Cleanup yuvasinin kisa adi "Sistem" (sayfa basligi "Sistem
// araclari" kalir); cok dar pencerede "Oyun hazirligi" -> "Hazirlik".
StringId topbar_label(Page pg, bool short_labels) {
    if (pg == Page::Cleanup) return StringId::NavSystem;
    if (pg == Page::Readiness && short_labels) return StringId::NavReadinessShort;
    return kNavText[static_cast<std::size_t>(pg)];
}
constexpr float kStatusDot = 8.f;  // Performans gostergesi noktasi (DIP)
// BOOST maketi durum hapi: px-2.5 (10) + 1 DIP cizgi, nokta 8, gap-1.5 (6), 12px/500 metin; yukseklik 28 (py-1 + satir 18
// + cizgi), yuvarlak. Zemin beyaz %4, cizgi beyaz %8.
constexpr float kStatusPillPad = 11.f, kStatusPillGap = 6.f, kStatusPillH = 28.f;
bool perf_on(const PerfView& pv) {
    return pv.phase == core::perf::Phase::On || pv.phase == core::perf::Phase::ConfirmRestore;
}
// Ust menu logosu: yatay logo ya da yalniz amblem; koyu palette renkli (yesil EF, camgobegi ok ucu), yuksek kontrastta
// tek renk (cizimde mono renk verilir).
const SvgDoc& topbar_logo(bool mark_only, bool dark) {
    using ef::ui::brand::Logo;
    if (mark_only) return ef::ui::brand::logo(dark ? Logo::MarkColor : Logo::MarkCurrent);
    return ef::ui::brand::logo(dark ? Logo::LockupDark : Logo::LockupWhite);
}
}  // namespace

namespace {
// Ust menu sekme ikonu (kNavOrder sayfalari; Sistem araclari yuvasi Cleanup).
Icon nav_icon(Page pg) {
    switch (pg) {
        case Page::Overview: return Icon::NavOverview;
        case Page::Games: return Icon::NavGames;
        case Page::Readiness: return Icon::NavReadiness;
        case Page::Measurements: return Icon::NavMeasurements;
        case Page::Updates: return Icon::NavUpdates;
        case Page::History: return Icon::NavHistory;
        case Page::Settings: return Icon::NavSettings;
        default: return Icon::NavSystem;
    }
}
}  // namespace

TopbarMetrics Screen::topbar_metrics(Graphics& g) {
    // Ust menu olculeri: etiketler gercek yazi tipiyle olculur (DIP); logo marka kuralina gore.
    TopbarMetrics m;
    constexpr float kUnbounded = 10000.f;
    for (std::size_t i = 0; i < kNavTabCount; ++i)
        m.label_w[i] = g.measure(Role::Body, t(topbar_label(kNavOrder[i], false)), kUnbounded, false).w;
    m.label_w_short_readiness = g.measure(Role::Body, t(StringId::NavReadinessShort), kUnbounded, false).w;
    m.nav_icon_w = kNavIconSize + kNavIconGap;
    // Logo: boyut degismez (spec 2026-11-11 §2: yatay logo logo_lockup_w = 200 DIP genisliginde SVG kutusu; dar modda
    // amblemin gorunen yuksekligi logo_mark_h = 30). Yerlesime MUREKKEP kutusu verilir (spec 2026-11-12 B5: SVG'nin
    // kendi ic boslugu kirpilir): logo sutunun sol kenarinda baslar, sekmelere olan bosluk murekkebin sag kenarindan
    // olculur.
    const bool dark = pal_ == &theme_.dark;
    {
        const auto& doc = topbar_logo(false, dark);
        const float k = doc.view_w > 0 ? std::max(theme_.logo_lockup_w, ef::ui::brand::kLockupMinWidthDip) / doc.view_w : 0.f;
        m.lockup_w = std::max(doc.max_x - doc.min_x, 0.f) * k;
        m.lockup_h = std::max(doc.max_y - doc.min_y, 0.f) * k;
    }
    {
        const auto& doc = topbar_logo(true, dark);
        const float ink_h = doc.max_y - doc.min_y;
        const float k = ink_h > 0 ? theme_.logo_mark_h / ink_h : 0.f;
        m.mark_w = std::max(doc.max_x - doc.min_x, 0.f) * k;
        m.mark_h = ink_h > 0 ? theme_.logo_mark_h : 0.f;
    }
    const float on_w = std::ceil(g.measure(Role::Medium12, t(StringId::TopbarPerfOn), kUnbounded, false).w);
    const float off_w = std::ceil(g.measure(Role::Medium12, t(StringId::TopbarPerfOff), kUnbounded, false).w);
    m.status_dot_w = kStatusDot + theme_.space(1);
    m.status_w = 2 * kStatusPillPad + kStatusDot + kStatusPillGap + std::max(on_w, off_w);
    m.status_compact_w = kStatusDot + theme_.space(1) +
                         std::max(g.measure(Role::Secondary, t(StringId::TopbarPerfOn), kUnbounded, false).w,
                                  g.measure(Role::Secondary, t(StringId::TopbarPerfOff), kUnbounded, false).w);
    return m;
}

void Screen::paint_strip(Graphics& g, float w, const UiState& s, std::int64_t now) const {
    const auto& p = pal();
    const RectF r{0, 0, std::max(w, 0.f), std::max(theme_.strip_h, 0.f)};
    if (r.w <= 0 || r.h <= 0) return;
    // Hizli yol (yalniz serit) kesirli DPI'da seridin son kismi piksel satirini da yeniden yazar; o satirin serit
    // disindaki kismi menu zeminiyle doldurulur (tam karede topbar bu bandi zaten ayni renkle ortuyor).
    g.fill({0, r.h, r.w, 1}, p.sidebar);
    if (pal_ != &theme_.dark) {  // yuksek kontrast: tek renk cizgi, hareket yok
        g.fill(r, p.accent);
        return;
    }
    // Spec §3 serit renkleri (kirmizi -> sari -> yesil -> camgobegi -> mor); son durak ilk renge doner ki
    // sarmali kayma dikissiz olsun. Hareket kapaliysa kayma 0 (sabit gradyan).
    const GradientStop stops[] = {{0.f, p.error}, {0.2f, p.warning}, {0.4f, p.accent}, {0.6f, p.cyan}, {0.8f, p.violet}, {1.f, p.error}};
    g.fill_gradient(r, stops, s.strip_live ? strip_phase(now, theme_.strip_period_ms) : 0.f);
}

void Screen::topbar(Graphics& g, const ShellLayout& l, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    const auto& p = pal();
    const auto& e = theme_.easing;
    paint_strip(g, l.strip.w, s, now);
    g.fill(l.topbar, p.sidebar);
    // Alt kenar: koyu temada maketteki border-white/[0.08] (dekoratif ayirici), yuksek kontrastta kenarlik token'i.
    g.fill({l.topbar.x, l.topbar.bottom() - 1, l.topbar.w, 1}, pal_ == &theme_.dark ? mix(p.sidebar, Color{1, 1, 1, 1}, 0.08f) : p.border);
    // Marka: yatay logo; dar pencerede yalniz amblem. Koyu palette marka renkleri SVG'den; yuksek kontrastta tek renk.
    {
        const bool dark = pal_ == &theme_.dark;  // UI-14: HC paletinde tek renk beyaz kilit kullanilir
        const auto& doc = topbar_logo(topbar_mark_only(l.mode), dark);
        const float ink_w = doc.max_x - doc.min_x, ink_h = doc.max_y - doc.min_y;
        g.set_alpha(s.brand_reveal.value(now, theme_.easing));  // yalniz acilista 160 ms; azaltilmis harekette aninda
        if (!doc.shapes.empty() && ink_w > 0 && ink_h > 0 && l.brand.w > 0 && l.brand.h > 0) {
            // B5: marka kutusu logonun murekkep kutusudur. SVG kutusu, murekkebin sol kenari icerik sutununun sol
            // kenarina (sayfa basligi ve kartlarla ayni hizaya), dikey ortasi sekme yazilarinin ortasina gelecek
            // bicimde kaydirilir; SVG'nin kendi ic boslugu boylece kirpilmis olur. Oran korunur.
            const float k = std::min(l.brand.w / ink_w, l.brand.h / ink_h);
            const RectF box{l.brand.x - doc.min_x * k, l.brand.y + l.brand.h / 2 - (doc.min_y + doc.max_y) / 2 * k, doc.view_w * k,
                            doc.view_h * k};
            g.svg(doc, box, dark ? std::nullopt : std::optional<Color>(p.text_primary), p.accent);
        } else {
            g.text(Role::Section, t(StringId::AppTitle), {l.brand.x, l.topbar.y, l.brand.w, l.topbar.h}, p.text_primary,
                   {Align::Leading, false, true});
        }
        g.set_alpha(1);
    }
    const bool short_labels = topbar_short_labels(l.mode);
    for (const Page pg : kNavOrder) {
        const auto i = static_cast<std::size_t>(pg);
        const auto& r = l.nav[i];
        const bool selected = nav_tab_selected(pg, s.page);
        const float hv = s.nav_hover[i].value(now, e);
        // Sekme zemini yuvarlak koseli (36 DIP, seritte ortali); secim ayrica seridin alt kenarindaki 2 DIP vurgu
        // cizgisiyle gosterilir (yalniz renge dayanmaz).
        // BOOST maketi: secili sekme vurgu %10 zemin, 13px/500 vurgu metni; alt cizgi sekmenin 8 DIP iceriden 2 DIP,
        // seridin alt kenarina (bottom -8px) oturur ve 8 DIP vurgu parlamasi tasir. Hover: beyaz %4 zemin, beyaz metin.
        constexpr float kSelLineH = 2.f, kSelInset = 8.f, kSelGlow = 8.f;
        const bool dark = pal_ == &theme_.dark;
        if (selected)
            g.fill_round(r, theme_.radius_control, mix(p.sidebar, p.accent, dark ? 0.10f : alpha_scale::kQuietSelectionTint));
        else if (hv > 0.001f)
            g.fill_round(r, theme_.radius_control, dark ? mix(p.sidebar, Color{1, 1, 1, 1}, 0.04f * hv) : mix(p.sidebar, p.surface_raised, hv));
        if (selected) {
            const RectF line{r.x + kSelInset, l.topbar.bottom() - kSelLineH - 0.5f, std::max(r.w - 2 * kSelInset, 0.f), kSelLineH};
            if (dark && glow_allowed(s)) g.soft_shadow(line, 0.f, 0.f, 0.f, kSelGlow, p.accent);
            g.fill(line, p.accent);
        }
        const Color fg = selected ? p.accent : mix(p.text_secondary, dark ? Color{1, 1, 1, 1} : p.text_primary, hv);
        const Role tab_role = selected && dark ? Role::Medium13 : Role::Body;
        // Etiket sekme kutusunda ortali: iki yandaki ic bosluk butun sekmelerde ayni (B5). Ikon varsa ikon + etiket
        // birlikte ortalanir; ikon etiketle ayni renkte (secili sekmede vurgu rengi).
        const auto label = t(topbar_label(pg, short_labels));
        if (l.nav_icons) {
            constexpr float kUnbounded = 10000.f;
            const float tw = std::ceil(g.measure(tab_role, label, kUnbounded, false).w);
            const float x0 = r.x + std::max((r.w - (kNavIconSize + kNavIconGap + tw)) / 2, 0.f);
            g.draw_icon(nav_icon(pg), {x0, r.y + (r.h - kNavIconSize) / 2, kNavIconSize, kNavIconSize}, fg);
            const float tx = x0 + kNavIconSize + kNavIconGap;
            g.text(tab_role, label, {tx, r.y, std::max(r.right() - tx, 0.f), r.h}, fg, {Align::Leading, false, true});
        } else {
            g.text(tab_role, label, r, fg, {Align::Center, false, true});
        }
        if (s.focus_visible && s.focus == nav_hit(i)) focus_ring(g, r, theme_.radius_control, hits);
        // UIA adi her zaman tam ad (kisaltma yalniz gorsel): "Sistem" sekmesi "Sistem araclari" diye okunur.
        hits.add(nav_hit(i), r, t(topbar_name(pg)), AccRole::TabItem, selected);
    }
    // Performans gostergesi: nokta + metin (dar pencerede yalniz nokta). Tiklanmaz; UIA'da metin olarak okunur.
    {
        const bool on = perf_on(vm.perf);
        const auto text = t(on ? StringId::TopbarPerfOn : StringId::TopbarPerfOff);
        const RectF& r = l.status;
        if (r.w > 0) {
            // Kutu iki metnin genisine gore ayrilir; gorunen metin kisaysa nokta + metin saga yaslanir (icerik
            // sutununun sag kenariyla hizali kalir).
            const bool dark = pal_ == &theme_.dark;
            const bool pill = l.status_pill && l.mode != TopbarMode::DotOnly;
            const Role st_role = pill ? Role::Medium12 : Role::Secondary;
            const float st_gap = pill ? kStatusPillGap : theme_.space(1);
            float x0 = r.x;
            if (!pill && l.mode != TopbarMode::DotOnly) {
                // Sikisik bicim (dar pencere / buyuk metin): hapsiz nokta + metin, saga yasli.
                constexpr float kUnbounded = 10000.f;
                const float tw = std::ceil(g.measure(st_role, text, kUnbounded, false).w);
                x0 = std::max(r.x, r.right() - (kStatusDot + st_gap + tw));
            }
            if (pill) {
                // Hap: metne gore genislik, saga yasli (icerik sutununun sag kenari), seritte dikey ortali.
                constexpr float kUnbounded = 10000.f;
                const float tw = std::ceil(g.measure(Role::Medium12, text, kUnbounded, false).w);
                const float pw = 2 * kStatusPillPad + kStatusDot + kStatusPillGap + tw;
                const RectF box{std::max(r.x, r.right() - pw), r.y + (r.h - kStatusPillH) / 2, std::min(pw, r.w), kStatusPillH};
                if (dark) {
                    g.fill_round(box, box.h / 2, Color{1, 1, 1, 0.04f});
                    g.stroke_round(box.inset(0.5f), box.h / 2 - 0.5f, Color{1, 1, 1, 0.08f});
                } else {
                    g.stroke_round(box.inset(0.5f), box.h / 2 - 0.5f, p.border);
                }
                x0 = box.x + kStatusPillPad;
            }
            // Nokta: Acik vurgu, Kapali hata rengi (maket); yuksek kontrastta sistem renkleri.
            g.fill_circle(x0 + kStatusDot / 2, r.y + r.h / 2, kStatusDot / 2, on ? p.accent : (dark ? p.error : p.text_secondary));
            if (l.mode != TopbarMode::DotOnly) {
                const float tx = x0 + kStatusDot + st_gap;
                g.text(st_role, text, {tx, r.y, std::max(r.right() - tx, 0.f), r.h}, on ? p.text_primary : p.text_secondary,
                       {Align::Leading, false, true});
            }
            // UIA kutusu metin satiri yuksekliginde (seridin tamami degil), dikey ortali.
            const float line = theme_.role(Role::Secondary).line;
            hits.add_text(Hit::TopbarStatus, {r.x, r.y + std::max(r.h - line, 0.f) / 2, r.w, std::min(line, r.h)}, text);
        }
    }
}

namespace {
// Sayfa basligi: Temizlik ve Baslangic ayni sekmenin (Sistem araclari) alt sayfalaridir.
StringId page_title(Page pg) {
    if (systools_page(pg)) return StringId::NavSystemTools;
    const auto i = static_cast<std::size_t>(pg);
    return i < kNavText.size() ? kNavText[i] : StringId::AppTitle;
}
}  // namespace

void Screen::header_strip(Graphics& g, const RectF& full, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    if (full.w <= 0 || full.h <= 0) return;
    RectF strip = full;
    // BOOST maketi (Ayarlar): baslik seridi sayfa genisliginde ("08 // Ayarlar" solda, sari mono surum sagda); liste ortali.
    constexpr float kUnbounded = 10000.f;
    const auto title = t(page_title(s.page));
    // BOOST maketi #page-code-tag ("02 //", 11px mono 600, vurgu): basligin solunda, gap-3 (12). Yalniz maketle birebir
    // yapilmis sayfalarda (Oyunlar "02 //", Oyun hazirligi "03 //", Olcumler "04 //"); digerleri kendi gecisinde eklenir.
    float lead_x = 0;
    if (s.page == Page::Games || s.page == Page::Readiness || s.page == Page::Measurements || s.page == Page::History || s.page == Page::Settings || s.page == Page::Updates || systools_page(s.page)) {
        const std::wstring_view kCode = s.page == Page::Updates ? L"05 //" : s.page == Page::Games ? L"02 //" : s.page == Page::Readiness ? L"03 //" : s.page == Page::Measurements ? L"04 //" : s.page == Page::History ? L"07 //" : s.page == Page::Settings ? L"08 //" : L"06 //";
        constexpr float kCodeGap = 12.f;
        const float cw = std::ceil(g.measure(Role::Mono11Semi, kCode, kUnbounded, false).w);
        const auto& p = pal();
        g.text(Role::Mono11Semi, kCode, {strip.x, strip.y, cw + 1.f, strip.h}, pal_ == &theme_.dark ? p.accent : p.text_primary,
               {Align::Leading, false, true});
        lead_x = std::min(cw + kCodeGap, strip.w);
    }
    const float tw = std::ceil(g.measure(Role::PageTitle, title, kUnbounded, false).w);
    g.text(Role::PageTitle, title, {strip.x + lead_x, strip.y, std::max(strip.w - lead_x, 0.f), strip.h}, pal().text_primary,
           {Align::Leading, false, true});
    HeaderBar bar = HeaderBar::make(strip, lead_x + tw, theme_.space(1));
    page_header(g, bar, s, vm, now, hits);
}

void Screen::page_header(Graphics& g, HeaderBar& bar, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    switch (s.page) {
        // Spec 2026-11-12 B4: sekme yok; yalniz zorunlu "Temizlik geri alinamaz" cipi.
        case Page::Cleanup:
        case Page::Startup:
        case Page::System: break;  // BOOST maketi: "Temizlik geri alinamaz" rozeti "Bolumler:" seridinde
        case Page::Games: games_header(g, bar, s, vm, now, hits); break;
        case Page::Measurements: measure_header(g, bar, s, vm, now, hits); break;
        case Page::History: history_header(g, bar, s, vm, now, hits); break;
        case Page::Settings: settings_header(g, bar, vm, hits); break;
        default: break;
    }
}

void Screen::expander(Graphics& g, const RectF& r, Hit id, bool open, const std::wstring& name, UiState& s, std::int64_t now, HitMap& hits,
                      bool visible) {
    // Ac/kapa dugmesi: ok isareti (acik ▴ / kapali ▾), hover'da yuzey tonu. Durum yalniz isarete dayanmaz: UIA'da
    // Selected = acik ve ad eylemi soyler. Gorunmeyen satirda cizilmeden kaydedilir (Tab ile gelince kaydirilir).
    if (r.w <= 0 || id == Hit::None) return;
    if (visible) {
        const auto& p = pal();
        const float hv = hover_value(s, id, now);
        const float pv = press_value(s, id, now);
        g.fill_round(r, theme_.radius_control, mix(p.surface, p.surface_raised, std::min(1.f, 0.5f + hv * 0.5f)));
        g.stroke_round(r, theme_.radius_control, mix(p.border, p.accent, std::min(1.f, hv + pv)));
        g.text(Role::Body, open ? L"▴" : L"▾", r, mix(p.text_secondary, p.accent, std::min(1.f, hv + pv)), {Align::Center, false, true});
        if (s.focus_visible && s.focus == id) focus_ring(g, r, theme_.radius_control, hits);
    }
    hits.add(id, r, name, AccRole::Expander, open);
}

float Screen::filter_chip_width(Graphics& g, std::wstring_view label, std::wstring_view count) const {
    constexpr float kUnbounded = 10000.f;
    const float lw = std::ceil(g.measure(Role::Body, label, kUnbounded, false).w);
    const float cw = count.empty() ? 0.f : theme_.space(0) + 2.f + std::ceil(g.measure(Role::Secondary, count, kUnbounded, false, true).w);
    return lw + cw + 2 * theme_.space(2);
}

void Screen::filter_chip(Graphics& g, const RectF& r, Hit id, std::wstring_view label, std::wstring_view count, bool selected,
                         const std::wstring& name, UiState& s, std::int64_t now, HitMap& hits) {
    // Suzgec cipi: hap bicimli radyo dugmesi. Secili: accent tonlu zemin + accent cerceve (renk + cerceve; UIA'da
    // Selected). Sayi etiketin saginda, daha sakin tonda. r bos (w == 0) ise oge sigmamistir: cizilmez.
    if (r.w <= 0) return;
    constexpr float kUnbounded = 10000.f;
    const auto& p = pal();
    const float rad = r.h / 2;
    const float hv = hover_value(s, id, now);
    const Color bg = selected ? mix(p.surface, p.accent, alpha_scale::kToggleOnTrack) : mix(p.surface, p.surface_raised, hv);
    g.fill_round(r, rad, bg);
    g.stroke_round(r, rad, selected ? p.accent : mix(p.border, p.accent, hv));
    const float lw = std::ceil(g.measure(Role::Body, label, kUnbounded, false).w);
    const float x = r.x + theme_.space(2);
    g.text(Role::Body, label, {x, r.y, lw + 1.f, r.h}, selected ? p.text_primary : mix(p.text_secondary, p.text_primary, hv),
           {Align::Leading, false, true});
    if (!count.empty()) {
        const float cx = x + lw + theme_.space(0) + 2.f;
        g.text(Role::Secondary, count, {cx, r.y, std::max(r.right() - theme_.space(2) - cx, 1.f) + 1.f, r.h},
               selected ? ensure_contrast(p.accent, bg, p.text_primary, 4.5) : p.text_secondary, {Align::Leading, false, true, true});
    }
    if (s.focus_visible && s.focus == id) focus_ring(g, r, rad, hits);
    hits.add(id, r, name, AccRole::RadioButton, selected);
}

void Screen::welcome_card(Graphics& g, Stack& st, const RectF& body, UiState& s, std::int64_t now, HitMap& hits) {
    const auto& p = pal();
    const float pad = theme_.card_pad;
    const float inner_w = body.w - 2 * pad;
    const auto title = t(StringId::WelcomeTitle);
    const std::array<std::wstring, 3> lines{t(StringId::WelcomeP1), t(StringId::WelcomeP2), t(StringId::WelcomeP3)};
    const float logo_h = 32;  // DS-13: karsilama logosu bilesen yuksekligi (token degil)
    const float th = g.measure(Role::StateTitle, title, inner_w).h;
    std::array<float, 3> lh{};
    float lines_h = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        lh[i] = std::max(g.measure(Role::Body, lines[i], inner_w - 16).h, theme_.role(Role::Body).line);
        lines_h += lh[i] + theme_.space(1);
    }
    const RectF card = st.next(pad + logo_h + theme_.space(3) + th + theme_.space(2) + lines_h + theme_.space(2) + theme_.button_h + pad);
    g.fill_round(card, theme_.radius_card, p.surface);
    g.stroke_round(card, theme_.radius_card, p.accent.with_alpha(alpha_scale::kSoftOutline), 1.5f);
    float y = card.y + pad;
    {
        using ef::ui::brand::Logo;
        const bool dark = pal_ == &theme_.dark;  // UI-14: HC paletinde tek renk beyaz kilit kullanilir
        const auto& doc = ef::ui::brand::logo(dark ? Logo::LockupDark : Logo::LockupWhite);
        g.svg(doc, {card.x + pad, y, std::min(inner_w, 240.f), logo_h}, dark ? std::nullopt : std::optional<Color>(p.text_primary),
              p.accent);
    }
    y += logo_h + theme_.space(3);
    g.text(Role::StateTitle, title, {card.x + pad, y, inner_w, th}, p.text_primary);
    y += th + theme_.space(2);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        g.fill_circle(card.x + pad + 4, y + theme_.role(Role::Body).line / 2, 3, p.accent);
        g.text(Role::Body, lines[i], {card.x + pad + 16, y, inner_w - 16, lh[i]}, p.text_secondary);
        y += lh[i] + theme_.space(1);
    }
    y += theme_.space(2) - theme_.space(1);
    button(g, card.x + pad, y, Hit::WelcomeStart, t(StringId::WelcomeStart), true, s, now, hits);
}

float Screen::empty_page(Graphics& g, const RectF& body, StringId text) {
    const auto& p = pal();
    const float pad = theme_.card_pad;
    const auto msg = t(text);
    const float h = g.measure(Role::Body, msg, body.w - 2 * pad).h;
    const RectF card{body.x, body.y, body.w, h + 2 * pad};
    draw_card(g, card);
    g.text(Role::Body, msg, card.inset(pad), p.text_secondary);
    return card.h;
}

SysCell sys_cell_split(std::string_view text) {
    SysCell c;
    constexpr std::string_view sep = " · ";
    bool first = true;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        const std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (line.empty()) continue;
        if (first) {
            first = false;
            const auto at = line.find(sep);
            c.main = std::string(line.substr(0, at));
            if (at != std::string_view::npos) c.detail = std::string(line.substr(at + sep.size()));
        } else {
            if (!c.detail.empty()) c.detail += sep;
            c.detail += line;
        }
    }
    return c;
}

SysCell sys_os_split(std::string_view os) {
    SysCell c;
    const auto open = os.rfind(" (");
    if (open != std::string_view::npos && !os.empty() && os.back() == ')' && open > 0) {
        c.main = std::string(os.substr(0, open));
        c.detail = std::string(os.substr(open + 2, os.size() - open - 3));
    } else {
        c.main = std::string(os);
    }
    return c;
}

namespace {
std::wstring replace_all_w(std::wstring s, std::wstring_view key, const std::wstring& val) {
    for (std::size_t pos = s.find(key); pos != std::wstring::npos; pos = s.find(key, pos + val.size())) s.replace(pos, key.size(), val);
    return s;
}
}  // namespace

void Screen::readiness_summary(Graphics& g, CardSlot& slot, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, float room) {
    // Panel: "Oyun hazirligi" karti (sahip onayli maket 2026-11-16; olculer HTML kaynagindan, Tailwind px = DIP). Baslik
    // satiri: teknik dizin "07 //", baslik, sayim cipi ("6 iyi · 1 oneri"; gercek sayim) ve "Duzelt" (Hit::OpenReadiness:
    // Oyun hazirligi sayfasina gecer). Altinda yuva: buyuk skor "iyi/toplam", kontrol basina bir parcali cubuk (iyi vurgu,
    // oneri uyari, olculmedi notr; ilk gosterimde dolar), altinda dogrulanan alanlar ve sagda "N iyilestirme hazir"
    // (eylemi olan oneri sayisi). Sonra en fazla iki bulgu satiri: once en onemli oneriler (eylem dugmesi: Oyun hazirligi
    // sayfasindaki ayni kimlik ChkAction k ve ayni islem), yer kalirsa iyi bulgular ("Dogrulandi ✓"). Sayilar yalniz
    // gercek tarama bulgularindan (vm.readiness.checks): iyi = Good, oneri = Advice + Problem, olculmedi = Unknown.
    namespace cr = core::readiness;
    const auto& p = pal();
    const auto& rv = vm.readiness;
    const bool dark = pal_ == &theme_.dark;
    constexpr float kHeadGap = 10.f, kBarH = 8.f, kSegGap = 2.f, kRowGap = 6.f, kDot = 8.f;
    constexpr float kUnbounded = 10000.f;
    const auto one_w = [&](Role role, std::wstring_view text) { return text.empty() ? 0.f : std::ceil(g.measure(role, text, kUnbounded, false).w) + 1.f; };
    const auto sc = readiness_score(rv);  // Oyun hazirligi sayfasindaki skorla ayni sayim
    const int good = sc.good, advice = sc.advice, unmeasured = sc.unmeasured, total = sc.total;
    const bool pending = rv.checks.empty();
    const bool scored = !pending && total > 0;
    std::wstring counts;
    if (scored) {
        counts = t(unmeasured > 0 ? StringId::OverviewReadinessCounts : StringId::PanelReadinessCounts);
        const auto put = [&](std::wstring_view key, int n) {
            if (const auto pos = counts.find(key); pos != std::wstring::npos) counts.replace(pos, key.size(), std::to_wstring(n));
        };
        put(L"{good}", good);
        put(L"{advice}", advice);
        put(L"{unmeasured}", unmeasured);
    }
    // Eylem kurali Oyun hazirligi sayfasiyla ayni: ag testi yalniz kullanilabilirken; duzeltmesi kayitli bulguda ikinci
    // "Duzelt" yok. Is surerken dugme devre disi (kaybolmaz).
    const auto undo_of = [&](std::size_t k) { return k < rv.undo.size() ? rv.undo[k] : cr::UndoState::None; };
    // Satir yuksekligi (0.9.2 yerlesimi): eylemi olan (yonlendirme dahil) bulgu satiri dugme yuksekligini korur; dugme kalksa da Panel
    // olculeri kaymaz.
    const auto row_slot = [&](std::size_t k) {
        const auto& c = rv.checks[k];
        return k < static_cast<std::size_t>(kMaxReadinessRows) && c.action != cr::ActionKind::None &&
               ((c.action != cr::ActionKind::RunNetworkTest && c.action != cr::ActionKind::RunDnsBench) || rv.net_available) && !(undo_of(k) != cr::UndoState::None && c.action == cr::ActionKind::AutoFix);
    };
    const auto action_of = [&](std::size_t k) {
        const auto& c = rv.checks[k];
        // P12-B1: satir dugmesi yalniz gercekten bir sey yapan eylemde (Oyun hazirligi sayfasiyla ayni kural); yonlendirme dugmesi yok.
        return cr::row_action_shown(c) && row_slot(k);
    };
    const bool enabled = !(rv.loading || rv.net_running || rv.mouse_running || rv.dns_running || rv.health_running || rv.fix_running);
    int fixable = 0;
    std::wstring verified;  // dogrulanan (iyi) alanlarin adlari, tekrarsiz
    {
        std::vector<std::wstring> names;
        for (std::size_t k = 0; k < rv.checks.size(); ++k) {
            const auto l = rv.checks[k].level;
            if (l == cr::Level::Good) {
                auto n = check_name(rv.checks[k]);
                if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(std::move(n));
            }
        }
        std::wstring list;
        for (const auto& n : names) list += (list.empty() ? L"" : L", ") + n;
        if (!list.empty()) verified = replace_all_w(t(StringId::PanelReadinessVerifiedList), L"{list}", list);
    }
    // P12-B1: "N iyilestirme hazir" ve baslik "Duzelt" ayni kurala bagli: uygulamanin kendisinin yazip geri okuyabildigi bulgular
    // (core/readiness/fix_plan). Yalniz yonlendiren bulgu "hazir iyilestirme" sayilmaz.
    fixable = static_cast<int>(cr::plan_fix_all(rv.checks, rv.undo).items.size());
    const std::wstring fix_txt = fixable > 0 ? replace_all_w(t(StringId::PanelReadinessFixable), L"{n}", std::to_wstring(fixable)) : std::wstring();
    // Bulgu satirlari: once oneriler (gorunen sira), sonra iyi bulgular; en fazla iki.
    std::vector<std::size_t> top = readiness_top_advice(rv, 2);
    if (top.size() < 2)
        for (const std::size_t k : readiness_display_order(rv, rv.checks.size()))
            if (top.size() < 2 && rv.checks[k].level == cr::Level::Good) top.push_back(k);

    // Maket (2026-11-16 birebir): kart p-3.5 + kenar (15); baslik satiri 28 (items-center): "07 //" (11px/600 mono, vurgu),
    // baslik 14px/600, sayim rozeti (11px mono vurgu, vurgu %15 zemin, px 8, yaricap 4), sagda .btn-secondary "Duzelt";
    // mb 10. Yuva (p-2.5): skor 20px/700, gap 12, sutun (gap 4): h 8 tam yuvarlak cubuk (#171D2A zemin, kontrol basina
    // flex-1 parca, aralarinda 2) ve altinda dogrulanan alanlar (11px, ikincil) | "N iyilestirme hazir" (11px, uyari).
    // Bulgu satirlari (mt 10, space-y-1.5): yuva px-3 py-2, nokta 8, ad 12px/500, aciklama 12px ikincil; sagda rozet
    // dugme (.quick-fix-btn, ton uyari / hata) ya da "Dogrulandi ✓" (11px mono vurgu).
    constexpr float kIn = 15.f, kWellIn = 11.f, kRowIn = 13.f, kRowPadY = 9.f, kScoreGap = 12.f, kRowsTop = 10.f;
    const auto open_label = t(StringId::PanelFix);
    const RectF btn = panel_button_rect(g, open_label, false);
    const auto title = t(StringId::OverviewReadinessTitle);
    const std::wstring idx = L"07 //";  // teknik dizin (cevrilmez)
    const float in_w2 = std::max(slot.area.w - 2 * kIn, 1.f);
    const float idx_w = one_w(Role::Mono11Semi, idx);
    const float head_w = std::max(in_w2 - btn.w - theme_.space(1), 1.f);
    const float title_w = std::min(one_w(Role::Title14, title), std::max(head_w - idx_w - theme_.space(1), 1.f));
    const float head_h = std::max(theme_.role(Role::Title14).line, btn.h);
    const float small_h = theme_.role(Role::Small11).line, line_h = theme_.role(Role::Small12).line;
    const float score_h = theme_.role(Role::Num20).line;
    const std::wstring score = std::to_wstring(good) + L"/" + std::to_wstring(total);
    const float score_w = scored ? std::ceil(g.measure(Role::Num20, score, kUnbounded, false, true).w) + 1.f : 0.f;
    const float col_h = kBarH + theme_.space(0) + small_h;
    const float well_h = 2 * kWellIn + std::max(score_h, col_h);
    const float badge_h = theme_.touch_min;
    const auto row_h_of = [&](std::size_t k) {
        const auto& c = rv.checks[k];
        const bool button_row = c.level != cr::Level::Good && row_slot(k);  // yonlendirme dugmesi kalksa da yukseklik ayni (0.9.2)
        return 2 * kRowPadY + std::max(line_h, button_row ? badge_h : 0.f);
    };
    const auto rows_h = [&](std::size_t n) {
        float h = n ? kRowsTop : 0.f;
        for (std::size_t i = 0; i < n; ++i) h += row_h_of(top[i]) + (i ? kRowGap : 0.f);
        return h;
    };
    const float base_h = kIn + head_h + kHeadGap + (scored ? well_h : line_h) + kIn;
    // Bulgu satirlari dogal boya, Panel pencereye kaydirmasiz sigdigi kadar girer (buyuk metin olceginde / kisa
    // pencerede azalir); yandaki kart satiri uzatmissa artan alana da sigdigi kadar yazilir.
    std::size_t nrows = scored ? top.size() : 0;
    while (nrows > 0 && base_h + rows_h(nrows) > room + 0.5f) --nrows;
    RectF card;
    if (!slot.place(base_h + rows_h(nrows), card)) return;
    if (scored)
        while (nrows < top.size() && base_h + rows_h(nrows + 1) <= card.h + 0.5f) ++nrows;
    draw_card(g, card, false, depth::kPanelCardRadius);
    const float x = card.x + kIn;
    float y = card.y + kIn;
    const auto mid = [&](Role role) { return y + (head_h - theme_.role(role).line) / 2; };
    float hx = x;
    g.text(Role::Mono11Semi, idx, {hx, mid(Role::Mono11Semi), idx_w, theme_.role(Role::Mono11Semi).line}, dark ? p.accent : p.text_secondary,
           {Align::Leading, false});
    hx += idx_w + theme_.space(1) - 1.f;
    g.text(Role::Title14, fit_text(g, Role::Title14, title, title_w), {hx, mid(Role::Title14), title_w, theme_.role(Role::Title14).line}, p.text_primary,
           {Align::Leading, false});
    hx += title_w + theme_.space(1) - 1.f;
    if (!counts.empty()) {
        const float cw = one_w(Role::Mono11, counts) + 2 * theme_.space(1);
        const float ch = theme_.role(Role::Mono11).line + 4.f;
        if (cw <= x + head_w - hx) {
            const RectF chip_r{hx, y + (head_h - ch) / 2, cw, ch};
            if (dark) g.fill_round(chip_r, 4.f, p.accent.with_alpha(0.15f));
            else g.stroke_round(chip_r, 4.f, p.border);
            g.text(Role::Mono11, counts, {chip_r.x + theme_.space(1), chip_r.y + 2.f, cw - 2 * theme_.space(1), ch - 4.f}, dark ? p.accent : p.text_primary,
                   {Align::Leading, false});
        }
    }
    // Baslik "Duzelt" (P12-B1): sayfa degistirmez, tek Evet/Hayir'da tum otomatik duzeltmeleri uygular. Duzeltilecek bir sey
    // (veya is bitene kadar) yoksa dugme yerinde devre disi cizilir; yerlesim kaymaz.
    panel_button(g, card.right() - kIn - btn.w, y + (head_h - btn.h) / 2, Hit::OpenReadiness, open_label, false, s, now, hits, enabled && fixable > 0);
    hits.meta[Hit::OpenReadiness].help = a11y_join(a11y_join(fix_txt, counts), pending ? t(StringId::OverviewReadinessPending) : std::wstring());
    y += head_h + kHeadGap;
    if (!scored) {
        s.readiness_fill_seen = false;
        g.text(Role::Small12, t(StringId::PanelReadinessEmpty), {x, y, in_w2, line_h}, p.text_secondary, {Align::Leading, false, true});
        return;
    }
    // Yuva: skor + parcali cubuk (ilk gosterimde 0'dan dolar; hareket kapali / oyun acik / olcum surerken aninda).
    if (!s.readiness_fill_seen) {
        const int ms = effective_ms(theme_.motion.value_ease, s.reduced_motion || s.motion_paused || vm.game_gate_blocked);
        s.readiness_fill = Tween(ms > 0 ? 0.f : 1.f);
        s.readiness_fill.retarget(1.f, now, ms);
        s.readiness_fill_seen = true;
    }
    const float grow = std::clamp(s.readiness_fill.value(now, theme_.easing), 0.f, 1.f);
    draw_well(g, {x, y, in_w2, well_h});
    {
        const float wx = x + kWellIn, wy = y + kWellIn, ww = std::max(in_w2 - 2 * kWellIn, 1.f), inner_h = well_h - 2 * kWellIn;
        g.text(Role::Num20, score, {wx, wy + (inner_h - score_h) / 2, score_w, score_h}, p.text_primary, {Align::Leading, false, false, true});
        const float bx = wx + score_w - 1.f + kScoreGap, bw = std::max(wx + ww - bx, 1.f);
        const float cy0 = wy + (inner_h - col_h) / 2;
        const float seg = std::max((bw - kSegGap * static_cast<float>(total - 1)) / static_cast<float>(total), 1.f);
        const Color track = dark ? depth::rgba(0x17, 0x1D, 0x2A, 1.f) : p.background;
        const int lit = static_cast<int>(std::floor(grow * static_cast<float>(total) + 0.5f));
        constexpr float kR = kBarH / 2;
        if (dark) g.fill_round({bx, cy0, bw, kBarH}, kR, track);  // rounded-full overflow-hidden kap
        for (int i = 0; i < total; ++i) {
            const RectF r{bx + static_cast<float>(i) * (seg + kSegGap), cy0, seg, kBarH};
            const bool on = i < lit && i < good + advice;
            const Color c = i >= lit ? track : i < good ? p.accent : i < good + advice ? p.warning : mix(p.surface, p.border, alpha_scale::kDivider);
            if (!dark) {
                if (on) g.fill_round(r, 2.f, c);
                else g.stroke_round(r.inset(0.5f), 2.f, p.border);
                continue;
            }
            // Ilk / son parcanin dis ucu kabin yuvarlak kosesine uyar (kirpilmis yari daire + duz govde).
            const bool first = i == 0, last = i == total - 1;
            RectF body = r;
            if (first && body.w > kR) {
                g.push_clip({r.x, r.y, kR, r.h});
                g.fill_round({r.x, r.y, kR * 2, r.h}, kR, c);
                g.pop_clip();
                body.x += kR, body.w -= kR;
            }
            if (last && body.w > kR) {
                g.push_clip({r.right() - kR, r.y, kR, r.h});
                g.fill_round({r.right() - kR * 2, r.y, kR * 2, r.h}, kR, c);
                g.pop_clip();
                body.w -= kR;
            }
            if (body.w > 0) g.fill(body, c);
        }
        const float ly = cy0 + kBarH + theme_.space(0);
        const float fw = one_w(Role::Small11, fix_txt);
        if (fw > 0) g.text(Role::Small11, fix_txt, {bx + bw - fw, ly, fw, small_h}, dark ? p.warning : p.text_primary, {Align::Trailing, false});
        // Toplu duzeltmenin tek cumlelik sonucu ("N ayar duzeltildi") dogrulanan alanlar satirinin yerinde gorunur (yerlesim ayni).
        const bool fix_note = rv.notice_check_id == core::readiness::kFixAllNoticeId && !rv.notice_key.empty();
        std::wstring left = verified;
        if (fix_note) {
            left = tk(rv.notice_key);
            if (left.empty()) left = widen(rv.notice_key);
            left = readiness_notice_fill(std::move(left), rv);
        }
        if (const float vw = bw - fw - (fw > 0 ? theme_.space(2) : 0.f); !left.empty() && vw >= 48.f)
            g.text(Role::Small11, fit_text(g, Role::Small11, left, vw), {bx, ly, vw, small_h},
                   fix_note && rv.notice_warning ? p.warning : p.text_secondary, {Align::Leading, false});
    }
    y += well_h + kRowsTop;
    const auto verified_label = t(StringId::PanelReadinessVerified);
    for (std::size_t i = 0; i < nrows; ++i) {
        const std::size_t k = top[i];
        const auto& c = rv.checks[k];
        const bool is_good = c.level == cr::Level::Good;
        const float row_h = row_h_of(k);
        draw_well(g, {x, y, in_w2, row_h});
        const float rx = x + kRowIn, rw = std::max(in_w2 - 2 * kRowIn, 1.f);
        const Color tone = is_good ? p.accent : c.level == cr::Level::Problem ? p.error : p.warning;
        g.fill_circle(rx + kDot / 2, y + row_h / 2, kDot / 2, tone);
        float right_w = 0;
        if (is_good) {
            right_w = one_w(Role::Mono11, verified_label);
            g.text(Role::Mono11, verified_label, {rx + rw - right_w, y, right_w, row_h}, dark ? p.accent : p.text_primary, {Align::Trailing, false, true});
        } else if (action_of(k)) {
            const auto label = t(readiness_action_label(c));
            const Hit id = hit_of(HitKind::ChkAction, k);
            right_w = badge_button_w(g, label);
            badge_button(g, {rx + rw - right_w, y + (row_h - badge_h) / 2, right_w, badge_h}, id, label, tone, enabled, s, now, hits);
            hits.meta[id].name = label + L": " + check_name(c);
            hits.meta[id].help = check_text(c);
            s.panel_readiness_actions.push_back(k);
        }
        const float tx = rx + kDot + theme_.space(1);
        const float avail = std::max(rx + rw - (right_w > 0 ? right_w + theme_.space(1) : 0.f) - tx, 1.f);
        const std::wstring name = check_name(c);
        const float nw = std::min(one_w(Role::Medium12, name), avail);
        g.text(Role::Medium12, fit_text(g, Role::Medium12, name, nw), {tx, y, nw, row_h}, p.text_primary, {Align::Leading, false, true});
        if (const float dw = avail - nw - theme_.space(1) + 1.f; dw >= 48.f)
            g.text(Role::Small12, fit_text(g, Role::Small12, check_text(c), dw), {tx + nw - 1.f + theme_.space(1), y, dw, row_h}, p.text_secondary,
                   {Align::Leading, false, true});
        y += row_h + kRowGap;
    }
}

std::wstring Screen::fit_text(Graphics& g, Role role, const std::wstring& text, float w) const {
    constexpr float kUnbounded = 10000.f;
    constexpr std::size_t kFitCacheMax = 256;
    const FitKey key{role, w, text};
    if (const auto it = fit_cache_.find(key); it != fit_cache_.end()) return it->second;
    if (fit_cache_.size() >= kFitCacheMax) fit_cache_.clear();
    if (g.measure(role, text, kUnbounded, false).w <= w) return fit_cache_[key] = text;
    std::size_t lo = 0, hi = text.size();
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        if (g.measure(role, text.substr(0, mid) + L"…", kUnbounded, false).w <= w) lo = mid;
        else hi = mid - 1;
    }
    if (lo > 0 && text[lo - 1] >= 0xD800 && text[lo - 1] <= 0xDBFF) --lo;  // vekil ciftini bolme
    while (lo > 0 && text[lo - 1] == L' ') --lo;
    return fit_cache_[key] = text.substr(0, lo) + L"…";
}

float Screen::overview(Graphics& g, const RectF& body, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    // Panel (spec 2026-11-12 B), yukaridan asagi: [karsilama / oyun engeli] -> Performans bolumu (yuvarlak dugme; acilan
    // icerik altinda) -> bes canli kutucuk -> esit satir ["Darbogaz" | "Oyun hazirligi"]. 1180x720'de kaydirmasiz tek
    // ekrandir. Sistem karti ve alt kartlar tasindi (B.4): Sistem + Depolama -> Sistem araclari "Sistem" alt sayfasi,
    // Once/Sonra -> Olcumler, bulgular + tek tik kontrol -> Oyun hazirligi.
    Stack full{body, theme_.card_gap};
    StatusSeq seq{0};  // salt okunur UIA metinleri: bolum sayilari (0..2), kutucuklar (3..7), darbogaz satirlari (8..)

    s.show_welcome = vm.show_welcome;
    s.panel_readiness_actions.clear();  // "Oyun hazirligi" kartinin bulgu dugmeleri (cizimde yeniden kaydedilir)
    if (vm.show_welcome) welcome_card(g, full, body, s, now, hits);
    game_banner(g, full, body, vm, s, now, hits);
    s.measure = vm.measure;  // Tab sirasi: "Testi baslat" etkinligi
    // 1: Performans bolumu (tam genislik, kartsiz).
    {
        const RectF a{body.x, body.y + full.cursor, body.w, 0};
        full.cursor += perf_card(g, a, s, vm, now, hits, seq) + full.gap;
    }
    // 2: bes canli kutucuk.
    live_tiles(g, full, body, s, vm, now, hits, seq);
    // 3: "Darbogaz" + "Oyun hazirligi": esit boylu satir (dar pencerede alt alta, dogal boy).
    CardFlow flow;
    flow.area = {body.x, body.y, body.w, 0};
    flow.gap = theme_.card_gap;
    flow.log = &hits.rows;
    flow.cursor = full.cursor;
    flow.stacked = content_columns(flow.area, theme_, 320.f).stacked;
    const float room = body.h - full.cursor;  // pencerede bu satira kalan yukseklik (1180x720'de kaydirmasiz)
    flow.row({{[&](CardSlot& c) { limit_card(g, c, s, vm, now, hits, seq, room); }}, {[&](CardSlot& c) { readiness_summary(g, c, s, vm, now, hits, room); }}});
    return flow.used();
}

HitMap Screen::paint(Graphics& g, float w, float h, UiState& s, const ViewModel& vm, std::int64_t now) {
    HitMap hits = paint_shell(g, w, h, s, vm, now);
    // Spec 2026-11-12 E: kip penceresi her kabuk durumunun (sayfa, canli test gorunumu, guncelleme karti) ustune cizilir.
    if (s.modal) modal_layer(g, w, h, s, now, hits);
    return hits;
}

HitMap Screen::paint_shell(Graphics& g, float w, float h, UiState& s, const ViewModel& vm, std::int64_t now) {
    HitMap hits;
    const auto& p = pal();
    card_log_.clear();
    shadow_log_.clear();
    s.shadow_casters.clear();
    s.tiles_shadow_begin = s.tiles_shadow_end = 0;
    backdrop_w_ = w;
    backdrop_h_ = h;
    if (update_gate_shown(vm)) {  // zorunlu guncelleme: tum pencere kart (gezinme yok)
        s.power_region = {};
        s.tiles_region = {};
        s.pulse_regions.clear();
        s.games_strip = {};
        s.games_reveal_strip = false;
        s.game_blocked = vm.game_gate_blocked;
        s.detection_retry = false;
        s.reveal_focus = false;
        s.content_height = 0;
        update_gate(g, w, h, s, vm, now, hits);
        hits.cards = std::move(card_log_);
        card_log_.clear();
        shadow_log_.clear();
        return hits;
    }
    s.update_gate = s.update_not_writable = s.update_busy = s.update_restore = s.update_repair = false;
    auto l = compute_shell(w, h, theme_, topbar_metrics(g));
    g.clear(p.background);
    paint_backdrop(g, {0, 0, w, h});  // pencere zemini (maket #main-window-shell: duz var(--bg))
    topbar(g, l, s, vm, now, hits);
    if (const float bh = update_banner(g, {0, l.content.y, w, 0}, l.header, vm, hits); bh > 0) {
        // Serit ust menunun hemen altinda; sayfa basligi ve govde asagi kayar.
        const float dy = std::min(bh, l.content.h);
        l.content.y += dy;
        l.content.h = std::max(l.content.h - dy, 0.f);
        l.header.y += dy;
        l.body.y += dy;
        l.body.h = std::max(l.body.h - dy, 0.f);
    }
    // ADR-033 spec 8: "Kurulum gerekli" seridi (guncelleme seridinin altinda; ayni kaydirma kurali).
    if (const float nh = install_offer_notice(g, {0, l.content.y, w, 0}, l.header, s, vm, now, hits); nh > 0) {
        const float dy = std::min(nh, l.content.h);
        l.content.y += dy;
        l.content.h = std::max(l.content.h - dy, 0.f);
        l.header.y += dy;
        l.body.y += dy;
        l.body.h = std::max(l.body.h - dy, 0.f);
    }
    // Spec G: ilk acilis bildirimi (guncelleme seridinin altinda; ayni kaydirma kurali).
    if (const float nh = autostart_notice(g, {0, l.content.y, w, 0}, l.header, s, vm, now, hits); nh > 0) {
        const float dy = std::min(nh, l.content.h);
        l.content.y += dy;
        l.content.h = std::max(l.content.h - dy, 0.f);
        l.header.y += dy;
        l.body.y += dy;
        l.body.h = std::max(l.body.h - dy, 0.f);
    }
    s.game_blocked = vm.game_gate_blocked;
    s.detection_retry = vm.game_gate_blocked && vm.game_block_can_retry;
    // Panel hizli yol bolgeleri her tam cizimde yeniden yazilir (Panel disinda / kirpilmis bolgede bos kalir).
    s.power_region = {};
    s.power_card = {};
    s.power_glow = false;
    s.power_conduits = false;
    s.tiles_region = {};
    // Oyunlar: nabiz noktalari ve secili oyun seridi de (Oyunlar disinda bos kalir).
    s.pulse_regions.clear();
    s.games_strip = {};

    // Canli test gorunumu (spec 2026-11-12 A4): denge testi canli ekranla surerken Olcumler sayfasinin icerik alani
    // (baslik seridi + govde) onizleme ve gostergelere birakilir; ust menu kalir. Bolge UiState'e yazilir ki pencere
    // yeni onizleme karesinde yalniz bu bolgeyi yeniden cizebilsin (paint_live_region).
    s.live_view = s.page == Page::Measurements && core::measure::live_view_shown(vm.measure);
    s.live_region = {};
    if (s.live_view) {
        s.live_region = {0, l.content.y, w, l.content.h};
        s.scroll = 0;
        s.content_height = 0;
        s.reveal_focus = false;
        s.rows_drawn = 0;
        paint_live_region(g, s, vm, now, hits);
        hits.cards = std::move(card_log_);
        card_log_.clear();
        shadow_log_.clear();
        return hits;
    }

    const float fade = s.page_fade.value(now, theme_.easing);
    g.set_alpha(fade);
    if (s.page == Page::Overview) {
        // BOOST maketi: yalniz Panel'de sayfa basligi seridi yok (switchPage: #header-strip yalniz 'overview'de gizli;
        // Guncellemeler "05 // Guncellemeler" basligini tasir); govde ust menunun page_margin altindan baslar (main p-4).
        l.body = overview_body(l, theme_);
    } else {
        header_strip(g, l.header, s, vm, now, hits);
    }
    // Ust menu ve baslik seridi ogeleri kaydirilmaz; bu sayidan sonrakiler govde (sayfa) ogeleridir.
    const std::size_t shell_items = hits.items.size();

    const RectF body = l.body;  // compute_shell: en fazla content_max_w, pencerede ortali
    // Govde kaydirilabilir: icerik pencereden uzunsa kesilmez, kaydirilir.
    const float max_scroll = std::max(0.f, s.content_height - l.body.h);
    s.scroll = std::clamp(s.scroll, 0.f, max_scroll);
    g.push_clip({l.content.x, l.body.y - 4, l.content.w, h - l.body.y + 4});
    s.view_top = l.body.y - 4 + s.scroll;
    s.view_bottom = h + s.scroll;
    s.rows_drawn = 0;
    g.set_offset(0, -s.scroll);
    HitMap page_hits;
    const std::size_t shell_cards = card_log_.size();  // bundan sonrakiler govde (sayfa) kartlaridir
    const std::size_t shell_shadows = shadow_log_.size();
    float used = 0;
    switch (s.page) {
        case Page::Overview: used = overview(g, body, s, vm, now, page_hits); break;
        case Page::Readiness: used = readiness_page(g, body, s, vm, now, page_hits); break;
        case Page::Games: used = games_page(g, body, s, vm, now, page_hits); break;
        case Page::Measurements: used = measure_page(g, body, s, vm, now, page_hits); break;
        // Sistem araclari (spec 2026-11-12 B4): sekmesiz tek sayfa; eski alt sayfa kimlikleri ayni sayfayi cizer.
        case Page::Cleanup:
        case Page::Startup:
        case Page::System: used = system_tools_page(g, body, s, vm, now, page_hits); break;
        case Page::History: used = history_page(g, body, s, vm, now, page_hits); break;
        case Page::Settings: used = settings_page(g, body, s, vm, now, page_hits); break;
        case Page::Updates: used = updates_page(g, body, s, vm, now, page_hits); break;
        case Page::Count_: break;
    }
    g.set_offset(0, 0);
    g.pop_clip();
    g.set_alpha(1);
    s.content_height = used;
    // Kaydirma gostergesi: icerik tasiyorsa ince cubuk.
    if (used > l.body.h && l.body.h > 0) {
        const float track_h = l.body.h;
        const float thumb_h = std::max(24.f, track_h * l.body.h / used);
        const float thumb_y = l.body.y + (track_h - thumb_h) * (s.scroll / std::max(1.f, used - l.body.h));
        // DS-10: ray zemininde (background/surface) kenarlik token'i >= 3:1; kaydirma cubugu durumu
        // yalniz renkle degil konumla da okunur (birim test: tum temalar).
        g.fill_round({l.content.right() - theme_.scrollbar_inset, thumb_y, theme_.scrollbar_thumb_w, thumb_h},
                     theme_.scrollbar_thumb_w / 2, p.border);
    }
    // Sayfa ogeleri: gorunur alan disindakiler de kayitli kalir (offscreen). Boylece Tab sirasi ve UIA agaci
    // kaydirmadan bagimsizdir; fare yalniz ekrandaki ogeye isabet eder (HitMap::at).
    for (auto& [id, r] : page_hits.items) {
        r.y -= s.scroll;
        const bool on = r.bottom() > l.body.y && r.y < l.body.bottom();
        hits.items.push_back({id, r});
        if (auto it = page_hits.meta.find(id); it != page_hits.meta.end()) {
            hits.meta[id] = it->second;
            hits.meta[id].offscreen = !on;
        }
    }
    hits.focus_drawn = hits.focus_drawn || page_hits.focus_drawn;
    // Simetri denetimi kayitlari: satir hucreleri ve kart cerceveleri de pencere koordinatina cevrilir.
    for (auto& row : page_hits.rows) {
        for (auto& cell : row) cell.y -= s.scroll;
        hits.rows.push_back(std::move(row));
    }
    for (std::size_t i = shell_cards; i < card_log_.size(); ++i) card_log_[i].y -= s.scroll;
    hits.cards = std::move(card_log_);
    card_log_.clear();
    // Golge kayitlari da pencere koordinatina (bolgesel cizim komsu kartlarin golgesini ayni sirayla yeniden cizer).
    for (std::size_t i = shell_shadows; i < shadow_log_.size(); ++i) shadow_log_[i].y -= s.scroll;
    s.shadow_casters = std::move(shadow_log_);
    shadow_log_.clear();
    // Spec 2026-11-14 B12: Sistem sayfasindaki bolum kartlarinin yuksekligi degistiyse (cerceve icerikten once cizilir)
    // ayni kare bir kez daha cizilir; ikinci cizimde yukseklikler aynidir.
    // B13: Gecmis sayfasindaki degisiklik / test kartlari ayni yolu izler.
    if (s.systools_relayout || s.history_relayout) {
        const bool again_page = (s.systools_relayout && systools_page(s.page)) || (s.history_relayout && s.page == Page::History);
        s.systools_relayout = s.history_relayout = false;
        if (again_page && !relayout_pass_) {
            relayout_pass_ = true;
            HitMap again = paint_shell(g, w, h, s, vm, now);
            relayout_pass_ = false;
            return again;
        }
    }
    // Spec 2026-11-12 B2: oyun yeni secildiyse secili oyun seridi gorunur alana kaydirilir ve bir kez yeniden cizilir.
    if (s.games_reveal_strip) {
        s.games_reveal_strip = false;
        if (s.page == Page::Games && s.games_strip.h > 0) {
            const float max_after = std::max(0.f, used - l.body.h);
            const float ns = std::clamp(reveal_scroll(s.scroll, s.games_strip, l.body.y, l.body.bottom(), 8.f), 0.f, max_after);
            if (std::fabs(ns - s.scroll) > 0.5f) {
                s.scroll = ns;
                s.reveal_focus = false;  // serit one alindi; odaktaki kart icin ikinci bir kaydirma yapilmaz
                return paint_shell(g, w, h, s, vm, now);
            }
        }
    }
    // Spec 2026-11-12 B4: eski Baslangic / Sistem kimligiyle gelindiyse o bolumun basligi govdenin ustune kaydirilir.
    if (s.systools_reveal != 0) {
        const std::size_t sec = static_cast<std::size_t>(std::clamp(s.systools_reveal - 1, 0, 3));
        s.systools_reveal = 0;
        if (systools_page(s.page)) {
            const float max_after = std::max(0.f, used - l.body.h);
            const float ns = std::clamp(s.systools_sections[sec], 0.f, max_after);
            if (std::fabs(ns - s.scroll) > 0.5f) {
                s.scroll = ns;
                return paint_shell(g, w, h, s, vm, now);
            }
        }
    }
    // RDY-10: Tab/UIA ile odak tasindiysa odakli ogeyi gorunur alana kaydir ve bir kez yeniden ciz.
    if (s.reveal_focus) {
        s.reveal_focus = false;
        // Ust menu sekmeleri ve baslik seridi ogeleri (alt sekmeler dahil) kaydirilmaz; yalniz govde ogesi getirilir.
        bool in_body = false;
        for (std::size_t i = shell_items; i < hits.items.size() && !in_body; ++i) in_body = hits.items[i].first == s.focus;
        if (const RectF* r = hits.rect_of(s.focus); r && in_body) {
            const float max_after = std::max(0.f, used - l.body.h);
            const float ns = std::clamp(reveal_scroll(s.scroll, *r, l.body.y, l.body.bottom(), 8.f), 0.f, max_after);
            if (std::fabs(ns - s.scroll) > 0.5f) {
                s.scroll = ns;
                return paint_shell(g, w, h, s, vm, now);
            }
        }
    }
    return hits;
}

}  // namespace ef::ui
