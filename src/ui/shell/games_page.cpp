// Handoff v1 EXT-AC-01/02: "Oyunlar" sayfasi — gercek kurulu oyun listesi (Steam/Epic manifestleri),
// calisiyor durumu (izleyici), ayar okuma adaptoru ve canli olcum uyumluluk durumu. Canli olcum
// dogrulanmadikca kapali ve nedeni UIA'da okunur; "VAC guvenli" gibi rozet yok. F-47: oyun basina tam ekran
// iyilestirmeleri yalniz kullanici kapsamli, journal'li ve geri alinabilir Evet/Hayir ile degistirilir.
// Spec 2026-11-12 B2: ince oyun kartlari (ikon, ad, magaza, en cok iki durum etiketi) + izgaranin altinda secili oyun
// seridi (o oyunun butun islemleri ve onerileri) + tek satir kaynak etiketleri.
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "core/games/game_guide.h"
#include "core/games/vendor_guide.h"
#include "core/i18n/turkish.h"
#include "ui/shell/panel_text.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;

namespace {
std::wstring widen_g(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
StringId capture_text(const std::string& key) {
    if (key == "gate.capture_blocked") return StringId::GateCaptureBlocked;
    if (key == "gate.capture_stale") return StringId::GateCaptureStale;
    if (key == "gate.capture_unknown") return StringId::GateCaptureUnknown;
    return StringId::GateCaptureUnverified;
}

// F-59: risk kodu -> katalog metni. Bilinmeyen kod risk sayılmaz (gösterilmez).
StringId risk_text(const std::string& key) {
    if (key == "risk.launch.insecure") return StringId::RiskLaunchInsecure;
    if (key == "risk.launch.third_party") return StringId::RiskLaunchThirdParty;
    if (key == "risk.launch.high_priority") return StringId::RiskLaunchHighPriority;
    if (key == "risk.launch.threads") return StringId::RiskLaunchThreads;
    if (key == "risk.launch.resolution_override") return StringId::RiskLaunchResolution;
    if (key == "risk.video.vsync") return StringId::RiskVideoVsync;
    if (key == "risk.video.borderless") return StringId::RiskVideoBorderless;
    if (key == "risk.video.reflex_off") return StringId::RiskVideoReflexOff;
    if (key == "risk.video.msaa") return StringId::RiskVideoMsaa;
    if (key == "risk.process.realtime") return StringId::RiskProcessRealtime;
    if (key == "risk.process.high") return StringId::RiskProcessHigh;
    if (key == "risk.process.affinity_fixed") return StringId::RiskProcessAffinityFixed;
    return StringId::GamesRiskNone;
}

// F-12/F-15/F-16/F-20/F-26/F-28: sabit satıcı rehberi anahtarı -> katalog metni.
StringId vendor_text(const std::string& key) {
    if (key == "vendor.nvidia.title") return StringId::VendorNvidiaTitle;
    if (key == "vendor.nvidia.sysmem") return StringId::VendorNvidiaSysmem;
    if (key == "vendor.nvidia.clean_install") return StringId::VendorNvidiaCleanInstall;
    if (key == "vendor.nvidia.overlay") return StringId::VendorNvidiaOverlay;
    if (key == "vendor.amd.title") return StringId::VendorAmdTitle;
    if (key == "vendor.amd.profiles") return StringId::VendorAmdProfiles;
    if (key == "vendor.amd.hypr") return StringId::VendorAmdHypr;
    if (key == "vendor.amd.ris") return StringId::VendorAmdRis;
    if (key == "vendor.amd.channel") return StringId::VendorAmdChannel;
    return StringId::VendorGuideNote;
}

std::wstring upscaler_label(core::games::UpscalerKind k) {
    switch (k) {
        case core::games::UpscalerKind::Dlss: return L"DLSS";
        case core::games::UpscalerKind::DlssFrameGeneration: return L"DLSS Frame Generation";
        case core::games::UpscalerKind::DlssRayReconstruction: return L"DLSS Ray Reconstruction";
        case core::games::UpscalerKind::XeSs: return L"XeSS";
        case core::games::UpscalerKind::Fsr: return L"FSR";
    }
    return L"?";
}

// Kart etiketi / serit degeri: kisa ad (yalniz dosya varligi; surum yazilmaz).
std::wstring upscaler_short(core::games::UpscalerKind k) {
    switch (k) {
        case core::games::UpscalerKind::Dlss: return L"DLSS";
        case core::games::UpscalerKind::DlssFrameGeneration: return L"DLSS FG";
        case core::games::UpscalerKind::DlssRayReconstruction: return L"DLSS RR";
        case core::games::UpscalerKind::XeSs: return L"XeSS";
        case core::games::UpscalerKind::Fsr: return L"FSR";
    }
    return L"?";
}

std::wstring protection_label(core::games::ProtectionSystem s) {
    switch (s) {
        case core::games::ProtectionSystem::EasyAntiCheat: return L"Easy Anti-Cheat";
        case core::games::ProtectionSystem::BattlEye: return L"BattlEye";
        case core::games::ProtectionSystem::Vanguard: return L"Vanguard";
    }
    return L"?";
}
// EF-X11: kaynak adi (marka adi cevrilmez; bilinmeyen kimlik oldugu gibi).
std::wstring launcher_label(std::string_view id) {
    if (id == "steam") return L"Steam";
    if (id == "epic") return L"Epic Games";
    if (id == "gog") return L"GOG";
    if (id == "ubisoft") return L"Ubisoft Connect";
    if (id == "ea") return L"EA app";
    if (id == "battlenet") return L"Battle.net";
    if (id == "riot") return L"Riot Client";
    if (id == "xbox") return L"Xbox";
    return widen_g(id);
}

}  // namespace

float Screen::games_page(Graphics& g, const RectF& body_in, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    // BOOST maketi: <main class="p-4"> govdeyi baslik seridinin (pb-1) 16 DIP altinda baslatir; ortak duzen 8 verir.
    constexpr float kTopInset = 8.f;
    const RectF body{body_in.x, body_in.y + kTopInset, body_in.w, std::max(body_in.h - kTopInset, 0.f)};
    const auto& p = pal();
    const float pad = theme_.card_pad;
    const float gap = theme_.space(2);
    const float section_h = theme_.role(Role::Section).line;
    Stack full{body, theme_.card_gap};
    game_banner(g, full, body, vm, s, now, hits);
    // Salt okunur UIA metinleri (secili oyun seridi, kaynak etiketleri, ekrandan kalkan kart notlari) sirali Status
    // kimligi alir. Olcum gecisinde kart islevleri yan etkisiz doner (CardSlot::place false): kimlik yalniz cizimde alinir.
    StatusSeq seq{0};

    // --- F-60: makine geneli koruma servisleri (salt okuma; "yok" yalniz sorgu basariliysa) ---
    // Gate P3b: ayri kart yerine kaynak satirinin altindaki iki ogeli bilgi satirinda (draw_info) tek satir.
    auto protection_lines = [&] {
        std::vector<std::wstring> lines;
        if (vm.protections_loading || vm.protections.empty()) {
            lines.push_back(t(StringId::GamesProtectionUnknown));
        } else {
            for (const auto& ps : vm.protections) {
                std::wstring line = protection_label(ps.system) + L": ";
                line += t(ps.running ? StringId::GamesProtectionRunning
                                     : ps.installed ? StringId::GamesProtectionInstalled : StringId::GamesProtectionAbsent);
                lines.push_back(std::move(line));
            }
        }
        return lines;
    };

    // --- GUIDE-01: rekabetci ayar rehberi (genel, kanita dayali kurallar; oyun dosyasina yazilmaz) ---
    auto draw_guide = [&](CardSlot& slot) {
        const float inner_w = slot.area.w - 2 * pad;
        const auto head = t(StringId::GuideTitle);
        std::vector<std::wstring> lines;
        // EF-X12: katalogdaki oyunlarda bulguya gore en cok 3 ayar; ayari kullanici oyunda degistirir.
        for (const auto& gr : vm.games) {
            core::games::GuideInput gi;
            gi.store = gr.guide_store;
            gi.app_id = gr.guide_app_id;
            gi.build = gr.guide_build;
            gi.verified_builds = gr.guide_verified_builds;
            gi.renderer = gr.guide_renderer;
            gi.risk_keys = gr.risk_keys;
            for (const auto& item : core::games::select_game_guide(gi)) {
                std::wstring line = L"\u2022 " + widen_g(gr.name) + L": " + tk(item.title_key) + L" \u2014 " + tk(item.effect_key);
                if (!item.menu_key.empty()) line += L" (" + tk(item.menu_key) + L")";
                lines.push_back(std::move(line));
            }
            // EF-X25 T01: gozlenen API DX12/Vulkan ise surucu kuyrugu kapsami (yalniz metin; her oyun icin, katalog disi dahil).
            if (const auto note = core::games::driver_queue_note_key(gr.guide_renderer); !note.empty())
                lines.push_back(L"\u2022 " + widen_g(gr.name) + L": " + tk(note));
        }
        for (auto id : {StringId::GuideRes, StringId::GuideLatency, StringId::GuideSync, StringId::GuideWindow, StringId::GuideQuality,
                        StringId::GuideShader})
            lines.push_back(L"\u2022 " + t(id));
        const auto src = t(StringId::GuideSource);
        float h = 0;
        std::vector<float> hs;
        for (const auto& l : lines) hs.push_back(g.measure(Role::Body, l, inner_w).h), h += hs.back() + theme_.space(1);
        const float sh = g.measure(Role::Secondary, src, inner_w).h;
        RectF card;
        if (!slot.place(pad + section_h + gap + h + gap + sh + pad, card)) return;
        draw_card(g, card, false, depth::kPanelCardRadius);
        g.text(Role::Section, head, {card.x + pad, card.y + pad, inner_w, section_h}, p.text_primary);
        float y = card.y + pad + section_h + gap;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            g.text(Role::Body, lines[i], {card.x + pad, y, inner_w, hs[i]}, p.text_primary);
            y += hs[i] + theme_.space(1);
        }
        g.text(Role::Secondary, src, {card.x + pad, y + gap - theme_.space(1), inner_w, sh}, p.text_secondary);
    };

