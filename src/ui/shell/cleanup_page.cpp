// WIN-08: Temizlik bolumu. Onizleme (gercek dosya sayisi/boyutu) -> kategori secimi -> geri alinamaz uyarisi + onay
// (uygulama ici kip penceresi; App sorar) -> gercek dosya sayisiyla ilerleme / Durdur -> olculen sonuc. ADR-016.
// Spec 2026-11-12 B4: Sistem araclari tek sayfasinin ilk bolumu. Ust satir (kartsiz): solda 84 DIP halka (secili
// kategorilerin toplam boyutu ortada; secim degisince motion.cleanup_ring ile doner; temizlik bitince yesil +
// "Temizlendi"), yaninda "N kategori secili" ve surucu haplari, sagda "Depolama ayarlarini ac" · "Onizleme" · ana
// "Temizle". Altinda kategoriler iki sutun kutu: anahtar, ad, boyut (seciliyse camgobegi kenar). Yonetici izni
// gerekenlerde sari "Yonetici" etiketi; bulunmayan soluk + "yok". Kaldirilan aciklamalar (surucu notu, birim kimligi,
// "uzerine gelin" ipucu, tahmini kazanc cumlesi) UIA ad / HelpText'te kalir.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

#include "core/measure/view.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
namespace cc = core::cleanup;

namespace {
std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::wstring rep(std::wstring s, std::wstring_view k, const std::wstring& v) {
    for (std::size_t p = s.find(k); p != std::wstring::npos; p = s.find(k, p + v.size())) s.replace(p, k.size(), v);
    return s;
}
}  // namespace

std::wstring format_bytes(std::uint64_t b, bool tr) {
    const double kib = 1024.0, mib = kib * 1024, gib = mib * 1024;
    if (b >= static_cast<std::uint64_t>(gib)) return widen(core::measure::format_number(b / gib, 1, tr)) + L" GB";
    if (b >= static_cast<std::uint64_t>(mib)) return widen(core::measure::format_number(b / mib, 1, tr)) + L" MB";
    if (b >= 1024) return widen(core::measure::format_number(b / kib, 0, tr)) + L" KB";
    return std::to_wstring(b) + L" B";
}

namespace {
std::wstring serial_text(std::uint32_t v) {
    wchar_t buf[16];
    std::swprintf(buf, 16, L"%04X-%04X", (v >> 16) & 0xFFFFu, v & 0xFFFFu);
    return buf;
}
}  // namespace

void Screen::cleanup_header(Graphics& g, HeaderBar& bar, HitMap& hits) {
    // Baslik seridi: kirmizi "Temizlik geri alinamaz" cipi (zorunlu uyari). Renk tek basina anlam tasimaz (kelime); UIA'da
    // tam cumle okunur. Sigmiyorsa (asgari pencerenin altinda) cizilmez.
    const auto text = t(StringId::CleanupIrreversibleChip);
    const RectF r = bar.take(chip_width(g, text), theme_.role(Role::Secondary).line);
    if (r.w <= 0) return;
    chip(g, r, text, ChipTone::Danger);
    hits.add_text(hit_of(HitKind::Status, static_cast<std::size_t>(kMaxStatusItems) - 1), r, t(StringId::CleanupIrreversible));
}