    // --- F-12/F-15/F-16/F-20/F-26/F-28: satıcı sürücü rehberi (salt okuma; ayar okunmaz/yazılmaz) ---
    auto draw_vendor = [&](CardSlot& slot) {
        const float inner_w = slot.area.w - 2 * pad;
        struct GLine {
            std::wstring text;
            float h = 0;
            bool heading = false;
            bool secondary = false;
        };
        std::vector<GLine> lines;
        for (const auto& sec : core::games::vendor_guide_sections()) {
            lines.push_back({t(vendor_text(sec.title_key)), 0, true});
            for (const auto& k : sec.point_keys) lines.push_back({L"• " + t(vendor_text(k)), 0, false});
        }
        // B-012 (R-B7): AMD/Intel dusuk gecikme rehberi (salt okuma; dugme yok). NVIDIA icin satir uretilmez;
        // "desteklenmiyor" ile "veri yetersiz" ayri durum satiridir.
        for (const auto& ll : core::games::low_latency_card_lines(vm.vendor_ll)) {
            std::wstring txt = tk(ll.key);
            if (!ll.heading && !ll.secondary) txt = L"• " + txt;
            lines.push_back({std::move(txt), 0, ll.heading, ll.secondary});
        }
        const auto note = t(StringId::VendorGuideNote);
        float h = 0;
        for (auto& l : lines) {
            l.h = g.measure(l.secondary ? Role::Secondary : Role::Body, l.text, inner_w).h;
            h += l.h + theme_.space(1);
        }
        const float nh = g.measure(Role::Secondary, note, inner_w).h;
        RectF vcard;
        if (!slot.place(pad + section_h + gap + h + gap + nh + pad, vcard)) return;
        draw_card(g, vcard, false, depth::kPanelCardRadius);
        g.text(Role::Section, t(StringId::VendorGuideTitle), {vcard.x + pad, vcard.y + pad, inner_w, section_h}, p.text_primary);
        float vy = vcard.y + pad + section_h + gap;
        for (const auto& l : lines) {
            g.text(l.secondary ? Role::Secondary : Role::Body, l.text, {vcard.x + pad, vy, inner_w, l.h}, l.heading ? p.text_primary : p.text_secondary);
            vy += l.h + theme_.space(1);
        }
        g.text(Role::Secondary, note, {vcard.x + pad, vy + gap - theme_.space(1), inner_w, nh}, p.text_secondary);
    };

    // --- Oyun izgarasi (BOOST maketi 2026-11-16, .game-card): 4 / 3 / 2 / 1 sutun (games_grid_columns) ---
    // Kart: card-v2 (r 10, gradyan, golge, kose yaylari) p-3; ust satir 32 DIP kisaltma kutusu (r 6, beyaz %6 zemin, %10
    // cizgi, 12px/600 vurgu) ya da oyunun gercek ikonu + ad (13px/600) ve "magaza · v<surum>" (11px, ikincil); gap-2.5;
    // etiket satiri (11px, px-2 py-0.5, r 4, beyaz %5, slate-300): durum etiketleri (en cok iki, game_status_chips), sonra
    // yer kalirsa koruma adi ve oyun klasorunde BULUNAN upscaler dosyalari (yalniz varlik; surum yazilmaz). Kartin tamami
    // secim dugmesidir (Hit GameCard + oyun indeksi; UIA RadioButton); karttan kalkan metinler UIA HelpText'indedir.
    // Suzgec (baslik seridi) yalniz gorunen kartlari suzer; kimlikler oyun indeksiyle kalir.
    constexpr float kTile = kGameIconTile;
    constexpr float kUnbounded = 10000.f;
    constexpr float kCardR = depth::kPanelCardRadius;
    constexpr float kHeadGap = 10.f;     // gap-2.5: kutu ile metin arasi
    constexpr float kPillPadX = 8.f;     // px-2
    constexpr float kPillGap = 4.f;      // gap-1
    constexpr float kPillR = 4.f;        // rounded
    constexpr float kStripInset = 15.f;  // secili oyun seridi: p-3.5 + 1 DIP kenarlik
    constexpr float kThinInset = 13.f;   // ince bilgi kartlari: p-3 + 1 DIP kenarlik
    const bool dark = pal_ == &theme_.dark;
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    const float name_l = theme_.role(Role::Semi13).line;
    const float sl = theme_.role(Role::Small11).line;
    const float chip_h = game_chip_h(theme_);
    const float line_gap = theme_.space(0);  // ayni bloktaki satirlar arasi
    const float head_h = std::max(kTile, name_l + sl);
    // Ikon kutusu: koyu palette oyunun gercek ikonu (yuvarlak koseli). Ikon yoksa, gecersizse ya da yuksek kontrastta
    // kisaltma kutusu (genel / yer tutucu ikon cizilmez): koyu temada maket kutusu, HC'de cerceve + sistem metni.
    const auto icon_tile = [&](const ViewModel::GameRowView& gr, const RectF& tile) {
        if (dark && g.image(gr.icon, tile, theme_.radius_control)) return;
        // Kisaltma: adin ilk iki sozcugunun ilk harfleri. Kutu ici yan bosluk space(0)/2: en genis Latin cifti ("WW")
        // de sigar; yine sigmayan (genis yazi sistemi) kisaltmada yalniz ilk harf kalir (vekil cift bolunmez).
        std::wstring initials = widen_g(game_initials(gr.name, tr));
        if (initials.size() > 1 && g.measure(Role::Semi12, initials, kUnbounded, false).w > kTile - theme_.space(0)) {
            const bool pair = initials[0] >= 0xD800 && initials[0] <= 0xDBFF && initials[1] >= 0xDC00 && initials[1] <= 0xDFFF;
            initials.resize(pair ? 2 : 1);
        }
        if (dark) {
            g.fill_round(tile, theme_.radius_control, Color{1, 1, 1, 0.06f});
            g.stroke_round(tile.inset(0.5f), theme_.radius_control, Color{1, 1, 1, 0.10f});
        } else {
            g.stroke_round(tile, theme_.radius_control, p.border);
        }
        if (!initials.empty()) g.text(Role::Semi12, initials, tile, dark ? p.accent : p.text_primary, {Align::Center, false, true});
    };
    const auto store_text = [&](const ViewModel::GameRowView& gr) {
        return gr.launcher == "manual" ? t(StringId::GamesSourceManual) : launcher_label(gr.launcher);
    };
    const auto advice_count = [&](const ViewModel::GameRowView& gr) {
        std::wstring n = t(StringId::ChipAdviceCount);
        if (const auto at = n.find(L"{n}"); at != std::wstring::npos) n.replace(at, 3, std::to_wstring(gr.risk_keys.size()));
        return n;
    };
    const auto chip_text = [&](GameChip c, const ViewModel::GameRowView& gr) {
        switch (c) {
            case GameChip::Running: return t(StringId::GamesRunning);
            case GameChip::StartOnce: return t(StringId::ChipStartOnce);
            case GameChip::Ready: return t(StringId::ChipReady);
            case GameChip::Protected: return t(StringId::ChipProtected);
            case GameChip::Advice: return advice_count(gr);
        }
        return std::wstring();
    };
    // Oyun klasorunde bulunan upscaler dosyalarinin kisa adlari (tekrarsiz). Yalniz dosya varligi bilinir: surum yazilmaz.
    const auto upscaler_kinds = [&](const ViewModel::GameRowView& gr) {
        std::vector<std::wstring> out;
        for (const auto& u : gr.upscalers) {
            auto l = upscaler_short(u.kind);
            if (std::find(out.begin(), out.end(), l) == out.end()) out.push_back(std::move(l));
        }
        return out;
    };
    const auto join_list = [](const std::vector<std::wstring>& v, std::wstring_view sep) {
        std::wstring out;
        for (std::size_t i = 0; i < v.size(); ++i) out += (i ? std::wstring(sep) : std::wstring()) + v[i];
        return out;
    };
    // F-47 / A1.1 durum cumleleri: deger kismi (seridin "Etiket: deger" satiri) ve tam cumle (UIA).
    const auto fs_value = [&](const ViewModel::GameRowView& gr) {
        if (!gr.fs_known) return t(StringId::GamesFsUnknownExe);
        if (!gr.fs_read_ok) return t(StringId::GamesFsUnreadable);
        if (!gr.fs_disabled) return t(StringId::GamesFsOn);
        return t(gr.fs_applied ? StringId::GamesFsApplied : StringId::GamesFsExternal);
    };
    const auto fs_status = [&](const ViewModel::GameRowView& gr) { return t(StringId::GamesFsStatus) + L" " + fs_value(gr); };
    const auto nv_value = [&](const ViewModel::GameRowView& gr) {
        return t(gr.nv_conflict ? StringId::GamesNvConflict : gr.nv_attention ? StringId::GamesNvAttention : gr.nv_partial ? StringId::GamesNvPartial
                 : gr.nv_applied ? StringId::GamesNvApplied : gr.nv_read_failed ? StringId::GamesNvReadFailed
                 : !gr.nv_has_profile ? StringId::GamesNvNoProfile : gr.nv_external ? StringId::GamesNvExternal : StringId::GamesNvAvailable);
    };
    const auto nv_status = [&](const ViewModel::GameRowView& gr) {
        if (gr.nv_exe.empty() && gr.nv_record_exe.empty()) return std::wstring();
        return t(StringId::GamesNvStatus) + L" " + nv_value(gr);
    };
    // "magaza · surum · koruma adi" (UIA). full_version: surum kesilmeden.
    const auto meta_text = [&](const ViewModel::GameRowView& gr, bool full_version) {
        std::wstring meta = store_text(gr);
        if (gr.manual_missing) meta += L" · " + t(StringId::GamesManualMissing);
        else if (gr.launcher != "manual" && !gr.manual_exe.empty()) meta += L" · " + t(StringId::GamesManualAlsoAdded);
        meta += L" · ";
        if (gr.version.empty()) meta += t(StringId::GamesVersionUnknown);
        else meta += t(StringId::GamesVersion) + L" " + widen_g(full_version ? gr.version : game_version_label(gr.version));
        if (!gr.protection.empty()) meta += L" · " + t(StringId::GamesProtectionPrefix) + L": " + widen_g(gr.protection);
        return meta;
    };
    // Kartin ikinci satiri (maket "Steam · v1.40.4.8"): magaza [· elle ekleme notu] · v<kisaltilmis surum>.
    const auto card_meta = [&](const ViewModel::GameRowView& gr) {
        std::wstring meta = store_text(gr);
        if (gr.manual_missing) meta += L" · " + t(StringId::GamesManualMissing);
        else if (gr.launcher != "manual" && !gr.manual_exe.empty()) meta += L" · " + t(StringId::GamesManualAlsoAdded);
        meta += L" · ";
        meta += gr.version.empty() ? t(StringId::GamesVersionUnknown) : L"v" + widen_g(game_version_label(gr.version));
        return meta;
    };
    // Kartin UIA HelpText'i: karttan kalkan butun metinler (bilgi kaybolmaz).
    const auto card_help = [&](const ViewModel::GameRowView& gr) {
        std::wstring h = meta_text(gr, true);
        h = a11y_join(std::move(h), t(gr.config_read ? StringId::GamesConfigRead : StringId::GamesNoAdapter));
        // Eski baslik satiri ("Canli olcum kapali") ve "Neden?" aciklamasi: bu oyunun nedeni.
        if (!gr.capture_key.empty()) h = a11y_join(a11y_join(std::move(h), t(StringId::GamesCaptureOff)), t(capture_text(gr.capture_key)));
        h = a11y_join(std::move(h), fs_status(gr));
        h = a11y_join(std::move(h), nv_status(gr));
        if (const auto ups = upscaler_kinds(gr); !ups.empty())
            h = a11y_join(std::move(h), t(StringId::GamesUpscalerShort) + L": " + join_list(ups, L", "));
        if (!gr.risk_keys.empty()) h = a11y_join(std::move(h), advice_count(gr));
        // Eski "Yapilandirma ve baslatma riskleri" kartinin durum / not satiri: oneri yoksa "bulunmadi" ile "okunamadi"
        // ayrimi (risk yok denemez) secim yapilmadan da okunur.
        h = a11y_join(std::move(h), t(!gr.risk_keys.empty() ? StringId::GamesRiskNote : gr.risk_scanned ? StringId::GamesRiskNone : StringId::GamesRiskUnscanned));
        // Eski "Neden?" blogunun son satiri (tam ekran iyilestirme notu): dugmesi olmayan / secilmemis oyunda da okunur.
        h = a11y_join(std::move(h), t(StringId::GamesFsNote));
        return h;
    };
    const auto pill_w = [&](const std::wstring& txt) { return std::ceil(g.measure(Role::Small11, txt, kUnbounded, false).w) + 2 * kPillPadX; };
    const auto pill = [&](const RectF& r, const std::wstring& txt) {
        if (dark) {
            g.fill_round(r, kPillR, Color{1, 1, 1, 0.05f});
            g.text(Role::Small11, txt, r, depth::kSlate300, {Align::Center, false, true});
        } else {
            g.stroke_round(r, kPillR, p.border);
            g.text(Role::Small11, txt, r, p.text_primary, {Align::Center, false, true});
        }
    };
    RectF strip_frame;  // secili oyun seridinin cercevesi (icerik koordinati; yoksa bos)
    auto draw_games_list = [&](Stack& st, const RectF& area) {
        const float inner_w = std::max(area.w - 2 * pad, 1.f);
        // Kimlik blogu kMaxGameRows ile sinirli: fazla kart secilemez ve durum vektorlerine yazilmaz (RDY-01).
        const std::size_t ids = std::min(vm.games.size(), static_cast<std::size_t>(kMaxGameRows));
        s.game_guides.assign(ids, false);
        s.game_fs.assign(ids, false);
        s.game_nv.assign(ids, false);
        s.game_manual.assign(ids, false);
        s.game_cards.assign(ids, false);
        s.game_card_order.clear();
        s.games_cols = 1;
        const auto message = [&](StringId id) {
            const auto msg = t(id);
            const float h = g.measure(Role::Body, msg, inner_w).h;
            const RectF card = st.next(h + 2 * pad);
            draw_card(g, card, false, kCardR);
            g.text(Role::Body, msg, card.inset(pad), p.text_secondary);
        };
        if (vm.games_loading || vm.games.empty()) {
            message(vm.games_loading ? StringId::GamesLoading : vm.library_untrusted ? StringId::GamesLibraryUnreadable : StringId::PageGamesEmpty);
            return;
        }
        // Cipler cizilmediyse (cok dar serit) suzgec degistirilemez: tum oyunlar gosterilir.
        const auto rows = games_filter_rows(vm.games, s.games_filters ? s.games_filter : GamesFilter::All);
        if (rows.empty()) {
            message(StringId::GamesFilterEmpty);
            return;
        }
        const int ncol = games_grid_columns(area.w);
        const float cgap = theme_.card_gap;
        const float cw = std::floor((area.w - static_cast<float>(ncol - 1) * cgap) / static_cast<float>(ncol));
        const float cin = std::max(cw - 2 * kGameCardInset, 1.f);  // kart ici genislik
        const float tin = std::max(cin - kTile - kHeadGap, 1.f);   // kutunun sagindaki metin genisligi
        const float card_h = game_card_h(theme_);                    // butun kartlar ayni boyda (ad tek satir)
        s.games_cols = ncol;
        const auto selected = games_selected_row(vm.games, s.games_selected);
        const float pulse = pulse_alpha(s.pulse_live, now, theme_.motion.pulse_period);
        for (std::size_t r0 = 0; r0 < rows.size(); r0 += static_cast<std::size_t>(ncol)) {
            const std::size_t r1 = std::min(rows.size(), r0 + static_cast<std::size_t>(ncol));
            const RectF row = st.next(card_h);
            std::vector<RectF> cells;  // simetri denetimi: izgara satirinin hucreleri
            for (std::size_t q = r0; q < r1; ++q) {
                const std::size_t i = rows[q];
                const auto& gr = vm.games[i];
                const RectF card{row.x + static_cast<float>(q - r0) * (cw + cgap), row.y, cw, card_h};
                cells.push_back(card);
                log_card(card);  // cerceve kaydi yukselmeden bagimsizdir (hover'da kart card_lift DIP yukari cizilir)
                const Hit id = hit_of(HitKind::GameCard, i);
                const bool is_sel = selected && *selected == i && id != Hit::None;
                const std::wstring name = widen_g(gr.name);
                const std::wstring store = store_text(gr);
                const GameChips chips = game_status_chips(gr);
                // Etiketler: durum (oncelikli) + koruma adi + bulunan upscaler dosyalari; sigmayan cizilmez, UIA adinda durur.
                struct Pill {
                    std::wstring text;
                    bool dot = false;
                };
                std::vector<Pill> pills;
                for (std::size_t k = 0; k < chips.count; ++k) pills.push_back({chip_text(chips.items[k], gr), chips.items[k] == GameChip::Running});
                if (!gr.protection.empty()) pills.push_back({widen_g(gr.protection)});
                for (auto& u : upscaler_kinds(gr)) pills.push_back({std::move(u)});
                // UIA adi: ad, magaza ve karttaki etiketler (secim UIA Selected ile okunur).
                std::wstring acc = a11y_join(name, store);
                for (const auto& pl : pills) acc = a11y_join(std::move(acc), pl.text);
                if (card.bottom() >= s.view_top && card.y <= s.view_bottom) {  // gorunmeyen kart cizilmez
                    ++s.rows_drawn;
                    // Hareket (maket hover:-translate-y-0.5): fareyle uzerine gelince kart card_lift DIP yukselir; basiliyken
                    // yerine iner. Animasyonlar kapaliyken kart yerinden oynamaz: hover yalniz kenarlik tonuyla gosterilir.
                    // Isabet kutusu yukselmez.
                    const float hv = hover_value(s, id, now);
                    const float pv = press_value(s, id, now);
                    const float lift = s.reduced_motion ? 0.f : theme_.card_lift * hv * (1.f - pv);
                    const RectF face{card.x, card.y - lift, card.w, card.h};
                    draw_card_face(g, face, false, kCardR);
                    if (dark) {
                        // .card-v2:hover kenarlik beyaz %15; secili kart !border-[#3DFF7A] (UIA'da Selected).
                        if (is_sel) g.stroke_round(face, kCardR, p.accent);
                        else if (hv > 0.f) g.stroke_round(face, kCardR, Color{1, 1, 1, 0.15f * hv});
                    } else {
                        // Yuksek kontrast: secili kart 2 DIP sistem vurgu kenarligi (renk + kalinlik).
                        if (is_sel) g.stroke_round(face.inset(0.5f), kCardR, p.cyan, 2.f);
                        else if (hv > 0.f) g.stroke_round(face, kCardR, mix(p.border, p.text_primary, hv * alpha_scale::kCardHoverTint));
                    }
                    const float x0 = face.x + kGameCardInset;
                    const float y0 = face.y + kGameCardInset;
                    icon_tile(gr, {x0, y0, kTile, kTile});
                    const float tx = x0 + kTile + kHeadGap;
                    g.text(Role::Semi13, fit_text(g, Role::Semi13, name, tin), {tx, y0, tin, name_l}, p.text_primary);
                    g.text(Role::Small11, fit_text(g, Role::Small11, card_meta(gr), tin), {tx, y0 + name_l, tin, sl}, p.text_secondary);
                    float cx = x0;
                    const float cy = y0 + head_h + kGameCardGap;
                    for (const auto& pl : pills) {
                        const float w = pl.dot ? dot_chip_width(g, pl.text) : pill_w(pl.text);
                        if (cx + w > x0 + cin + 0.5f) break;  // sigmayan etiket cizilmez (UIA adinda durur)
                        const RectF cr{cx, cy, w, chip_h};
                        if (pl.dot) {
                            const RectF dr = dot_chip(g, cr, pl.text, ChipTone::Good, pulse);
                            // Nabiz bolgesi: nokta tamamen gorunur govdedeyse (pencere koordinati).
                            if (dr.y >= s.view_top + 4.f && dr.bottom() <= s.view_bottom) s.pulse_regions.push_back({dr.x, dr.y - s.scroll, dr.w, dr.h});
                        } else {
                            pill(cr, pl.text);
                        }
                        cx += w + kPillGap;
                    }
                    if (s.focus_visible && s.focus == id) focus_ring(g, face, kCardR, hits);
                }
                // RDY-10: gorunmeyen kart da kaydedilir (Tab + UIA).
                hits.add(id, card, std::move(acc), AccRole::RadioButton, is_sel);
                if (id != Hit::None) {
                    hits.meta[id].help = card_help(gr);
                    s.game_cards[i] = true;
                    s.game_card_order.push_back(i);
                }
            }
            if (cells.size() >= 2) hits.rows.push_back(std::move(cells));
        }

        // --- Secili oyun seridi (maket #selected-game-strip): izgaranin altinda tam genislik card-v2 p-3.5. Solda
        // "<ad> — Secili Oyun Islemleri" (14px/600) ve "Etiket: deger" satiri (12px; deger kalin; upscaler vurgu): NVIDIA
        // oyun profili (surucude profil okunduysa), tam ekran iyilestirmeleri, upscaler dosyalari (yalniz varlik). Sagda o
        // oyunun butun islemleri (kimlikler, akislar ve Evet/Hayir onaylari ayni): ilk uygulama islemi .btn-primary
        // (h 28), digerleri .btn-secondary. Korumali oyunda tek kisa uyari, "Bir kez baslat" durumunda tek kisa yonlendirme;
        // oyunun onerileri madde madde (satirin altinda). Suzgec secili oyunu gizlediyse serit cizilmez.
        if (!selected || *selected >= ids || std::find(rows.begin(), rows.end(), *selected) == rows.end()) return;
        const std::size_t i = *selected;
        const auto& gr = vm.games[i];
        const std::wstring name = widen_g(gr.name);
        const float sin_w = std::max(area.w - 2 * kStripInset, 1.f);
        struct Btn {
            HitKind kind;
            std::wstring label, help;
            bool enabled = true, primary = false;
            float w = 0, x = 0, y = 0;  // dugme genisligi; x/y: blok ici konum
        };
        std::vector<Btn> btns;
        const bool busy_free = !vm.readiness.fix_running;
        const auto fs_line = fs_status(gr);
        const auto nv_line = nv_status(gr);
        // A1.1: dugme yalniz surucude profili olan ve baska kaynakla ayarlanmamis oyunda (maket sirasi: NVIDIA ilk).
        if (!nv_line.empty() && (gr.nv_applied || (gr.nv_has_profile && !gr.nv_external))) {
            const bool apply = !gr.nv_conflict && !gr.nv_applied;
            btns.push_back({HitKind::GameNvidia, t(gr.nv_conflict ? StringId::GamesNvKeep : gr.nv_applied ? StringId::GamesNvUndo : StringId::GamesNvApply),
                            nv_line, busy_free, apply});
            s.game_nv[i] = busy_free;
        }
        // F-47: dugme yalniz uygulanabilir oyunda (bilinmeyen EXE'ye yazilmaz).
        if (gr.fs_known && gr.fs_read_ok && (gr.fs_applied || !gr.fs_disabled)) {
            btns.push_back({HitKind::GameFullscreen, t(gr.fs_applied ? StringId::GamesFsActionUndo : StringId::GamesFsActionDisable),
                            a11y_join(fs_line, t(StringId::GamesFsNote)), busy_free, !gr.fs_applied});
            s.game_fs[i] = busy_free;
        }
        // GUIDE-01: taninan oyunda "Profesyonel ayarlar".
        if (!gr.pro_slug.empty()) {
            btns.push_back({HitKind::GameGuide, t(StringId::GuidePro), {}, true, false});
            s.game_guides[i] = true;
        }
        // EF-X11: elle eklenen oyun listeden cikarilabilir (oyun acikken kapali; dosyaya dokunulmaz).
        if (!gr.manual_exe.empty()) {
            btns.push_back({HitKind::GameManualRemove, t(StringId::GamesManualRemove), {}, !gr.running, false});
            s.game_manual[i] = !gr.running;
        }
        // Yalniz ilk dugme birincil olabilir (uygulama islemi); geri alma / koruma / rehber / cikarma ikincildir.
        for (std::size_t k = 1; k < btns.size(); ++k) btns[k].primary = false;
        const float bgap = theme_.space(1);
        constexpr float kIdentityMinW = 240.f;
        constexpr float kStripGap = 12.f;  // gap-3: kimlik ile dugmeler arasi
        float natural_w = 0;
        for (auto& b : btns) {
            b.w = std::min(sin_w, panel_button_rect(g, b.label, b.primary, false, true).w);
            natural_w += b.w + bgap;
        }
        if (!btns.empty()) natural_w -= bgap;
        const bool inline_btns = !btns.empty() && sin_w - natural_w - kStripGap >= kIdentityMinW;
        const float btn_area = inline_btns ? natural_w : sin_w;
        {
            float x = 0, y = 0;
            for (auto& b : btns) {
                if (x > 0 && x + b.w > btn_area + 0.5f) x = 0, y += theme_.button_row_h + bgap;
                b.x = x, b.y = y;
                x += b.w + bgap;
            }
        }
        const float btns_h = btns.empty() ? 0.f : btns.back().y + theme_.button_row_h;
        const bool btns_below = !btns.empty() && !inline_btns;
        const float id_w = std::max(sin_w - (inline_btns ? btn_area + kStripGap : 0.f), 1.f);
        // "Etiket: deger" gruplari (deger kalin); sigmayan grup alt satira akar.
        struct Group {
            std::wstring label, value;
            Color color;
            float lw = 0, vw = 0, x = 0;
            int line = 0;
        };
        std::vector<Group> groups;
        const Color strong = dark ? depth::kSlate200 : p.text_primary;
        if (!nv_line.empty())
            groups.push_back({t(StringId::GamesNvStatus), nv_value(gr), (gr.nv_conflict || gr.nv_attention || gr.nv_partial) ? p.warning : strong});
        groups.push_back({t(StringId::GamesFsStatus), fs_value(gr), strong});
        {
            std::wstring lab = t(StringId::GamesUpscalerShort);
            if (!lab.empty() && lab.back() != L':') lab += L':';
            const auto ups = upscaler_kinds(gr);
            std::wstring v = !ups.empty() ? join_list(ups, L", ")
                                          : t(vm.games_deep_deferred || !gr.upscalers_scanned ? StringId::GamesUpscalerNotScanned : StringId::GamesUpscalerFoundNone);
            groups.push_back({std::move(lab), std::move(v), !ups.empty() ? (dark ? p.accent : p.text_primary) : strong});
        }
        const float sl12 = theme_.role(Role::Small12).line;
        const float space_w = std::max(g.measure(Role::Small12, L"x x", kUnbounded, false).w - g.measure(Role::Small12, L"xx", kUnbounded, false).w, 2.f);
        const std::wstring sep = L"·";
        const float sep_w = space_w + std::ceil(g.measure(Role::Small12, sep, kUnbounded, false).w) + space_w;
        int nlines = 1;
        {
            float x = 0;
            for (auto& gp : groups) {
                gp.lw = std::ceil(g.measure(Role::Small12, gp.label, kUnbounded, false).w);
                gp.vw = std::ceil(g.measure(Role::Semi12, gp.value, kUnbounded, false).w);
                const float lead = x > 0 ? sep_w : 0.f;
                if (x > 0 && x + lead + gp.lw + space_w + gp.vw > id_w + 0.5f) x = 0, ++nlines;
                else x += lead;
                gp.x = x, gp.line = nlines - 1;
                // Tek basina sigmayan grup: deger kisaltilir (tam metin UIA'da).
                if (gp.lw + space_w + gp.vw > id_w) gp.vw = std::max(id_w - gp.lw - space_w, 1.f);
                x += gp.lw + space_w + gp.vw;
            }
        }
        const float title_l = theme_.role(Role::Title14).line;
        constexpr float kTitleGap = 2.f;  // mt-0.5
        const float block_h = title_l + kTitleGap + static_cast<float>(nlines) * sl12;
        // Govde satirlari: zorunlu uyari / yonlendirme / islem durumu uyarisi (tek satir), sonra oneriler.
        struct Line {
            std::wstring text;
            Role role;
            Color color;
            float h = 0;
        };
        std::vector<Line> lines;
        if (!gr.protection.empty()) lines.push_back({t(StringId::GamesStripProtected), Role::Small12, p.warning});
        if (!gr.fs_known) lines.push_back({t(StringId::GamesStripStartOnce), Role::Small12, p.text_secondary});
        // P12-B2a: yarim kalan / inceleme bekleyen / sonradan degisen NVIDIA kaydi ustteki "NVIDIA oyun profili: deger"
        // grubunda zaten uyari renginde yazili; ayni cumle ikinci satir olarak tekrarlanmaz (tam metin UIA adinda).
        for (const auto& k : gr.risk_keys) lines.push_back({L"• " + t(risk_text(k)), Role::Body, p.text_primary});
        float lines_h = 0;
        for (auto& l : lines) l.h = g.measure(l.role, l.text, sin_w).h, lines_h += l.h + line_gap;
        const float row_gap = theme_.space(1);
        const float head_block = std::max(block_h, inline_btns ? btns_h : 0.f);
        const float strip_h = kStripInset + head_block + (btns_below ? row_gap + btns_h : 0.f) +
                              (lines.empty() ? 0.f : row_gap + lines_h - line_gap) + kStripInset;
        const RectF card = st.next(strip_h);
        strip_frame = card;
        draw_card(g, card, false, kCardR);
        const float x0 = card.x + kStripInset;
        float y = card.y + kStripInset;
        const float ty = y + (head_block - block_h) / 2;
        {
            std::wstring title = t(StringId::GamesStripTitle);
            if (const auto at = title.find(L"{game}"); at != std::wstring::npos) title.replace(at, 6, name);
            g.text(Role::Title14, fit_text(g, Role::Title14, title, id_w), {x0, ty, id_w, title_l}, p.text_primary);
        }
        const float gy = ty + title_l + kTitleGap;
        for (std::size_t k = 0; k < groups.size(); ++k) {
            const auto& gp = groups[k];
            const float ly = gy + static_cast<float>(gp.line) * sl12;
            if (k > 0 && groups[k - 1].line == gp.line)
                g.text(Role::Small12, sep, {x0 + gp.x - sep_w, ly, sep_w, sl12}, p.text_secondary, {Align::Center, false, true});
            g.text(Role::Small12, gp.label, {x0 + gp.x, ly, gp.lw + 1.f, sl12}, p.text_secondary, {Align::Leading, false, true});
            const float vx = x0 + gp.x + gp.lw + space_w;
            g.text(Role::Semi12, fit_text(g, Role::Semi12, gp.value, gp.vw + 1.f), {vx, ly, gp.vw + 1.f, sl12}, gp.color, {Align::Leading, false, true});
        }
        // UIA: ad + kesilmemis "magaza · surum · koruma" satiri + ekranda yazilmayan durumlar. Dugmelerden once.
        {
            std::wstring acc = a11y_join(name, meta_text(gr, true));
            acc = a11y_join(std::move(acc), fs_line);
            acc = a11y_join(std::move(acc), nv_line);
            if (const auto ups = upscaler_kinds(gr); !ups.empty()) acc = a11y_join(std::move(acc), t(StringId::GamesUpscalerShort) + L": " + join_list(ups, L", "));
            if (!gr.capture_key.empty()) acc = a11y_join(std::move(acc), t(capture_text(gr.capture_key)));
            acc = a11y_join(std::move(acc), t(!gr.risk_keys.empty() ? StringId::GamesRiskNote : gr.risk_scanned ? StringId::GamesRiskNone : StringId::GamesRiskUnscanned));
            const Hit hid = seq.take();
            hits.add_text(hid, {x0, ty, id_w, block_h}, std::move(acc));
            // Eski upscaler bilgi satirinin notu ve tipik render olcegi (dosya varligi; kalite modu oyunda secilir).
            std::wstring help = t(StringId::GamesUpscalerNote);
            for (const auto& u : gr.upscalers) {
                const auto presets = core::games::nominal_presets(u.kind);
                if (presets.empty()) continue;
                std::vector<double> scales;
                for (const auto& pr : presets) scales.push_back(pr.scale);
                // Uyumluluk B6 (bosluk 28): yuzde bicimi dile gore (TR "%67", EN "67%").
                help = a11y_join(std::move(help), upscaler_label(u.kind) + L" — " + t(StringId::GamesUpscalerScale) + scale_percents_text(scales, tr));
                break;
            }
            hits.meta[hid].help = std::move(help);
        }
        const float bx0 = inline_btns ? card.right() - kStripInset - btn_area : x0;
        const float by0 = inline_btns ? y + (head_block - btns_h) / 2 : y + head_block + row_gap;
        for (const auto& b : btns) {
            const Hit id = hit_of(b.kind, i);
            if (id == Hit::None) continue;
            panel_button(g, bx0 + b.x, by0 + b.y, id, b.label, b.primary, s, now, hits, b.enabled, false, true, b.w);
            hits.meta[id].name = b.label + L": " + name;
            if (!b.help.empty()) hits.meta[id].help = b.help;
        }
        y += head_block + (btns_below ? row_gap + btns_h : 0.f);
        if (!lines.empty()) y += row_gap;
        for (const auto& l : lines) {
            const RectF lr{x0, y, sin_w, l.h};
            g.text(l.role, l.text, lr, l.color);
            hits.add_text(seq.take(), lr, l.text);
            y += l.h + line_gap;
        }
    };