float Screen::cleanup_section(Graphics& g, const RectF& area, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits, StatusSeq& seq) {
    const auto& p = pal();
    const auto& cv = vm.cleanup;
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    const bool hc = pal_ != &theme_.dark;
    const float pad = theme_.card_pad;
    const float bl = theme_.role(Role::Body).line, sl = theme_.role(Role::Secondary).line;
    constexpr float kUnbounded = 10000.f;
    s.cleanup_phase = cv.phase;
    s.cleanup_toggles.assign(cv.rows.size(), false);
    s.cleanup_file_buttons.assign(cv.rows.size(), false);
    s.cleanup_can_run = cv.any_selected && !vm.game_gate_blocked && !cv.admin_pending;  // dugme oyun/pending sirasinda cizilmez
    s.cleanup_volumes = 0;
    s.cleanup_more = false;
    const bool editable = cv.phase == cc::SessionPhase::Ready || cv.phase == cc::SessionPhase::Done;
    const bool busy = cv.phase == cc::SessionPhase::Scanning || cv.phase == cc::SessionPhase::Running || cv.phase == cc::SessionPhase::Confirming;
    const float x = area.x + pad, inner_w = std::max(area.w - 2 * pad, 1.f);

    // ---------- Spec 2026-11-14 B12 kart baslik seridi: ikon, "Temizlik", ozet (N kategori secili / Temizlendi /
    // taraniyor) ve sagda eylemler ----------
    std::size_t selected_n = 0;
    std::uint64_t ready_total = 0;
    for (const auto& r : cv.rows) {
        selected_n += r.selected ? 1 : 0;
        if (r.status == cc::PreviewStatus::Ready) ready_total += r.bytes;
    }
    const bool done = cv.phase == cc::SessionPhase::Done && cv.report;
    const bool scanning = cv.phase == cc::SessionPhase::Scanning;
    std::wstring head;
    Color head_c = p.text_primary;
    if (scanning) {
        head = t(StringId::CleanupPreviewScanning) + L"…";
        head_c = p.text_secondary;
    } else if (done) {
        head = t(StringId::CleanupCleaned);
        head_c = p.success;
    } else if (cv.phase == cc::SessionPhase::Ready || cv.phase == cc::SessionPhase::Confirming) {
        head = selected_n > 0 ? rep(t(StringId::CleanupRingSelected), L"{n}", std::to_wstring(selected_n)) : t(StringId::CleanupNothingSelected);
        if (selected_n == 0) head_c = p.text_secondary;
    } else {
        head = rep(t(StringId::CleanupRingSelected), L"{n}", L"0");
        head_c = p.text_secondary;
    }
    // Dugmeler (gorunen sira = Tab sirasi): Depolama ayarlari -> Onizleme -> Temizle; tarama surerken Iptal.
    const bool show_actions = !scanning && cv.phase != cc::SessionPhase::Running && cv.phase != cc::SessionPhase::Confirming;
    const bool show_storage = cv.phase != cc::SessionPhase::Running && cv.phase != cc::SessionPhase::Confirming;
    const bool show_run = show_actions && cv.phase == cc::SessionPhase::Ready && cv.any_selected && !vm.game_gate_blocked && !cv.admin_pending;
    const auto l_storage = t(StringId::CleanupStorageSettings), l_scan = t(StringId::CleanupPreview), l_run = t(StringId::CleanupRun),
               l_cancel = t(StringId::CleanupScanCancel);
    float btn_w = 0;
    const auto add_w = [&](const std::wstring& label) { btn_w += (btn_w > 0 ? theme_.space(1) : 0.f) + button_rect(g, 0, 0, label).w; };
    if (show_storage) add_w(l_storage);
    if (show_actions) add_w(l_scan);
    if (show_run) add_w(l_run);
    if (scanning) add_w(l_cancel);
    RectF slot{};
    const float line_y = section_head_v2(g, area, L"01", false, t(StringId::NavCleanup), head, head_c, btn_w, hits, seq, {}, &slot, false);
    if (btn_w > 0) {
        float bx = slot.x;
        const float by = slot.y;
        // Yalniz ms-settings:storagesense acar; temizlenen alana sayilmaz (Handoff 06).
        if (show_storage) bx += button(g, bx, by, Hit::CleanStorageSettings, l_storage, false, s, now, hits) + theme_.space(1);
        if (show_actions)
            bx += button(g, bx, by, Hit::CleanScan, l_scan, !cv.admin_pending && (cv.phase == cc::SessionPhase::Idle || cv.phase == cc::SessionPhase::Failed),
                         s, now, hits) +
                  theme_.space(1);
        if (show_run) bx += button(g, bx, by, Hit::CleanRun, l_run, true, s, now, hits) + theme_.space(1);
        if (scanning) button(g, bx, by, Hit::CleanScanCancel, l_cancel, false, s, now, hits);
    }
    // Ince cizgi en sonda, govde bos degilse cizilir (bos govdede kart baslik seridinde biter; Baslangic karti gibi).
    Stack st{{x, line_y + 1 + theme_.card_pad, inner_w, 0}, theme_.space(2)};

    // Bildirim satiri (tek metin; ton cerceve + metin rengi).
    auto notice = [&](const std::wstring& text, Role role, const Color& fg, const Color& outline, bool danger) {
        const float iw = inner_w - 2 * pad;
        const float h = g.measure(role, text, iw).h;
        const RectF card = st.next(theme_.space(1) + h + theme_.space(1));
        if (danger) {
            draw_card(g, card, true);
        } else {
            g.fill_round(card, theme_.radius_card, p.surface);
            g.stroke_round(card, theme_.radius_card, outline);
        }
        g.text(role, text, {card.x + pad, card.y + theme_.space(1), iw, h}, fg);
        hits.add_text(seq.take(), card, text);
    };

    // WIN-08 ek: yonetici izni isteniyor / reddedildi / yardimci baslatilamadi (tahmin yok; durum metni).
    {
        std::wstring gate_note;
        if (!cv.gate_reason_key.empty()) gate_note = tk(cv.gate_reason_key);  // Evet sonrasi/silme sirasinda kapi: gercek neden
        else if (cv.report && cv.report->incomplete) gate_note = t(StringId::CleanupIncomplete);
        if (!gate_note.empty()) notice(gate_note, Role::Secondary, p.warning, p.warning.with_alpha(alpha_scale::kOutline), false);
        if (cv.admin_pending || !cv.admin_notice_key.empty()) {
            const auto text = cv.admin_pending ? t(StringId::CleanupAdminPending) : tk(cv.admin_notice_key);
            const Color c = cv.admin_pending ? p.text_secondary : p.warning;
            notice(text, Role::Secondary, c, c.with_alpha(alpha_scale::kOutline), false);
        }
    }

    // --- Onay: uygulama ici kip penceresi (App sorar; spec E). Sayfadaki Evet/Hayir seridi yalniz kip penceresi acik
    // degilken cizilir (yedek yol; kimlikler CleanYes / CleanNo korunur). Kip penceresi acikken sayfa karartilir. ---
    if (cv.phase == cc::SessionPhase::Confirming && !s.modal) {
        const float iw = inner_w - 2 * pad;
        const float section_h = theme_.role(Role::Section).line;
        const auto q = t(StringId::CleanupRunQuestion);
        std::size_t sel = 0;
        for (const auto& r : cv.rows) sel += r.selected ? 1 : 0;
        const auto detail = rep(rep(rep(t(StringId::CleanupConfirmBody), L"{n}", std::to_wstring(sel)), L"{size}", format_bytes(cv.selected_bytes, tr)), L"{root}",
                                cv.volume);
        const auto yes = t(StringId::CleanupConfirmDelete), no = t(StringId::ActionCancel);
        const float yes_w = vm.game_gate_blocked ? 0.f : button_rect(g, 0, 0, yes).w + theme_.space(1);
        const float cbtn_w = yes_w + button_rect(g, 0, 0, no).w;
        constexpr float kTextMinW = 280.f;
        const bool side = iw >= kTextMinW + theme_.space(3) + cbtn_w;
        const float text_w = side ? iw - cbtn_w - theme_.space(3) : iw;
        const float qh = std::max(g.measure(Role::Section, q, text_w).h, section_h);
        const float dh = g.measure(Role::Secondary, detail, text_w).h;
        const float text_h = qh + dh;
        const float h = pad + (side ? std::max(text_h, theme_.button_h) : text_h + theme_.space(1) + theme_.button_h) + pad;
        const RectF card = st.next(h);
        g.fill_round(card, theme_.radius_dialog, p.surface_raised);
        g.stroke_round(card, theme_.radius_dialog, p.warning.with_alpha(alpha_scale::kOutline));
        const float cx = card.x + pad;
        const float ty = card.y + pad + (side ? (std::max(text_h, theme_.button_h) - text_h) / 2 : 0.f);
        g.text(Role::Section, q, {cx, ty, text_w, qh}, p.text_primary);
        g.text(Role::Secondary, detail, {cx, ty + qh, text_w, dh}, p.warning);
        // DS-04: geri donussuz silme onayi Performans "Evet"inden hem etiketle hem danger rengiyle ayrilir.
        float bx = side ? card.right() - pad - cbtn_w : cx;
        const float by = side ? card.y + (h - theme_.button_h) / 2 : card.y + pad + text_h + theme_.space(1);
        if (!vm.game_gate_blocked) bx += button(g, bx, by, Hit::CleanYes, yes, true, s, now, hits, true, true) + theme_.space(1);
        button(g, bx, by, Hit::CleanNo, no, false, s, now, hits);
    }

    // --- Calisiyor: gercek dosya sayisiyla ilerleme + Durdur (tek satir serit) ---
    if (cv.phase == cc::SessionPhase::Running) {
        const float iw = inner_w - 2 * pad;
        constexpr float kBarH = 6.f;
        const auto stop = t(StringId::CleanupStop);
        const float bw = button_rect(g, 0, 0, stop).w;
        const float text_w = std::max(iw - bw - theme_.space(3), 1.f);
        const float content_h = std::max(kBarH + theme_.space(0) + sl, theme_.button_h);
        const RectF card = st.next(pad + content_h + pad);
        draw_card(g, card);
        const float y0 = card.y + pad + (content_h - (kBarH + theme_.space(0) + sl)) / 2;
        const RectF bar{card.x + pad, y0, text_w, kBarH};
        g.fill_round(bar, kBarH / 2, p.surface_raised);
        if (hc) g.stroke_round(bar, kBarH / 2, p.border);
        if (cv.total > 0) {
            const float f = static_cast<float>(cv.done) / static_cast<float>(cv.total);
            if (f > 0) g.fill_round({bar.x, bar.y, std::max(kBarH, bar.w * std::min(f, 1.f)), bar.h}, kBarH / 2, p.accent);
        }
        const auto line = rep(rep(t(StringId::CleanupProgress), L"{done}", std::to_wstring(cv.done)), L"{total}", std::to_wstring(cv.total));
        g.text(Role::Secondary, line, {card.x + pad, y0 + kBarH + theme_.space(0), text_w, sl}, p.text_secondary, {Align::Leading, false, false, true});
        button(g, card.right() - pad - bw, card.y + (card.h - theme_.button_h) / 2, Hit::CleanStop, stop, false, s, now, hits);
    }

    // --- Sonuc (olculen): satirlar genis seritte iki sutuna dizilir ---
    if (cv.phase == cc::SessionPhase::Done && cv.report) {
        const float iw = inner_w - 2 * pad;
        const auto& r = *cv.report;
        std::vector<std::wstring> lines;
        std::vector<Color> colors;
        if (!r.categories.empty() || !r.standby.ran) {  // T15: yalniz bellek secildiyse "silinen boyut" satiri yazilmaz
            lines.push_back(rep(t(StringId::CleanupFreed), L"{size}", format_bytes(r.bytes_deleted(), tr)));
            colors.push_back(p.success);
        }
        if (r.standby.ran) {
            // Sayi yalniz iki okuma da basarili olduysa; aksi halde "olculemedi". Basarisizlikta sayi yok.
            if (!r.standby.ok) {
                lines.push_back(t(StringId::CleanupStandbyFailed));
                colors.push_back(p.warning);
            } else if (r.standby.before_bytes && r.standby.after_bytes) {
                lines.push_back(rep(rep(t(StringId::CleanupStandbyResult), L"{before}", format_bytes(*r.standby.before_bytes, tr)), L"{after}",
                                    format_bytes(*r.standby.after_bytes, tr)));
                colors.push_back(p.success);
            } else {
                lines.push_back(t(StringId::CleanupStandbyUnmeasured));
                colors.push_back(p.text_secondary);
            }
        }
        if (r.files_locked() > 0) {
            lines.push_back(rep(t(StringId::CleanupSkippedLocked), L"{files}", std::to_wstring(r.files_locked())));
            colors.push_back(p.text_secondary);
        }
        std::size_t links = 0;
        for (const auto& c : r.categories) links += c.reparse.files;
        if (links > 0) {
            lines.push_back(rep(t(StringId::CleanupSkippedLinks), L"{count}", std::to_wstring(links)));
            colors.push_back(p.text_secondary);
        }
        // Handoff v1: silinen/atlanan/hatali sayilari gercek; degismis dosya ayri yazilir.
        std::size_t changed = 0, other = 0;
        for (const auto& c : r.categories) changed += c.changed.files, other += c.denied.files + c.missing.files + c.other.files;
        if (changed > 0) {
            lines.push_back(rep(t(StringId::CleanupSkippedChanged), L"{count}", std::to_wstring(changed)));
            colors.push_back(p.text_secondary);
        }
        if (other > 0) {
            lines.push_back(rep(t(StringId::CleanupSkippedOther), L"{count}", std::to_wstring(other)));
            colors.push_back(p.text_secondary);
        }
        for (const auto& v : r.volumes)
            if (v.free_before && v.free_after) {
                lines.push_back(v.volume + L"  " + rep(rep(t(StringId::CleanupFreeSpace), L"{before}", format_bytes(*v.free_before, tr)), L"{after}",
                                                       format_bytes(*v.free_after, tr)));
                colors.push_back(p.text_secondary);
            }
        if (r.cancelled) {
            lines.push_back(t(StringId::CleanupCancelled));
            colors.push_back(p.warning);
        }
        constexpr float kColMinW = 360.f;
        const float col_gap = theme_.space(4);
        const std::size_t cols = lines.size() > 1 && iw >= 2 * kColMinW + col_gap ? 2 : 1;
        const float cw = (iw - col_gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        std::vector<float> hs, row_h;
        for (const auto& l : lines) hs.push_back(std::max(g.measure(Role::Body, l, cw).h, bl));
        float total = 0;
        for (std::size_t i = 0; i < lines.size(); i += cols) {
            float rh = 0;
            for (std::size_t k = i; k < std::min(lines.size(), i + cols); ++k) rh = std::max(rh, hs[k]);
            row_h.push_back(rh);
            total += rh;
        }
        const RectF card = st.next(theme_.space(1) + total + theme_.space(1));
        g.fill_round(card, theme_.radius_card, p.surface);
        g.stroke_round(card, theme_.radius_card, p.success.with_alpha(alpha_scale::kQuietOutline));
        float y = card.y + theme_.space(1);
        for (std::size_t i = 0, rr = 0; i < lines.size(); i += cols, ++rr) {
            for (std::size_t k = i; k < std::min(lines.size(), i + cols); ++k) {
                const RectF lr{card.x + pad + static_cast<float>(k - i) * (cw + col_gap), y, cw, hs[k]};
                g.text(Role::Body, lines[k], lr, colors[k]);
                hits.add_text(seq.take(), lr, lines[k]);
            }
            y += row_h[rr];
        }
    }

    if (cv.phase == cc::SessionPhase::Failed) {
        const auto msg = t(cv.volume_changed ? StringId::CleanupVolumeChanged : cv.maybe_partial ? StringId::CleanupMaybePartial : StringId::PerfPlanFailed);
        notice(msg, Role::Body, p.text_primary, p.error, true);
    }

    // ---------- Ust satir: halka | surucu haplari (+ uyarilar) ----------
    // Halka ortasi: secili boyut (temizlikten sonra silinen boyut); bilinmiyorsa "—".
    std::wstring center = L"—";
    if (done) center = format_bytes(cv.report->bytes_deleted(), tr);
    else if (cv.phase == cc::SessionPhase::Ready || cv.phase == cc::SessionPhase::Confirming) center = format_bytes(cv.selected_bytes, tr);
    const float ratio = done ? 1.f : (cv.phase == cc::SessionPhase::Ready || cv.phase == cc::SessionPhase::Confirming) ? cleanup_ring_ratio(cv.selected_bytes, ready_total) : 0.f;
    // Uyari satirlari (zorunlu): dogrulanamayan surucu, tarama iptali / butce siniri.
    std::vector<std::wstring> warns;
    std::wstring ident;
    if (cv.volume_id.known && cv.phase != cc::SessionPhase::Idle && !scanning) {
        ident = rep(rep(rep(t(StringId::CleanupVolumeIdentity), L"{serial}", serial_text(cv.volume_id.serial)), L"{fs}", cv.volume_id.fs.empty() ? L"?" : cv.volume_id.fs),
                    L"{cluster}", cv.volume_id.cluster ? format_bytes(cv.volume_id.cluster, tr) : L"?");
        if (!cv.volume_id.label.empty()) ident += L" · " + cv.volume_id.label;
    } else if (!cv.volume_id.known && !cv.volume_id.root.empty() && cv.phase == cc::SessionPhase::Ready) {
        warns.push_back(t(StringId::CleanupVolumeUnverified));
    }
    if (cv.scan_cancelled) warns.push_back(t(StringId::CleanupScanCancelled));
    else if (cv.scan_partial) warns.push_back(t(StringId::CleanupScanBudget));
    // Maket "Birim: ..." mono satiri (12px, ikincil renk): yalniz GERCEK alanlar - secili birimin envanter kaydindan tur +
    // disk modeli, taramadan sonra birim kimligi (seri / dosya sistemi / kume). Eksik alan atlanir; yer tutucu yok.
    std::wstring idline, idshort;  // idshort: ekranda gorunen kisa satir (tur + model); idline: UIA'da kimlik ayrintisiyla
    {
        std::wstring parts;
        const auto add = [&](const std::wstring& v) {
            if (v.empty()) return;
            if (!parts.empty()) parts += L" · ";
            parts += v;
        };
        if (!cv.volume.empty()) {
            for (const auto& a : vm.storage) {
                if (a.root.empty() || std::towupper(static_cast<wint_t>(static_cast<unsigned char>(a.root[0]))) != std::towupper(static_cast<wint_t>(cv.volume[0]))) continue;
                if (a.bus == core::inventory::BusKind::Nvme) add(t(StringId::StorageTypeNvme));
                else if (a.media == core::inventory::MediaKind::Ssd) add(t(StringId::StorageTypeSsd));
                else if (a.media == core::inventory::MediaKind::Hdd) add(t(StringId::StorageTypeHdd));
                add(widen(a.disk_model));
                break;
            }
        }
        idshort = parts;  // P12-B2b: birim kimligi / dosya sistemi / kume yalniz UIA'da ve halka HelpText'inde
        add(ident);
        idline = std::move(parts);
    }

    // Surucu haplari (EXT-CLN-01): "C: · 120 GB bos"; sigmayan hap alt satira sarilir. Ustlerinde kucuk "Temizlenecek
    // surucu" etiketi.
    const float d = kCleanupRingD;
    const float text_x0 = d + theme_.space(3);
    const float text_w = std::max(inner_w - text_x0, 1.f);
    const float chip_h = theme_.button_row_h;
    std::vector<std::wstring> vlabels;
    std::vector<RectF> vchips;  // metin blogunun sol ustune gore
    float vchips_h = 0;
    {
        float cx = 0, cy = 0;
        for (std::size_t k = 0; k < cv.volumes.size(); ++k) {
            std::wstring l = cv.volumes[k];
            if (k < cv.volume_free.size() && cv.volume_free[k])
                l = rep(rep(t(StringId::CleanupVolumeFree), L"{root}", l), L"{free}", format_bytes(*cv.volume_free[k], tr));
            const float w = std::min(text_w, std::max(theme_.touch_min, std::ceil(g.measure(Role::Body, l, kUnbounded, false).w) + 2 * theme_.space(2)));
            if (cx > 0 && cx + w > text_w) cx = 0, cy += chip_h + theme_.space(0);
            vchips.push_back({cx, cy, w, chip_h});
            cx += w + theme_.space(1);
            vlabels.push_back(std::move(l));
        }
        if (!vchips.empty()) vchips_h = cy + chip_h;
    }
    const auto vtitle = t(StringId::CleanupVolumeTitle);
    const float vtitle_h = vchips_h > 0 ? sl : 0.f;
    std::vector<float> warn_h;
    float warns_h = 0;
    for (const auto& w : warns) warn_h.push_back(g.measure(Role::Secondary, w, text_w).h), warns_h += warn_h.back();
    const float id_h = (vchips_h > 0 && !idshort.empty()) ? theme_.role(Role::Mono12).line : 0.f;
    const float block_h = (vchips_h > 0 ? vtitle_h + theme_.space(0) + vchips_h + (id_h > 0 ? theme_.space(0) + id_h : 0.f) : 0.f) + (warns_h > 0 ? (vchips_h > 0 ? theme_.space(1) : 0.f) + warns_h : 0.f);
    const float top_h = std::max(d, block_h);
    // Henuz taranmamis (bos / taraniyor / basarisiz) ve yaninda surucu hapi ya da uyari yoksa bos "—" halkasi cizilmez;
    // kart baslik seridinde biter (ozet ve Onizleme dugmesi baslikta).
    const bool unscanned = cv.phase == cc::SessionPhase::Idle || cv.phase == cc::SessionPhase::Scanning || cv.phase == cc::SessionPhase::Failed;
    const bool show_top = !(unscanned && center == L"—" && vchips.empty() && warns.empty());
    const RectF top = show_top ? st.next(top_h) : RectF{};
    if (show_top) {
        // Halka: secim degisince yeni orana doner (motion.cleanup_ring; hareket kapali / oyun acik / test surerken aninda).
        if (s.cleanup_ring_to < 0.f) {
            s.cleanup_ring = Tween(ratio);
            s.cleanup_ring_to = ratio;
        } else if (std::fabs(ratio - s.cleanup_ring_to) > 1e-4f) {
            s.cleanup_ring.retarget(ratio, now, effective_ms(theme_.motion.cleanup_ring, s.reduced_motion || s.motion_paused || vm.game_gate_blocked));
            s.cleanup_ring_to = ratio;
        }
        const RectF ring{top.x, top.y + (top_h - d) / 2, d, d};
        const float rcx = ring.x + d / 2, rcy = ring.y + d / 2;
        constexpr float kRingThick = 7.f;
        const float v = std::clamp(s.cleanup_ring.value(now, theme_.easing), 0.f, 1.f);
        const Color fill = done ? p.success : p.cyan;
        if (top.bottom() >= s.view_top && top.y <= s.view_bottom) {
            if (hc) {
                constexpr float kEdge = 1.f;
                g.arc(rcx, rcy, d / 2 - kEdge / 2, kEdge, 0.f, 360.f, p.border);
                g.arc(rcx, rcy, d / 2 - kRingThick + kEdge / 2, kEdge, 0.f, 360.f, p.border);
                g.ring(rcx, rcy, (d - kRingThick) / 2, kRingThick - 2 * kEdge, v, p.background, fill);
            } else {
                g.ring(rcx, rcy, (d - kRingThick) / 2, kRingThick, v, p.surface_raised, fill);
            }
            const float cw = d - 2 * kRingThick - theme_.space(1);
            g.text(Role::Body, fit_text(g, Role::Body, center, cw), {rcx - cw / 2, ring.y, cw, d}, center == L"—" ? p.text_secondary : p.text_primary,
                   {Align::Center, false, true, true});
        }
        // UIA: halka ve sayim tek salt okunur metin; kaldirilan aciklamalar (ayrilan alan notu, surucu notu, birim kimligi,
        // geri alinamaz cumlesi) HelpText'te.
        const Hit ring_id = seq.take();
        hits.add_text(ring_id, ring, a11y_join(t(StringId::CleanupRingLabel) + L" " + center, head));
        {
            std::wstring help;
            if (cv.phase == cc::SessionPhase::Ready && cv.selected_bytes > 0)
                help = rep(t(StringId::CleanupAllocNote), L"{alloc}", format_bytes(cv.selected_allocated, tr));
            if (!cv.volumes.empty()) help = a11y_join(std::move(help), t(StringId::CleanupVolumeNote));
            help = a11y_join(std::move(help), ident);
            help = a11y_join(std::move(help), t(StringId::CleanupIrreversible));
            hits.meta[ring_id].help = std::move(help);
        }
        const float tx = top.x + text_x0;
        float ty = top.y + (top_h - block_h) / 2;
        if (vchips_h > 0) {
            g.text(Role::Secondary, fit_text(g, Role::Secondary, vtitle, text_w), {tx, ty, text_w, vtitle_h}, p.text_secondary, {Align::Leading, false, true});
            ty += vtitle_h + theme_.space(0);
            for (std::size_t k = 0; k < vchips.size(); ++k) {
                const Hit id = hit_of(HitKind::CleanVolume, k);
                const bool selected = cv.volumes[k] == cv.volume;
                const RectF r{tx + vchips[k].x, ty + vchips[k].y, vchips[k].w, vchips[k].h};
                const float rad = r.h / 2;
                g.fill_round(r, rad, selected ? mix(p.surface_raised, p.cyan, alpha_scale::kSelectionTint) : p.surface_raised);
                g.stroke_round(r, rad, selected ? p.cyan : p.border, selected ? 2.f : 1.f);
                g.text(Role::Body, fit_text(g, Role::Body, vlabels[k], std::max(r.w - 2 * theme_.space(1), 1.f)), r,
                       busy && !selected ? p.text_secondary.with_alpha(alpha_scale::kBusyText) : (selected ? p.text_primary : p.text_secondary),
                       {Align::Center, false, true});
                // DS-05: is surerken haplar devre disi ama UIA'da kalir (secili birim okunur); Tab'da atlanir.
                if (s.focus_visible && s.focus == id) focus_ring(g, r, rad, hits);
                hits.add(id, r, vtitle + L" " + vlabels[k], AccRole::RadioButton, selected, -1, !busy);
            }
            if (!busy) s.cleanup_volumes = vchips.size();
            ty += vchips_h;
            if (id_h > 0) {
                ty += theme_.space(0);
                const RectF ir{tx, ty, text_w, id_h};
                g.text(Role::Mono12, fit_text(g, Role::Mono12, idshort, text_w), ir, p.text_secondary, {Align::Leading, false, true});
                hits.add_text(seq.take(), ir, idline);
                ty += id_h;
            }
        }
        if (warns_h > 0) {
            if (vchips_h > 0) ty += theme_.space(1);
            for (std::size_t i = 0; i < warns.size(); ++i) {
                const RectF wr{tx, ty, text_w, warn_h[i]};
                g.text(Role::Secondary, warns[i], wr, ensure_contrast(p.warning, p.surface, p.text_primary, 4.5));
                hits.add_text(seq.take(), wr, warns[i]);
                ty += warn_h[i];
            }
        }
    }

    // ---------- Kategori kutulari: iki sutun (anahtar · ad · etiket · boyut · dosya listesi) ----------
    const std::size_t n = cv.rows.size();
    if (n > 0) {
        struct Lay {
            std::wstring title, desc, right;
            Color right_c;
            std::vector<std::pair<std::wstring, ChipTone>> chips;  // Yonetici / geri gelmez (Geri Donusum Kutusu)
            float chips_w = 0, right_w = 0;
            bool has_btn = false, open = false, absent = false;
        };
        std::vector<Lay> lay;
        bool any_btn = false;
        for (const auto& r : cv.rows) {
            const auto& inf = cc::info(r.category);
            Lay l{tk(inf.title_key), tk(inf.desc_key), {}, p.text_secondary};
            switch (r.status) {
                case cc::PreviewStatus::Ready:
                    l.right = format_bytes(r.bytes, tr);
                    l.right_c = r.bytes > 0 ? (r.selected ? ensure_contrast(p.cyan, p.surface, p.text_primary, 4.5) : p.text_primary) : p.text_secondary;
                    break;
                case cc::PreviewStatus::NotPresent:
                    l.right = t(StringId::CleanupAbsentShort);
                    l.absent = true;
                    break;
                case cc::PreviewStatus::OtherVolume:
                    l.right = t(StringId::CleanupAbsentShort);
                    l.absent = true;
                    break;
                case cc::PreviewStatus::NeedsAdmin: l.right = L"—"; break;
                case cc::PreviewStatus::NotScanned: l.right = L"—", l.right_c = p.warning; break;
                case cc::PreviewStatus::Error: l.right = L"—"; break;
            }
            if (r.partial && r.status == cc::PreviewStatus::Ready) {
                l.right += L" · " + t(StringId::CleanupPartial);  // EXT-CLN: yarim tarama acikca yazilir
                l.right_c = p.warning;
            }
            if (r.needs_admin || r.admin_gate || r.category == cc::CleanupCategory::StandbyMemory) l.chips.push_back({t(StringId::CleanupAdminChip), ChipTone::Warn});
            if (r.category == cc::CleanupCategory::RecycleBin) l.chips.push_back({t(StringId::ChipNoReturn), ChipTone::Danger});
            for (const auto& c : l.chips) l.chips_w += chip_width(g, c.first) + theme_.space(0);
            l.right_w = std::ceil(g.measure(Role::Secondary, l.right, kUnbounded, false, true).w) + 1.f;
            l.has_btn = editable && r.status == cc::PreviewStatus::Ready && r.files > 0 && r.category != cc::CleanupCategory::RecycleBin;
            l.open = l.has_btn && cv.files_open && *cv.files_open == r.category;
            any_btn = any_btn || l.has_btn;
            lay.push_back(std::move(l));
        }
        const float col_gap = theme_.space(2);
        const int cols = systools_columns(inner_w, col_gap, 2, kSysToolsTileMinW);
        const std::size_t ucols = static_cast<std::size_t>(cols);
        const float cw = (inner_w - col_gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        const float tile_h = theme_.row_h + theme_.space(1);
        const float tp = theme_.space(2);  // kutu ic boslugu
        const float tog_w = theme_.toggle_w, tog_h = theme_.toggle_h;
        const float tog_dx = tog_w + theme_.space(2);
        const float exp_w = theme_.button_row_h;
        const float exp_col = any_btn ? theme_.space(1) + exp_w : 0.f;  // dosya listesi dugmesi sutunu (tum kutularda ayrilir)
        // Spec 2026-11-14 B12: uzun liste kapaliyken ilk kCleanupCollapsedRows satir gorunur; secili ya da dosya listesi
        // acik kategori hic gizlenmez. Gizlenenler "+N kategori daha" dugmesinin HelpText'inde okunur. Odakta / imlec
        // altindaki anahtar da gizlenmez: secimi kaldirilan kutu ayni tiklamada kaybolmaz (odak kaybi yok).
        std::size_t last_needed = n;
        for (std::size_t i = 0; i < n; ++i) {
            const Hit tid = hit_of(HitKind::CleanToggle, i), fid = hit_of(HitKind::CleanFiles, i);
            if (cv.rows[i].selected || lay[i].open || s.focus == tid || s.hover == tid || s.focus == fid || s.hover == fid) last_needed = i;
        }
        (void)last_needed;
        const std::size_t shown_n = n;  // BOOST maketi: tum kategoriler gorunur ("daha az goster" yok)
        const bool collapsible = false;

        // Acik dosya listesi kendi izgara satirinin altinda acilir (EXT-CLN-03).
        std::vector<std::wstring> flist;
        std::vector<float> flist_h;
        float files_h = 0;
        std::size_t open_row = n;
        const std::size_t fcols = inner_w >= 760.f ? 2 : 1;
        const float f_pad = theme_.space(1);
        const float f_gap = theme_.space(3);
        const float fw = (inner_w - 2 * f_pad - f_gap * static_cast<float>(fcols - 1)) / static_cast<float>(fcols);
        std::wstring f_more;
        float f_more_h = 0;
        for (std::size_t i = 0; i < shown_n; ++i)
            if (lay[i].open) {
                open_row = i / ucols;
                for (const auto& f : cv.files) flist.push_back(f.relative + L"  ·  " + format_bytes(f.bytes, tr));
                if (cv.files_total > cv.files.size())
                    f_more = rep(rep(t(StringId::CleanupFilesMore), L"{count}", std::to_wstring(cv.files_total - cv.files.size())), L"{shown}",
                                 std::to_wstring(cv.files.size()));
                for (const auto& line : flist) flist_h.push_back(g.measure(Role::Secondary, line, fw).h);
                for (std::size_t k = 0; k < flist.size(); k += fcols) {
                    float rh = 0;
                    for (std::size_t q = k; q < std::min(flist.size(), k + fcols); ++q) rh = std::max(rh, flist_h[q]);
                    files_h += rh;
                }
                if (!f_more.empty()) f_more_h = g.measure(Role::Secondary, f_more, inner_w - 2 * f_pad).h;
                files_h += f_more_h + 2 * f_pad;  // kutu ic boslugu
            }
        const std::size_t grid_rows = (shown_n + ucols - 1) / ucols;
        const float grid_h = static_cast<float>(grid_rows) * tile_h + static_cast<float>(grid_rows - 1) * col_gap + (files_h > 0 ? col_gap + files_h : 0.f);
        const RectF grid = st.next(grid_h);
        float y = grid.y;
        for (std::size_t i = 0, gr = 0; i < shown_n; i += ucols, ++gr) {
            const bool visible = y + tile_h >= s.view_top && y <= s.view_bottom;
            for (std::size_t k = i; k < std::min(shown_n, i + ucols); ++k) {
                const auto& r = cv.rows[k];
                const auto& l = lay[k];
                const RectF tile{x + static_cast<float>(k - i) * (cw + col_gap), y, cw, tile_h};
                const Hit id = hit_of(HitKind::CleanToggle, k);
                // Yonetici kategorisi henuz yukseltilmis taranmadiysa tiklanabilir: UAC istemci tetikler.
                const bool can = editable && (r.selectable || r.admin_gate) && !cv.admin_pending;
                s.cleanup_toggles[k] = can;
                const RectF tg{tile.x + tp, tile.y + (tile_h - tog_h) / 2, tog_w, tog_h};
                // UIA adi: ad + aciklama (spec §5); boyut/durum HelpText'te.
                const std::wstring name = a11y_join(l.title, l.desc);
                if (visible) {
                    const bool sel = r.selected && r.status == cc::PreviewStatus::Ready;
                    g.fill_round(tile, theme_.radius_control, l.absent ? p.background : p.surface);
                    g.stroke_round(tile, theme_.radius_control, sel ? p.cyan : p.border, sel ? 1.5f : 1.f);
                    small_toggle(g, tg, r.selected, can, id, name, s, now, hits);
                } else {
                    toggle_hit(tg, r.selected, can, id, name, hits);  // RDY-10: gorunmeyen kutunun anahtari cizilmeden kaydedilir
                }
                const float tx = tile.x + tp + tog_dx;
                const float right_edge = tile.right() - tp - exp_col;
                const float title_w = std::max(right_edge - tx - (l.chips_w > 0 ? theme_.space(1) + l.chips_w : 0.f) - theme_.space(2) - l.right_w, 1.f);
                if (id != Hit::None) {
                    // Isabet kutusu adi da kapsar (ada tiklamak anahtari cevirir).
                    auto& box = hits.items.back().second;
                    box.w = std::min(theme_.sidebar_max, tog_dx + std::min(title_w, std::ceil(g.measure(Role::Body, l.title, kUnbounded, false).w) + 1.f) + 4.f);
                    std::wstring help = l.right;
                    if (r.status == cc::PreviewStatus::Ready)
                        help = rep(rep(t(StringId::CleanupPreviewSummary), L"{files}", std::to_wstring(r.files)), L"{size}", format_bytes(r.bytes, tr));
                    else if (r.status == cc::PreviewStatus::NotPresent) help = t(StringId::CleanupNotPresent);
                    else if (r.status == cc::PreviewStatus::OtherVolume) help = t(StringId::CleanupOtherVolume);
                    else if (r.status == cc::PreviewStatus::NeedsAdmin) help = t(StringId::CleanupAdminNeeded);
                    else if (r.status == cc::PreviewStatus::NotScanned) help = t(StringId::CleanupNotScanned);
                    else if (r.status == cc::PreviewStatus::Error) help = t(StringId::SysUnknown);
                    if (r.partial && r.status == cc::PreviewStatus::Ready) help += L" · " + t(StringId::CleanupPartial);
                    hits.meta[id].help = std::move(help);
                }
                if (l.has_btn) {
                    expander(g, {tile.right() - tp - exp_w, tile.y + (tile_h - exp_w) / 2, exp_w, exp_w}, hit_of(HitKind::CleanFiles, k), l.open,
                             t(l.open ? StringId::CleanupHideFiles : StringId::CleanupShowFiles) + L": " + l.title, s, now, hits, visible);
                    s.cleanup_file_buttons[k] = true;
                }
                if (!visible) continue;
                const Color name_c = l.absent ? p.text_secondary : p.text_primary;
                const std::wstring shown = fit_text(g, Role::Body, l.title, title_w);
                const float sw = std::min(std::ceil(g.measure(Role::Body, shown, kUnbounded, false).w) + 1.f, title_w);
                g.text(Role::Body, shown, {tx, tile.y, sw, tile_h}, name_c, {Align::Leading, false, true});
                float chx = tx + sw + theme_.space(1);
                for (const auto& [ct, tone] : l.chips) chx += chip(g, Point{chx, tile.y + (tile_h - sl) / 2}, ct, tone) + theme_.space(0);
                g.text(Role::Secondary, l.right, {right_edge - l.right_w, tile.y, l.right_w, tile_h}, l.right_c, {Align::Trailing, false, true, true});
            }
            y += tile_h;
            if (gr == open_row && files_h > 0) {
                y += col_gap;
                // EXT-CLN-03: silmeden once dosya listesi (en buyukler once); yukseltilmis yuzeyde, genis alanda iki sutun.
                const RectF box{x, y, inner_w, files_h};
                if (box.bottom() >= s.view_top && box.y <= s.view_bottom) {
                    g.fill_round(box, theme_.radius_control, p.surface_raised);
                    if (hc) g.stroke_round(box, theme_.radius_control, p.border);
                    float fy = box.y + f_pad;
                    for (std::size_t k = 0; k < flist.size(); k += fcols) {
                        float frh = 0;
                        for (std::size_t q = k; q < std::min(flist.size(), k + fcols); ++q) {
                            g.text(Role::Secondary, flist[q], {box.x + f_pad + static_cast<float>(q - k) * (fw + f_gap), fy, fw, flist_h[q]}, p.text_primary);
                            frh = std::max(frh, flist_h[q]);
                        }
                        fy += frh;
                    }
                    if (!f_more.empty()) g.text(Role::Secondary, f_more, {box.x + f_pad, fy, inner_w - 2 * f_pad, f_more_h}, p.text_secondary);
                }
                y += files_h;
            }
            if (gr + 1 < grid_rows) y += col_gap;
        }
        // "+N kategori daha" / "Daha az goster": izgaranin altinda, sola yasli ikincil dugme (UIA Expander; Selected = acik).
        // Gizli kategorilerin adlari HelpText'te (bilgi kaybolmaz).
        if (collapsible) {
            const std::wstring label = s.cleanup_more_open ? t(StringId::SystoolsCleanupLess)
                                                           : rep(t(StringId::SystoolsCleanupMore), L"{n}", std::to_wstring(n - shown_n));
            const RectF mr = button_rect(g, x, st.next(theme_.button_row_h).y, label, true);
            if (mr.bottom() >= s.view_top && mr.y <= s.view_bottom) button_face(g, mr, Hit::CleanMore, label, ButtonKind::Secondary, true, s, now, hits);
            hits.add(Hit::CleanMore, mr, label, AccRole::Expander, s.cleanup_more_open);
            std::wstring help;
            for (std::size_t k = shown_n; k < n; ++k) help = a11y_join(std::move(help), lay[k].title);
            if (!help.empty()) hits.meta[Hit::CleanMore].help = std::move(help);
            s.cleanup_more = true;
        }
    }
    if (st.cursor <= 0) return line_y - area.y;
    section_card_rule(g, area, line_y);
    return st.area.y + st.cursor - st.gap + pad - area.y;
}

}  // namespace ef::ui