    // F-47: islem sonucu (Evet/Hayir sonrasi gercek durum yeniden okunur; sonuc anahtar sozcukle yazilir).
    auto draw_notice = [&](Stack& st, const RectF& area) {
        const float inner_w = area.w - 2 * pad;
        if (!vm.games_notice_key.empty()) {
            const auto notice = tk(vm.games_notice_key);
            const float nh = g.measure(Role::Body, notice, inner_w).h;
            const RectF ncard = st.next(nh + 2 * pad);
            g.fill_round(ncard, kCardR, p.surface);
            g.stroke_round(ncard, kCardR, vm.games_notice_warning ? p.warning : p.border);
            g.text(Role::Body, notice, ncard.inset(pad), vm.games_notice_warning ? p.warning : p.text_primary);
        }
    };

    // --- Maket: iki ince bilgi karti (card-v2 p-3, 12px; solda ikincil etiket, sagda beyaz deger dizisi; lg:grid-cols-2):
    // "Oyun kaynaklari (N saglayici)" + saglayici basina "Ad (oyun sayisi)" ve "Makine geneli koruma servisleri" + servis
    // durumlari (F-60; "yok" yalniz sorgu basariliysa). Kaynak kartinin saginda "Oyun EXE'si ekle" (EF-X11, her zaman
    // etkin; eski kart notu HelpText'inde). Kapali / desteklenmeyen kaynak soluk ve sayisiz; okunamayan / kismen okunan
    // kaynak uyari tonunda ve durumu yazili. Deger dizisi sigmazsa satirlara akar (iki kart ayni boyda kalir); dar govdede
    // kartlar alt alta. Eski "Oyunlara dokunulmaz" satiri ve koruma notu koruma metninin UIA HelpText'indedir.
    struct Seg {
        std::wstring text, acc;
        Color color;
        float w = 0, x = 0;
        int line = 0;
    };
    struct Thin {
        std::wstring label;
        std::vector<Seg> segs;
        float reserve = 0, label_w = 0, h = 0;
        bool below = false;
        int lines = 1;
        std::vector<float> line_w;
    };
    const float tl = theme_.role(Role::Small12).line;
    const float tsep_w = std::ceil(g.measure(Role::Small12, L"x · x", kUnbounded, false).w - g.measure(Role::Small12, L"xx", kUnbounded, false).w);
    const auto layout_thin = [&](Thin& th, float w) {
        const float iw = std::max(w - 2 * kThinInset - th.reserve, 1.f);
        th.label_w = std::min(std::ceil(g.measure(Role::Small12, th.label, kUnbounded, false).w), iw);
        float widest = 0;
        for (auto& sg : th.segs) sg.w = std::min(std::ceil(g.measure(Role::Small12, sg.text, kUnbounded, false).w), iw), widest = std::max(widest, sg.w);
        constexpr float kLabelGap = 12.f, kMinFlow = 120.f;
        const float avail = iw - th.label_w - kLabelGap;
        th.below = avail < std::max(widest, kMinFlow);
        const float fw = th.below ? iw : avail;
        int line = th.below ? 1 : 0;
        float x = 0;
        th.line_w.assign(static_cast<std::size_t>(line), 0.f);
        for (auto& sg : th.segs) {
            const float lead = x > 0 ? tsep_w : 0.f;
            if (x > 0 && x + lead + sg.w > fw + 0.5f) {
                th.line_w.push_back(x);
                x = 0, ++line;
            } else {
                x += lead;
            }
            sg.x = x, sg.line = line;
            x += sg.w;
        }
        th.line_w.push_back(x);
        th.lines = line + 1;
        th.h = std::max(2 * kThinInset + static_cast<float>(th.lines) * tl, th.reserve > 0 ? theme_.button_row_h + 2 * theme_.space(1) : 0.f);
    };
    // Ilk satirin ust kenari: tek satirli kartta dikey ortali (dugmeli kart 44 DIP), cok satirlida ic kenarda.
    const auto thin_y0 = [&](const Thin& th, const RectF& card) { return th.lines == 1 ? card.y + (card.h - tl) / 2 : card.y + kThinInset; };
    const auto seg_rect = [&](const Thin& th, const RectF& card, const Seg& sg) {
        const float iw = std::max(card.w - 2 * kThinInset - th.reserve, 1.f);
        const float lw = th.line_w[static_cast<std::size_t>(sg.line)];
        const float base = th.below ? card.x + kThinInset : card.x + kThinInset + iw - lw;  // yan yana: saga yasli (justify-between)
        return RectF{base + sg.x, thin_y0(th, card) + static_cast<float>(sg.line) * tl, sg.w + 1.f, tl};
    };
    const auto draw_thin = [&](const Thin& th, const RectF& card) {
        draw_card(g, card, false, kCardR);
        const float x0 = card.x + kThinInset, y0 = thin_y0(th, card);
        g.text(Role::Small12, fit_text(g, Role::Small12, th.label, th.label_w + 1.f), {x0, y0, th.label_w + 1.f, tl}, p.text_secondary,
               {Align::Leading, false, true});
        for (std::size_t k = 0; k < th.segs.size(); ++k) {
            const auto& sg = th.segs[k];
            const RectF r = seg_rect(th, card, sg);
            if (k > 0 && th.segs[k - 1].line == sg.line)
                g.text(Role::Small12, L"·", {r.x - tsep_w, r.y, tsep_w, tl}, p.text_primary, {Align::Center, false, true});
            g.text(Role::Small12, fit_text(g, Role::Small12, sg.text, r.w), r, sg.color, {Align::Leading, false, true});
        }
    };
    const auto protection_ln = protection_lines();
    auto draw_sources = [&](Stack& st, const RectF& area) {
        const auto title = t(StringId::GamesSourcesTitle);
        Thin src;
        {
            std::wstring lab = t(StringId::GamesSourcesCountTitle);
            if (const auto at = lab.find(L"{n}"); at != std::wstring::npos) lab.replace(at, 3, std::to_wstring(vm.library_providers.size()));
            src.label = vm.library_providers.empty() ? title : lab;
        }
        for (const auto& pv : vm.library_providers) {
            Seg it;
            const std::wstring label = pv.provider == "manual" ? t(StringId::GamesSourceManual) : launcher_label(pv.provider);
            const std::wstring status = tk(pv.status_key);
            it.acc = label + L": " + status;
            if (pv.games > 0) {
                std::wstring n = t(StringId::GamesSourceCount);
                if (const auto at = n.find(L"{n}"); at != std::wstring::npos) n.replace(at, 3, std::to_wstring(pv.games));
                it.acc += L" (" + n + L")";
            }
            const bool off = pv.status_key == "games.source.disabled" || pv.status_key == "games.source.unsupported";
            const bool warn = pv.status_key == "games.source.unreadable" || pv.status_key == "games.source.partial";
            it.text = label;
            if (!off && !(warn && pv.games == 0)) it.text += L" (" + std::to_wstring(pv.games) + L")";
            if (warn) it.text += L" · " + status;
            it.color = warn ? p.warning : off ? (dark ? mix(p.text_secondary, p.background, alpha_scale::kSourceOffText) : p.text_secondary) : p.text_primary;
            src.segs.push_back(std::move(it));
        }
        if (src.segs.empty()) src.segs.push_back({t(StringId::GamesSourcesPending), t(StringId::GamesSourcesPending), p.text_secondary});
        const auto add = t(StringId::GamesAddExe);
        const float add_w = std::min(std::max(area.w - 2 * kThinInset, 1.f), panel_button_rect(g, add, false).w);
        if (!s.games_add_header) src.reserve = add_w + 12.f;
        Thin prot;
        prot.label = t(StringId::GamesProtectionMachine);
        for (const auto& l : protection_ln) prot.segs.push_back({l, l, p.text_primary});
        constexpr float kPairMinW = 360.f;  // bundan dar yarim genislikte kartlar alt alta
        const bool side = (area.w - theme_.card_gap) / 2 >= kPairMinW;
        const float cw = side ? std::floor((area.w - theme_.card_gap) / 2) : area.w;
        layout_thin(src, cw);
        layout_thin(prot, side ? area.w - cw - theme_.card_gap : area.w);
        RectF c1, c2;
        if (side) {
            const float h = std::max(src.h, prot.h);
            const RectF row = st.next(h);
            c1 = {row.x, row.y, cw, h};
            c2 = {row.x + cw + theme_.card_gap, row.y, row.w - cw - theme_.card_gap, h};
            hits.rows.push_back({c1, c2});
        } else {
            c1 = st.next(src.h);
            c2 = st.next(prot.h);
        }
        draw_thin(src, c1);
        for (const auto& sg : src.segs) hits.add_text(seq.take(), seg_rect(src, c1, sg), a11y_join(title, sg.acc));  // saglayici basina UIA metni
        if (!s.games_add_header) {
            const float bx = c1.right() - kThinInset - add_w;
            const float by = c1.y + (std::min(c1.h, theme_.button_row_h + 2 * theme_.space(1)) - theme_.button_row_h) / 2;
            panel_button(g, bx, by, Hit::GamesAddExe, add, false, s, now, hits, true, false, false, add_w);
            hits.meta[Hit::GamesAddExe].help = t(StringId::GamesSourcesNote);
        }
        draw_thin(prot, c2);
        {
            std::wstring joined;
            for (std::size_t k = 0; k < protection_ln.size(); ++k) joined += (k ? L" · " : L"") + protection_ln[k];
            RectF tr = seg_rect(prot, c2, prot.segs.front());
            for (const auto& sg : prot.segs) {
                const RectF r = seg_rect(prot, c2, sg);
                const float r0 = std::max(tr.right(), r.right()), b0 = std::max(tr.bottom(), r.bottom());
                tr.x = std::min(tr.x, r.x), tr.y = std::min(tr.y, r.y), tr.w = r0 - tr.x, tr.h = b0 - tr.y;
            }
            const Hit id = seq.take();
            hits.add_text(id, tr, a11y_join(t(StringId::GamesProtectionTitle), joined));
            hits.meta[id].help = a11y_join(a11y_join(t(StringId::GamesProtectionNote), t(StringId::GamesPolicyTitle)), t(StringId::GamesPolicy));
        }
    };

    // Rehber ac/kapa karti (uygulamanin kendi ozelligi; makette yok): ince kart, tam genislik bir satir; acilinca iki
    // rehber karti esit satir olur. ("Yapilandirma ve baslatma riskleri" karti kalkti: oneriler secili oyun seridinde.)
    auto draw_guides = [&](CardSlot& slot) {
        const auto label = t(StringId::GamesGuidesToggle) + (s.games_guides_open ? L"  ▴" : L"  ▾");
        RectF card;
        const float vpad = theme_.space(1);
        if (!slot.place(vpad + theme_.button_row_h + vpad, card)) return;
        draw_card(g, card, false, kCardR);
        const float lw = std::min(card.w - 2 * kThinInset, std::ceil(g.measure(Role::Body, label, 1000.f, false).w) + 2 * theme_.space(1));
        const RectF lr{card.x + kThinInset - theme_.space(1), card.y + vpad, lw, theme_.button_row_h};
        const float hv = hover_value(s, Hit::GamesGuides, now);
        if (hv > 0.01f) g.fill_round(lr, theme_.radius_control, p.surface_raised.with_alpha(std::min(1.f, hv)));
        g.text(Role::Body, label, lr, p.accent, {Align::Center, false, true});
        if (s.focus_visible && s.focus == Hit::GamesGuides) focus_ring(g, lr, theme_.radius_control, hits);
        hits.add(Hit::GamesGuides, lr, t(StringId::GamesGuidesToggle), AccRole::Expander, s.games_guides_open);
    };
    const float top = full.cursor;
    const RectF lead{body.x, body.y + top, body.w, 0};
    Stack ls{lead, theme_.card_gap};
    s.pulse_regions.clear();
    draw_notice(ls, lead);
    draw_games_list(ls, lead);
    draw_sources(ls, lead);
    // Secim yeni yapildiysa serit gorunur alana getirilir (kaydirma paint_shell'de; pencere koordinati).
    s.games_strip = strip_frame.h > 0 ? RectF{strip_frame.x, strip_frame.y - s.scroll, strip_frame.w, strip_frame.h} : RectF{};
    CardFlow flow;
    flow.area = {body.x, body.y, body.w, 0};
    flow.gap = theme_.card_gap;
    flow.log = &hits.rows;
    flow.cursor = top + ls.cursor;
    flow.stacked = content_columns(flow.area, theme_).stacked;
    flow.pair_max_ratio = kPairMaxRatio;  // boylar benzer degilse kart eslenmez, tam genislik alir
    const CardFn none;
    flow.pairs({{draw_guides, true},
                {s.games_guides_open ? CardFn(draw_guide) : none}, {s.games_guides_open ? CardFn(draw_vendor) : none}});
    return flow.used() + kTopInset;
}

float Screen::dot_chip_width(Graphics& g, std::wstring_view text) const {
    return chip_width(g, text) + theme_.status_dot + theme_.space(0);
}

RectF Screen::dot_chip(Graphics& g, const RectF& r, std::wstring_view text, ChipTone tone, float dot_alpha) const {
    const ChipColors c = chip_colors(pal(), tone);
    g.fill_round(r, theme_.radius_control, c.bg);
    const float d = theme_.status_dot;
    // Nokta kutusu: cipin duz (yuvarlak kose disi) kisminda; hizli yol bu kutuyu cip zeminiyle doldurup noktayi cizer.
    const RectF dot{r.x + theme_.space(1) - 1.f, r.y + (r.h - d) / 2 - 1.f, d + 2.f, d + 2.f};
    g.fill_circle(dot.x + dot.w / 2, dot.y + dot.h / 2, d / 2, c.fg.with_alpha(dot_alpha));
    const float tx = r.x + theme_.space(1) + d + theme_.space(0);
    g.text(Role::Secondary, text, {tx, r.y, std::max(r.right() - theme_.space(1) - tx, 1.f) + 1.f, r.h}, c.fg, {Align::Leading, false, true});
    return dot;
}

void Screen::paint_pulse_region(Graphics& g, const UiState& s, std::int64_t now) const {
    // "Calisiyor" cipi her zaman Good tonundadir (games_page: chip_tone). Kutu cip zeminiyle doldurulur, nokta guncel
    // opaklikla cizilir: tam cizimdeki dot_chip ile ayni geometri.
    const ChipColors c = chip_colors(pal(), ChipTone::Good);
    const float a = pulse_alpha(s.pulse_live, now, theme_.motion.pulse_period);
    for (const RectF& r : s.pulse_regions) {
        g.fill(r, c.bg);
        g.fill_circle(r.x + r.w / 2, r.y + r.h / 2, theme_.status_dot / 2, c.fg.with_alpha(a));
    }
}

GameChips game_status_chips(const ViewModel::GameRowView& g) {
    // Oncelik sirasi (ilk ikisi alinir); sonra gorunen siraya (enum sirasi) dizilir.
    std::array<GameChip, 4> want{};
    std::size_t n = 0;
    if (g.running) want[n++] = GameChip::Running;
    if (!g.protection.empty()) want[n++] = GameChip::Protected;
    if (!g.risk_keys.empty()) want[n++] = GameChip::Advice;
    if (!g.running) want[n++] = g.fs_known ? GameChip::Ready : GameChip::StartOnce;
    GameChips out;
    out.count = std::min<std::size_t>(n, out.items.size());
    for (std::size_t i = 0; i < out.count; ++i) out.items[i] = want[i];
    if (out.count == 2 && static_cast<int>(out.items[1]) < static_cast<int>(out.items[0])) std::swap(out.items[0], out.items[1]);
    return out;
}

std::string game_row_key(const ViewModel::GameRowView& g) { return g.launcher + "|" + g.dir_key + "|" + g.name; }

std::optional<std::size_t> games_selected_row(const std::vector<ViewModel::GameRowView>& games, std::string_view key) {
    if (key.empty()) return std::nullopt;
    for (std::size_t i = 0; i < games.size(); ++i)
        if (game_row_key(games[i]) == key) return i;
    return std::nullopt;
}

Hit games_arrow_target(const std::vector<std::size_t>& order, int cols, Hit focus, int dx, int dy) {
    const HitSlot slot = classify_hit(focus);
    if (slot.kind != HitKind::GameCard || cols < 1) return Hit::None;
    const auto it = std::find(order.begin(), order.end(), slot.index);
    if (it == order.end()) return Hit::None;
    const auto at = static_cast<std::ptrdiff_t>(it - order.begin());
    const auto n = static_cast<std::ptrdiff_t>(order.size());
    // Sol / sag gorunen sirada komsu karta gider (satir sonunda alt / ust satira gecer); yukari / asagi bir satir atlar.
    const std::ptrdiff_t to = at + dx + static_cast<std::ptrdiff_t>(dy) * cols;
    if (to < 0 || to >= n || to == at) return Hit::None;
    return hit_of(HitKind::GameCard, order[static_cast<std::size_t>(to)]);
}

bool games_filter_match(GamesFilter f, const ViewModel::GameRowView& g) {
    switch (f) {
        case GamesFilter::All: return true;
        case GamesFilter::Protected: return !g.protection.empty();
        case GamesFilter::Advice: return !g.risk_keys.empty();
    }
    return true;
}

std::vector<std::size_t> games_filter_rows(const std::vector<ViewModel::GameRowView>& games, GamesFilter f) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < games.size(); ++i)
        if (games_filter_match(f, games[i])) out.push_back(i);
    return out;
}

std::string game_version_label(std::string_view v, std::size_t max_chars) {
    // Kod noktasi sinirlari: gecersiz bayt tek karakter sayilir (cok baytli karakter bolunmez).
    std::size_t chars = 0, cut = v.size();
    for (std::size_t i = 0; i < v.size(); ++chars) {
        if (chars == max_chars) {
            cut = i;
            break;
        }
        const auto b = static_cast<unsigned char>(v[i]);
        std::size_t n = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 1;
        if (i + n > v.size()) n = 1;
        for (std::size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(v[i + k]) & 0xC0) != 0x80) {
                n = 1;
                break;
            }
        i += n;
    }
    if (cut == v.size()) return std::string(v);
    while (cut > 0 && v[cut - 1] == ' ') --cut;
    return std::string(v.substr(0, cut)) + "\xE2\x80\xA6";  // U+2026
}

std::size_t game_button_rows(const std::vector<float>& widths, float avail, float gap) {
    std::size_t rows = widths.empty() ? 0 : 1;
    float x = 0;
    for (const float w : widths) {
        if (x > 0 && x + w > avail) x = 0, ++rows;
        x += w + gap;
    }
    return rows;
}

std::vector<float> game_button_widths(const std::vector<float>& text_w, float avail, float gap, float pad, float compact_pad, float min_w) {
    const auto sized = [&](float inner) {
        std::vector<float> out;
        out.reserve(text_w.size());
        for (const float t : text_w) out.push_back(std::min(avail, std::max(min_w, std::ceil(t) + 2 * inner)));
        return out;
    };
    auto natural = sized(pad);
    if (text_w.size() < 2 || compact_pad >= pad) return natural;
    auto compact = sized(compact_pad);
    return game_button_rows(compact, avail, gap) < game_button_rows(natural, avail, gap) ? compact : natural;
}

std::string game_initials(std::string_view name, bool turkish) {
    // UTF-8 kod noktalari; gecersiz bayt ayirici sayilir (kisaltmaya girmez).
    std::vector<std::vector<std::string>> all;  // sozcukler; her oge bir kod noktasinin UTF-8 parcasi
    std::vector<std::string> cur;
    const auto flush = [&] {
        if (!cur.empty()) all.push_back(std::move(cur));
        cur.clear();
    };
    for (std::size_t i = 0; i < name.size();) {
        const auto b = static_cast<unsigned char>(name[i]);
        std::size_t n = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
        char32_t cp = n == 1 ? b : n == 2 ? (b & 0x1Fu) : n == 3 ? (b & 0x0Fu) : (b & 0x07u);
        bool ok = n > 0 && i + n <= name.size();
        for (std::size_t k = 1; ok && k < n; ++k) {
            const auto c = static_cast<unsigned char>(name[i + k]);
            ok = (c & 0xC0) == 0x80;
            cp = (cp << 6) | (c & 0x3Fu);
        }
        if (!ok) {
            flush();
            ++i;
            continue;
        }
        const bool ascii_alnum = (cp >= U'0' && cp <= U'9') || (cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z');
        // ASCII disi: Latin harfleri ve diger alfabeler harf sayilir; noktalama/simge bloklari ayiricidir.
        const bool wide_letter = cp >= 0x00C0 && cp != 0x00D7 && cp != 0x00F7 && !(cp >= 0x2000 && cp <= 0x2BFF) &&
                                 !(cp >= 0x3000 && cp <= 0x303F) && !(cp >= 0xFE00 && cp <= 0xFE6F) && !(cp >= 0xFF00 && cp <= 0xFF0F) &&
                                 cp != 0xFFFD && !(cp >= 0x1F000 && cp <= 0x1FFFF) && cp < 0xE0000;  // emoji / piktogram / etiket: harf degil
        const bool joiner = cp == U'\'' || cp == 0x2019;  // kesme isareti sozcugu bolmez ("Assassin's" tek sozcuk)
        if (ascii_alnum || wide_letter) cur.emplace_back(name.substr(i, n));
        else if (!joiner) flush();
        i += n;
    }
    flush();
    std::string picked;
    if (all.empty()) return picked;
    if (all.size() >= 2) picked = all[0][0] + all[1][0];
    else picked = all[0][0] + (all[0].size() > 1 ? all[0][1] : std::string());
    std::string up = core::i18n::tr_upper(picked);
    if (!turkish) {  // Turkce disi arayuz: i -> I (tr_upper'in U+0130'u geri alinir)
        static constexpr std::string_view kDotI = "\xC4\xB0";
        for (std::size_t at = up.find(kDotI); at != std::string::npos; at = up.find(kDotI, at + 1)) up.replace(at, kDotI.size(), "I");
    }
    return up;
}

void Screen::games_header(Graphics& g, HeaderBar& bar, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    const float h = theme_.button_row_h;
    const auto& p = pal();
    const bool dark = pal_ == &theme_.dark;
    constexpr float kUnbounded = 10000.f;
    // Spec 2026-11-12 B2: "Canli olcum kapali · Neden?" satiri cizilmez (metinleri oyun kartinin UIA HelpText'inde);
    // games_why hep false kalir (Hit::GamesWhy odak sirasina girmez).
    s.games_filters = s.games_why = s.games_rescan = s.games_add_header = false;
    const bool listed = !vm.games_loading && !vm.games.empty();
    // "Oyun EXE'si ekle" (EF-X11, her zaman etkin): sigarsa seritte "Yeniden tara"nin solunda, sigmazsa kaynak kartinda.
    const auto add = t(StringId::GamesAddExe);
    const float add_w = panel_button_rect(g, add, false).w;
    // "Yeniden tara" (.btn-secondary): genislik iki etiketin genisidir (etiket degisince yer oynamaz).
    const auto idle = t(StringId::GateRetry), busy = t(StringId::GateRetrying);
    const float rescan_w = std::max(panel_button_rect(g, idle, false).w, panel_button_rect(g, busy, false).w);
    // Suzgec hapciklari (maket #header-left-chips: px-2.5 py-0.5 rounded-full 11px, gap-1.5; basliktan gap-3): secili =
    // vurgu %15 zemin + %40 cizgi + vurgu metin, digerleri beyaz %5 zemin + ikincil metin. Metin "Etiket N". Isabet kutusu
    // dokunma hedefi yuksekliginde, hapcik onun icinde ortali. Ucu birden (Yeniden tara ile) sigmiyorsa hic cizilmez.
    if (listed) {
        struct F {
            GamesFilter f;
            StringId label;
        };
        const F fs[] = {{GamesFilter::All, StringId::GamesFilterAll},
                        {GamesFilter::Protected, StringId::GamesFilterProtected},
                        {GamesFilter::Advice, StringId::GamesFilterAdvice}};
        constexpr float kPadX = 10.f, kGap = 6.f, kTitleGap = 12.f, kPillLine = 2.f;
        std::array<std::wstring, std::size(fs)> labels, counts, texts;
        std::array<float, std::size(fs)> ws{};
        const float x0 = bar.lead - 2 * bar.gap + kTitleGap;
        float total = kGap * static_cast<float>(std::size(fs) - 1);
        for (std::size_t i = 0; i < std::size(fs); ++i) {
            labels[i] = t(fs[i].label);
            counts[i] = std::to_wstring(games_filter_rows(vm.games, fs[i].f).size());
            texts[i] = labels[i] + L" " + counts[i];
            ws[i] = std::ceil(g.measure(Role::Small11, texts[i], kUnbounded, false).w) + 2 * kPadX + (s.games_filter == fs[i].f ? kPillLine : 0.f);
            total += ws[i];
        }
        const bool with_add = x0 + total + bar.gap + add_w + bar.gap + rescan_w <= bar.trail;
        if (with_add || x0 + total + bar.gap + rescan_w <= bar.trail) {
            s.games_add_header = with_add;
            s.games_filters = true;
            const float hit_h = std::min(std::max(theme_.touch_min, theme_.role(Role::Small11).line + 4.f + kPillLine), bar.strip.h);
            const float cy = bar.strip.y + bar.strip.h / 2;
            float x = x0;
            for (std::size_t i = 0; i < std::size(fs); ++i) {
                const Hit id = games_filter_hit(fs[i].f);
                const bool sel = s.games_filter == fs[i].f;
                std::wstring name = t(StringId::GamesFilterCount);
                if (const auto at = name.find(L"{label}"); at != std::wstring::npos) name.replace(at, 7, labels[i]);
                if (const auto at = name.find(L"{n}"); at != std::wstring::npos) name.replace(at, 3, counts[i]);
                const RectF hit{x, cy - hit_h / 2, ws[i], hit_h};
                // Hapcik: py-0.5 (2) + 11px satir (16.5); secili hapcikta 1 DIP cizgi iki yanda.
                const float ph = theme_.role(Role::Small11).line + 4.f + (sel ? kPillLine : 0.f);
                const RectF pr{x, cy - ph / 2, ws[i], ph};
                const float hv = hover_value(s, id, now);
                if (dark) {
                    if (sel) {
                        g.fill_round(pr, ph / 2, p.accent.with_alpha(0.15f));
                        g.stroke_round(pr.inset(0.5f), ph / 2 - 0.5f, p.accent.with_alpha(0.40f));
                    } else {
                        g.fill_round(pr, ph / 2, Color{1, 1, 1, 0.05f + 0.03f * hv});
                    }
                    g.text(Role::Small11, texts[i], pr, sel ? p.accent : mix(p.text_secondary, p.text_primary, hv), {Align::Center, false, true});
                } else {
                    g.stroke_round(pr.inset(sel ? 1.f : 0.5f), ph / 2, sel ? p.accent : mix(p.border, p.accent, hv), sel ? 2.f : 1.f);
                    g.text(Role::Small11, texts[i], pr, p.text_primary, {Align::Center, false, true});
                }
                if (s.focus_visible && s.focus == id) focus_ring(g, hit, hit_h / 2, hits);
                hits.add(id, hit, name, AccRole::RadioButton, sel);
                x += ws[i] + kGap;
            }
            bar.lead = std::min(x - kGap + bar.gap, bar.trail);
        }
    }
    // "Yeniden tara": kitaplik + surec taramasini simdi yeniler (tarama surerken devre disi; etiket durumu soyler).
    if (const RectF r = bar.take(rescan_w, h); r.w > 0) {
        const bool enabled = !vm.detection_retrying;
        panel_button(g, r.x, r.y, Hit::GamesRescan, vm.detection_retrying ? busy : idle, false, s, now, hits, enabled, false, false, rescan_w);
        s.games_rescan = true;
    }
    if (!listed) s.games_add_header = bar.fits(add_w);
    if (s.games_add_header) {
        const RectF r = bar.take(add_w, h);
        panel_button(g, r.x, r.y, Hit::GamesAddExe, add, false, s, now, hits, true, false, false, add_w);
        hits.meta[Hit::GamesAddExe].help = t(StringId::GamesSourcesNote);
    }
}

}  // namespace ef::ui
