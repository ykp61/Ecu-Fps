// Panel'de bes canli kutucuk — CPU, RAM, GPU, DISK, AG — tek sirada esit sutun (kutucuk < 170 DIP kaliyorsa 3 + 2, o
// da darsa tek sutun). Cizim spec 2026-11-11 §4'teki gibidir ve degismez: 32x32 ikon kutusu (ikon rengi usage_color,
// zemin o rengin %12'si, >= %75'te parlama), etiket, sagda kullanim kelimesi, deger (Metric 24), 12 parcali LED cubuk.
// Spec 2026-11-12 B.2: altta iki satir — parca adi (birincil renk; envanterden) ve canli ayrinti (ikincil renk). Yalniz
// gercekten okunan parca yazilir; okunamayan atlanir. Uzun ad "…" ile kisalir, tam ad UIA adindadir. Olculemeyen deger
// "—" + nedendir; sayi uydurulmaz. Ornekleme yalniz pencere gorunurken 1 Hz (App). Spec F: LED ve sayi yeni degere 250
// ms'de gecer; yeni ornek ve gecis yalniz kutucuk bolgesini yeniden cizer (paint_tiles_region). Yuksek kontrastta
// rampa/parlama yok: sistem renkleri + kelime; yanan parca dolu, sonuk parca cerceve.
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "core/measure/view.h"
#include "ui/shell/screen.h"
#include "ui/theme/ramp.h"

namespace ef::ui {

using core::i18n::StringId;

namespace {
std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::wstring replace_all(std::wstring s, std::wstring_view key, const std::wstring& val) {
    for (std::size_t pos = s.find(key); pos != std::wstring::npos; pos = s.find(key, pos + val.size()))
        s.replace(pos, key.size(), val);
    return s;
}
std::optional<float> pct_of(const contracts::Measurement& m) {
    if (!m.has_value()) return std::nullopt;
    const auto v = core::metrics::clamp_pct(*m.value());
    if (!v) return std::nullopt;
    return static_cast<float>(*v);
}
// Ag kutucugu doluluk: indirme / baglanti hizi (%). Biri yoksa veya baglanti bilinmiyorsa yok.
std::optional<float> net_pct_of(const core::metrics::LiveSnapshot& l) {
    if (!l.net_down_mbps.has_value() || !l.net_link_mbps.has_value()) return std::nullopt;
    const auto f = core::metrics::link_fraction(*l.net_down_mbps.value(), *l.net_link_mbps.value());
    if (!f) return std::nullopt;
    return static_cast<float>(*f * 100.0);
}
StringId reason_id(const contracts::Measurement& m) {
    switch (m.missing_reason()) {
        case contracts::MissingReason::NotSupported: return StringId::LiveReasonNotSupported;
        case contracts::MissingReason::SourceError:
        case contracts::MissingReason::Invalid: return StringId::LiveReasonSourceError;
        default: return StringId::LiveReasonNotSampled;
    }
}
// Bir ondalikli metin; printf yarim degerleri cift sayiya yuvarlar (9,25 -> 9,2), gosterim yarimi yukari yuvarlar.
std::wstring one_decimal(double v, bool turkish) {
    const double tenths = std::floor(v * 10.0 + 0.5);
    const auto whole = static_cast<long long>(tenths) / 10;
    const auto frac = static_cast<long long>(tenths) % 10;
    return std::to_wstring(whole) + (turkish ? L"," : L".") + std::to_wstring(frac);
}
}  // namespace

std::wstring net_mbs_text(double mbit_per_s, bool turkish) {
    const auto v = core::metrics::mbit_to_mbyte_per_s(mbit_per_s);
    if (!v) return L"—";
    return one_decimal(*v, turkish);
}

LinkText net_link_text(double mbit_per_s, bool turkish) {
    if (!std::isfinite(mbit_per_s) || !(mbit_per_s > 0)) return {};
    if (mbit_per_s < 999.5) return {std::to_wstring(static_cast<long long>(std::floor(mbit_per_s + 0.5))), false};
    // Gbps: tam sayiya yuvarlaniyorsa ondaliksiz ("1", "10"), degilse bir ondalik ("2,5").
    const double gb = mbit_per_s / 1000.0;
    const double tenths = std::floor(gb * 10.0 + 0.5);
    if (static_cast<long long>(tenths) % 10 == 0) return {std::to_wstring(static_cast<long long>(tenths) / 10), true};
    return {one_decimal(gb, turkish), true};
}

void Screen::live_tiles(Graphics& g, Stack& st, const RectF& body, UiState& s, const ViewModel& vm, std::int64_t now,
                        HitMap& hits, StatusSeq& seq, TilePass pass) {
    const auto& p = pal();
    const auto& lv = vm.live;
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    const bool dark = pal_ == &theme_.dark;  // yuksek kontrastta rampa/gradyan yok: sistem renkleri + kelime
    const float gap = theme_.card_gap;
    constexpr float kIconBox = 32.f;   // ikon kutusu (DIP)
    constexpr float kIconInset = 6.f;  // kutu icinde ikon payi (20 DIP ikon)
    constexpr float kLedH = 7.f;       // LED parca yuksekligi (maket h-[7px])
    constexpr float kSparkH = 22.f;    // ince grafik yuksekligi (maket h-[22px])
    constexpr float kLedGap = 3.f;     // LED parcalari arasi
    constexpr float kLedRadius = 2.f;
    constexpr float kUnbounded = 10000.f;
    constexpr float kRegionPad = 2.f;  // hizli yol bolgesinin kutucuk satirinin disina tasan payi (yalniz zemin)
    // BOOST maketi (Tailwind px = DIP): kart p-3 + 1 DIP kenar (13); baslik 32.5, deger satiri (mt 8.5) 24px/28, LED
    // (mt 10) 7, ince grafik (mt 6) 22, alt blok (mt 8): cizgi 1 + pt 6 + ad 17 + ayrinti 16, alt pay 13 -> 188 DIP.
    constexpr float kTilePad = 13.f, kIconRadius = 6.f, kBadgePadX = 6.f, kSparkW = 120.f;
    const float idx_h = theme_.role(Role::Mono9).line, caps_h = theme_.role(Role::Caps11).line;
    const float small_h = theme_.role(Role::Small11).line, mono10_h = theme_.role(Role::Mono10).line;
    const float num_h = theme_.role(Role::Num24).line, name_h = theme_.role(Role::Medium12).line;
    const float head_h = std::max(32.5f, idx_h + 3.f + caps_h);
    // + ayrinti satir(lar)i (detail_h; tek satirda small_h)
    const float base_h = kTilePad + head_h + 8.5f + num_h + 10.f + kLedH + 6.f + kSparkH + 8.f + 7.f + (name_h - 1.f) - 0.5f + kTilePad;
    // Spec 2026-11-12 F: LED parcalari ve sayi yeni degere motion.value_ease (250 ms) icinde gecer. Hareket kapali,
    // pencere gorunmuyor, oyun acik, olcum suruyor ya da pilde: aninda (zamanlayici kare istemez).
    const int dur = effective_ms(theme_.motion.value_ease, s.reduced_motion || s.motion_paused || vm.game_gate_blocked);
    const Color track = ensure_contrast(p.surface_raised, p.surface, p.text_secondary, 3.0);

    const auto num = [&](double v, int dec) { return widen(core::measure::format_number(v, dec, tr)); };
    const auto pct_text = [&](double v) {
        const auto n = num(v, 0);
        return tr ? L"%" + n : n + L"%";
    };
    const auto join = [](std::wstring a, const std::wstring& b) {
        if (b.empty()) return a;
        return a.empty() ? b : a + L" · " + b;
    };

    struct Tile {
        LiveTile slot;
        StringId label;
        Icon icon;
        const contracts::Measurement* m;  // buyuk sayinin kaynagi
        std::optional<float> pct;         // rampa / LED / kelime yuzdesi (ag: indirme / baglanti)
        std::wstring name;                // 1. satir: parca adi (envanter); bos = okunamadi ("—")
        std::wstring detail;              // 2. satir: canli ayrinti (deger varken); bos = "—"
        std::wstring detail_a11y;         // UIA adinda ayrintinin yerine (ok isaretsiz, kaynakli); bos = detail
        bool name_loading = false;        // envanter henuz okunuyor: ad yerine iskelet
        std::vector<VolumeFill> fills;    // DISK: birim dolulugu (ayrinti genislige gore kisalir: ilk birimler + "+N")
    };

    // Ad ve ayrinti satirlari (spec B.2): yalniz envanterin / canli ornegin gercekten okudugu parcalar; okunamayan
    // parca yazilmaz, uydurma deger uretilmez.
    const SysCell cpu_cell = sys_cell_split(vm.cpu);
    std::wstring cpu_detail;
    {
        if (const auto cores = cpu_core_count(cpu_cell.detail)) cpu_detail = replace_all(t(StringId::LiveCpuCores), L"{n}", std::to_wstring(*cores));
        if (const auto v = pct_of(lv.cpu_max_core_pct)) cpu_detail = join(std::move(cpu_detail), replace_all(t(StringId::LiveCpuBusiest), L"{v}", pct_text(*v)));
    }
    std::wstring ram_detail;
    if (lv.ram_pct.has_value() && lv.ram_total_mib > 0)
        ram_detail = replace_all(t(StringId::LiveRamInUse), L"{used}", num(static_cast<double>(lv.ram_used_mib) / 1024.0, 1));
    // GPU adi: envanterin ilk karti (ad + bellek). Ayrinti: sicaklik (EF-X24: yalniz taze surucu okumasi; kaynak UIA
    // adinda) + birincil ekranin cozunurlugu ve yenileme hizi.
    // EF-X24.02: hibrit sistemde ad ve sicaklik yalniz olcumu ureten karta yazilir (pick_live_gpu); envanter kimligi
    // yoksa (eski model / bos) eski davranis: ilk kart.
    bool temp_ok = lv.gpu_temp_c.has_value();
    std::wstring gpu_name;
    if (vm.gpu_cards.empty()) {
        gpu_name = widen(std::string_view(vm.gpu).substr(0, vm.gpu.find('\n')));
    } else {
        const auto pick = core::metrics::pick_live_gpu(vm.gpu_cards, lv.gpu_adapters, lv.gpu_temp_device, temp_ok);
        if (pick.card) gpu_name = widen(vm.gpu_cards[*pick.card].label);
        temp_ok = pick.show_temp;
    }
    std::wstring gpu_detail, gpu_a11y;
    {
        const std::wstring mode = widen(display_mode_text(vm.display));
        if (temp_ok) {
            const auto tv = num(*lv.gpu_temp_c.value(), 0);
            gpu_detail = replace_all(t(StringId::LiveGpuTempLine), L"{t}", tv);
            gpu_a11y = join(replace_all(t(StringId::LiveGpuTemp), L"{temp}", tv), mode);
        }
        gpu_detail = join(std::move(gpu_detail), mode);
    }
    // DISK adi: sistem diski (ilk birim) — model (yoksa tur) + toplam boyut. Ayrinti: birimlerin dolulugu, tek satir.
    std::wstring disk_name, disk_a11y;
    std::vector<VolumeFill> fills = volume_fills(lv.volumes);
    {
        namespace ci = core::inventory;
        if (!vm.storage_loading && !vm.storage.empty()) {
            const auto& a = vm.storage.front();
            if (!a.disk_model.empty()) disk_name = widen(a.disk_model);
            else if (a.bus == ci::BusKind::Nvme) disk_name = t(StringId::StorageTypeNvme);
            else if (a.media == ci::MediaKind::Ssd) disk_name = t(StringId::StorageTypeSsd);
            else if (a.media == ci::MediaKind::Hdd) disk_name = t(StringId::StorageTypeHdd);
            if (a.total_gib) disk_name = join(std::move(disk_name), replace_all(t(StringId::SysStorageSize), L"{v}", std::to_wstring(*a.total_gib)));
        }
        if (fills.empty() && !vm.storage_loading) {  // canli ornek birim tasimiyorsa envanterin okudugu birimler
            std::vector<core::metrics::VolumeUse> inv;
            for (const auto& a : vm.storage) inv.push_back({a.root, a.total_gib, a.free_gib});
            fills = volume_fills(inv);
        }
        if (!fills.empty()) disk_a11y = replace_all(t(StringId::LiveVolFillA11y), L"{list}", volume_fill_list(fills, fills.size(), tr));
        // Okuma / yazma hizi kutucukta gorunmez; ekran okuyucu adinda kalir (yalniz olculduyse).
        if (lv.disk_read_mbs.has_value() && lv.disk_write_mbs.has_value())
            disk_a11y = a11y_join(std::move(disk_a11y), replace_all(replace_all(t(StringId::LiveDiskRwA11y), L"{r}", num(*lv.disk_read_mbs.value(), 1)),
                                                                    L"{w}", num(*lv.disk_write_mbs.value(), 1)));
    }
    std::wstring net_name, net_detail, net_a11y;
    {
        if (lv.net_link_mbps.has_value()) {
            const auto lt = net_link_text(*lv.net_link_mbps.value(), tr);
            if (!lt.value.empty())
                net_name = replace_all(t(StringId::LiveNetLinkName), L"{link}",
                                       replace_all(t(lt.gbps ? StringId::LiveNetLinkGbps : StringId::LiveNetLinkMbps), L"{v}", lt.value));
        }
        if (lv.net_up_mbps.has_value()) {
            const auto up = net_mbs_text(*lv.net_up_mbps.value(), tr);
            net_detail = replace_all(t(StringId::LiveNetUpLine), L"{up}", up);
            net_a11y = replace_all(t(StringId::LiveNetUpA11y), L"{up}", up);
        }
    }
    const bool sl = vm.system_loading;
    const Tile tiles[kLiveTileCount] = {
        {LiveTile::Cpu, StringId::LiveCpu, Icon::Cpu, &lv.cpu_pct, pct_of(lv.cpu_pct), widen(cpu_cell.main), cpu_detail, {}, sl, {}},
        {LiveTile::Ram, StringId::LiveRam, Icon::Ram, &lv.ram_pct, pct_of(lv.ram_pct), widen(ram_tile_name(vm.ram)), ram_detail, {}, sl, {}},
        {LiveTile::Gpu, StringId::LiveGpu, Icon::Gpu, &lv.gpu_pct, pct_of(lv.gpu_pct), gpu_name, gpu_detail, gpu_a11y, sl, {}},
        {LiveTile::Disk, StringId::LiveDisk, Icon::Disk, &lv.disk_pct, pct_of(lv.disk_pct), disk_name, {}, disk_a11y, vm.storage_loading, fills},
        {LiveTile::Net, StringId::LiveNet, Icon::Net, &lv.net_down_mbps, net_pct_of(lv), net_name, net_detail, net_a11y, false, {}},
    };
    const auto has_value = [](const Tile& tl) { return tl.m->has_value() && std::isfinite(*tl.m->value()) && *tl.m->value() >= 0; };
    const std::wstring dash = L"—";
    // Gorunen ayrinti: deger varken canli ayrinti (tek satir; sigmazsa "…", DISK'te ilk birimler + "+N"), yokken neden
    // metni (sarabilir).
    const auto detail_for = [&](const Tile& tl, float w) -> std::wstring {
        if (!has_value(tl)) return t(reason_id(*tl.m));
        if (!tl.fills.empty()) {
            const auto line = [&](std::size_t shown) { return replace_all(t(StringId::LiveVolFill), L"{list}", volume_fill_list(tl.fills, shown, tr)); };
            for (std::size_t shown = tl.fills.size(); shown > 1; --shown)
                if (auto text = line(shown); g.measure(Role::Small11, text, kUnbounded, false).w <= w) return text;
            return fit_text(g, Role::Small11, line(1), w);
        }
        return tl.detail.empty() ? dash : fit_text(g, Role::Small11, tl.detail, w);
    };

    const auto draw_tile = [&](const Tile& tl, const RectF& r, float detail_h, const std::wstring& detail) {
        const std::size_t slot = static_cast<std::size_t>(tl.slot);
        const bool is_net = tl.slot == LiveTile::Net;
        const bool has = has_value(tl);
        // Sayi gecisi: hedef degisince motion.value_ease (hareket kapaliyken aninda). Eksikten donuste gecis yok.
        auto& tw = s.live_value[slot];
        float shown = 0;
        if (has) {
            const float target = is_net ? static_cast<float>(*tl.m->value()) : tl.pct.value_or(0.f);
            if (!s.live_seen[slot]) tw.retarget(target, now, 0);
            else if (std::fabs(target - tw.target()) > 1e-3f) tw.retarget(target, now, dur);
            s.live_seen[slot] = true;
            shown = tw.value(now, theme_.easing);
        } else {
            s.live_seen[slot] = false;
        }
        const bool easing = has && tw.active(now);
        // Net: gosterilen yuzde gecisli indirme degerinden (baglanti sabit); diger kutucuklar dogrudan.
        std::optional<float> pct;
        if (has && tl.pct) {
            if (is_net && lv.net_link_mbps.has_value()) {
                const auto f = core::metrics::link_fraction(shown, *lv.net_link_mbps.value());
                if (f) pct = static_cast<float>(*f * 100.0);
            } else if (!is_net) {
                pct = std::clamp(shown, 0.f, 100.f);
            }
        }
        // Tepe izi (update_live_peaks; yalniz modelden: tam kare ve bolge cizimi ayni sonucu verir). Tepe hedef
        // degerle karsilastirilir (gecis ara degeriyle degil): yukselis gecisinde hedefin ust bolutu bar gelmeden
        // "hayalet" tepe olarak yanmaz; yalniz gercekten tutulan gecmis tepe gosterilir.
        const double peak = vm.live_peak_pct[slot];
        const int peak_seg = dark && pct && tl.pct && peak >= 0 ? led_peak_segment(peak, std::clamp(*tl.pct, 0.f, 100.f)) : -1;
        // BOOST maketi (2026-11-16 birebir): kart kenari notr (yuke gore kirmizi / yesil cerceve yok); yuk anlami ikon
        // kutusu, deger rengi, LED cubugu ve kelimede.
        draw_card(g, r, false, depth::kPanelCardRadius);

        const float x = r.x + kTilePad;
        const float w = std::max(r.w - 2 * kTilePad, 1.f);
        const float y0 = r.y + kTilePad;
        // Ton: koyu temada kullanim rampasi; yuksek kontrastta sistem metin rengi. Deger yoksa ikincil metin.
        const Color tone = !has ? p.text_secondary : (dark && pct ? usage_color(p, *pct) : p.text_primary);

        // Baslik satiri (h 32.5): ikon kutusu (32, yaricap 6; ton %12 zemin, >= %75'te 0 0 14px ton %21 parlama), yaninda
        // gap-2 ile iki satir — teknik dizin ("01 //", 9px mono; cevrilmez) ve etiket (11px/600 buyuk harf, tracking);
        // sagda kullanim kelimesi rozeti (11px, beyaz %4 zemin, px 6, yaricap 4).
        const RectF ib{x, y0 + 0.5f, kIconBox, kIconBox};
        if (dark) {
            // Parlama kartin icine kirpilir: kutucuk bolgesinin (hizli yol) disina tasmaz.
            if (pct && usage_glow(*pct) && glow_allowed(s)) {
                g.push_clip(r);
                g.soft_shadow(ib, kIconRadius, 0.f, 0.f, 14.f, tone.with_alpha(0.208f));
                g.pop_clip();
            }
            g.fill_round(ib, kIconRadius, tone.with_alpha(0.12f));
        } else {
            g.stroke_round(ib, theme_.radius_control, p.border);
        }
        g.draw_icon(tl.icon, ib.inset(kIconInset), tone);
        const auto label = t(tl.label);
        const float lx = x + kIconBox + theme_.space(1);
        const float label_w = std::ceil(g.measure(Role::Caps11, label, kUnbounded, false).w) + 1.f;
        {
            const std::wstring idx = (slot < 9 ? L"0" : L"") + std::to_wstring(slot + 1) + L" //";
            g.text(Role::Mono9, idx, {lx, y0, std::max(x + w - lx, 1.f), idx_h}, p.text_secondary, {Align::Leading, false});
            g.text(Role::Caps11, label, {lx, y0 + idx_h + 3.f, label_w, caps_h}, dark ? depth::kSlate200 : p.text_primary, {Align::Leading, false});
        }
        std::wstring word, word_long;
        if (pct) word = tk(usage_short_word_key(*pct)), word_long = tk(usage_word_key(*pct));
        if (!word.empty()) {
            const float ww = std::ceil(g.measure(Role::Small11, word, kUnbounded, false).w) + 1.f;
            const float bw = ww + 2 * kBadgePadX, bh = small_h + 4.f;
            if (lx + label_w + theme_.space(1) + bw <= x + w) {
                const RectF badge{x + w - bw, y0 + (head_h - bh) / 2, bw, bh};
                if (dark) g.fill_round(badge, 4.f, depth::kBadgeFill);
                else g.stroke_round(badge, 4.f, p.border);
                g.text(Role::Small11, word, {badge.x + kBadgePadX, badge.y + 2.f, ww, small_h}, p.text_secondary, {Align::Leading, false});
            }
        }

        // Deger satiri (items-baseline): buyuk sayi (24px/700, ton), ag icin "MB/s" (11px, ikincil; 8 DIP sonra), sagda
        // "TEPE %58" (10px mono, ikincil; yalniz tutulan gercek tepe varken). UIA adi gecis ara degerini degil hedefi okur.
        const float vy = y0 + head_h + 8.5f;
        std::wstring value = dash, value_a11y = dash;
        if (has) {
            if (is_net) {
                value = net_mbs_text(shown, tr);
                value_a11y = replace_all(t(StringId::LiveNetValueA11y), L"{v}", net_mbs_text(tw.target(), tr));
            } else {
                value = pct_text(shown);
                value_a11y = pct_text(tw.target());
            }
        }
        const float vw = std::ceil(g.measure(Role::Num24, value, kUnbounded, false, true).w) + 1.f;
        g.text(Role::Num24, value, {x, vy, std::min(vw, w), num_h}, tone, {Align::Leading, false, false, true});
        float used = vw;
        if (has && is_net) {
            const auto unit = t(StringId::LiveNetUnit);
            const float uw = std::ceil(g.measure(Role::Small11, unit, kUnbounded, false).w) + 1.f;
            const float ux = x + vw + 7.f;
            g.text(Role::Small11, unit, {ux, vy + g.baseline(Role::Num24) - g.baseline(Role::Small11), uw, small_h}, p.text_secondary, {Align::Leading, false});
            used = ux - x + uw;
        }
        std::wstring peak_txt;
        if (has && pct && peak >= 0) {
            peak_txt = replace_all(t(StringId::LivePeak), L"{v}", pct_text(peak));
            const float pw = std::ceil(g.measure(Role::Mono10, peak_txt, kUnbounded, false).w) + 1.f;
            if (used + theme_.space(1) + pw <= w)
                g.text(Role::Mono10, peak_txt, {x + w - pw, vy + g.baseline(Role::Num24) - g.baseline(Role::Mono10), pw, mono10_h}, p.text_secondary,
                       {Align::Trailing, false});
        }

        // 12 parcali LED cubuk (h 7, gap 3, yaricap 2): parca i, (i + 1) / 12 * 100 <= deger + 4 ise yanar; rengi kendi
        // konumunun rampa rengi. Gecis surerken siradaki parca kademeli yanar / soner. Sonuk parca: iz tonu (>= 3:1); tepe
        // izi: iz ile metin arasinda %45 (maket beyaz %45).
        const float ly = vy + num_h + 10.f;
        const float seg_w = std::max((w - kLedGap * static_cast<float>(kLedSegments - 1)) / static_cast<float>(kLedSegments), 1.f);
        const float seg_span = 100.f / static_cast<float>(kLedSegments);
        for (int i = 0; i < kLedSegments; ++i) {
            const RectF seg{x + static_cast<float>(i) * (seg_w + kLedGap), ly, seg_w, kLedH};
            const bool lit = pct && led_segment_lit(i, *pct);
            if (dark) {
                float k = lit ? 1.f : 0.f;
                if (easing && pct && !lit) k = std::clamp((*pct + 4.f - static_cast<float>(led_segment_pct(i))) / seg_span + 1.f, 0.f, 1.f);
                const Color off = i == peak_seg ? mix(track, p.text_primary, 0.45f) : track;
                g.fill_round(seg, kLedRadius, k <= 0.f ? off : mix(off, usage_color(p, led_segment_pct(i)), k));
            } else if (lit) {
                g.fill_round(seg, kLedRadius, p.accent);
            } else {
                g.stroke_round(seg.inset(0.5f), kLedRadius, p.border);
            }
        }
        // Ince grafik (mt 6, h 22): maketteki gibi 120 DIP genislikte ortali; son canli orneklerin kutucuk yuzdesi
        // (ViewModel::live_history; LED ile ayni kaynak), alti ton %10 dolgu, cizgi 1.6. Olculemeyen ornek bosluk birakir;
        // 2'den az gecerli ornekte cizilmez. Dikey olcek 0..100 sabit.
        const float sy = ly + kLedH + 6.f;
        {
            const auto& hist = vm.live_history[slot];
            const std::size_t n = hist.size();
            std::size_t valid = 0;
            for (const float v : hist) valid += std::isfinite(v) ? 1u : 0u;
            if (n >= 2 && valid >= 2) {
                const float bw = std::min(kSparkW, w), bx0 = x + (w - bw) / 2;
                const float step = bw / static_cast<float>(ViewModel::kLiveHistoryMax - 1);
                const float x0 = bx0 + bw - step * static_cast<float>(n - 1);  // en yeni ornek sag kenarda
                const Color lc = dark ? (has ? tone : p.text_secondary) : p.text_primary;
                std::vector<Point> run;
                const auto flush = [&] {
                    if (run.size() >= 2) {
                        if (dark) {
                            std::vector<Point> area = run;
                            area.push_back({run.back().x, sy + kSparkH});
                            area.push_back({run.front().x, sy + kSparkH});
                            g.fill_polygon(area, lc.with_alpha(0.10f));
                        }
                        g.polyline(run, 1.6f, lc);
                    }
                    run.clear();
                };
                for (std::size_t k = 0; k < n; ++k) {
                    const float v = hist[k];
                    if (!std::isfinite(v)) {
                        flush();
                        continue;
                    }
                    run.push_back({x0 + step * static_cast<float>(k), sy + kSparkH - 2.f - std::clamp(v, 0.f, 100.f) / 100.f * (kSparkH - 4.f)});
                }
                flush();
            }
        }
        // Alt blok (mt 8, pt 6, ust kenarda beyaz %5 cizgi): parca adi (12px/500, birincil; sigmazsa "…", tam ad UIA
        // adinda) ve canli ayrinti (11px, ikincil).
        const float dy = sy + kSparkH + 8.f;
        if (dark) g.fill({x, dy, w, 1.f}, Color{1, 1, 1, 0.05f});
        else g.fill({x, dy, w, 1.f}, p.border);
        const float ny = dy + 6.5f;
        if (tl.name_loading) {
            // Iskelet: sabit, sakin gri cubuk (surekli parlama animasyonu yok).
            g.fill_round({x, ny + 4, w * 0.7f, name_h - 8}, 4, p.surface_raised);
        } else if (tl.name.empty()) {
            g.text(Role::Medium12, dash, {x, ny, w, name_h}, p.text_secondary, {Align::Leading, false});
        } else {
            g.text(Role::Medium12, fit_text(g, Role::Medium12, tl.name, w), {x, ny, w, name_h}, p.text_primary, {Align::Leading, false});
        }
        const float ty = ny + name_h - 0.75f;
        constexpr std::wstring_view kUp = L"↑ ";
        if (is_net && has && dark && detail.starts_with(kUp)) {
            // Renk rolu (F): yukleme oku mor (veri rengi); sayi ikincil renkte kalir.
            const float aw = std::ceil(g.measure(Role::Small11, kUp, kUnbounded, false).w);
            g.text(Role::Small11, kUp.substr(0, 1), {x, ty, aw, detail_h}, p.data_net_up, {Align::Leading, false});
            g.text(Role::Small11, std::wstring_view(detail).substr(kUp.size()), {x + aw, ty, std::max(w - aw, 1.f), detail_h}, p.text_secondary,
                   {Align::Leading, false});
        } else {
            g.text(Role::Small11, detail, {x, ty, w, detail_h}, p.text_secondary, {Align::Leading, true});
        }

        // UIA: "CPU %23, az kullanim. AMD Ryzen 7 9800X3D. 8 cekirdek · en yogun %41" / eksikse "GPU —. <ad>. Bu sistemde
        // sayac yok". Ad ve ayrinti kisaltilmadan okunur.
        std::wstring name = label + L" " + value_a11y;
        if (has && !word_long.empty()) name += L", " + word_long;
        if (!peak_txt.empty()) name = a11y_join(std::move(name), replace_all(t(StringId::LivePeakAcc), L"{v}", pct_text(peak)));
        name = a11y_join(std::move(name), tl.name);
        if (has) name = a11y_join(std::move(name), !tl.detail_a11y.empty() ? tl.detail_a11y : tl.detail);
        else name = a11y_join(std::move(name), detail);
        hits.add_text(seq.take(), r, std::move(name));
    };

    // Yerlesim: 5 sutun tek sira; dar govdede 3 + 2 (alt sira iki genis kutucuk); cok darsa alt alta. Satir
    // yuksekligi icerik kadardir: ayrinti tek satirdir, yalniz sigmayan neden metni sarar (o satirin tum kutucuklari).
    const float top = st.cursor;
    const std::size_t first_seq = seq.next;
    const std::size_t first_shadow = shadow_log_.size();
    RectF region;
    const int cols = live_tile_columns(body.w, gap);
    std::size_t i = 0;
    while (i < kLiveTileCount) {
        const std::size_t left = kLiveTileCount - i;
        const std::size_t n = cols == 5 ? left : cols == 3 ? std::min<std::size_t>(left, i == 0 ? 3 : 2) : 1;
        const float tw = (body.w - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
        const float inner = std::max(tw - 2 * kTilePad, 1.f);
        float detail_h = small_h;
        std::array<std::wstring, kLiveTileCount> details;
        for (std::size_t k = 0; k < n; ++k) {
            details[k] = detail_for(tiles[i + k], inner);
            // Deger varken ayrinti tek satirdir (sigdirilmis); yalniz neden metni sarabilir.
            if (!has_value(tiles[i + k])) detail_h = std::max(detail_h, g.measure(Role::Small11, details[k], inner).h);
        }
        const RectF row = st.next(base_h + detail_h);
        if (pass != TilePass::Measure)
            for (std::size_t k = 0; k < n; ++k)
                draw_tile(tiles[i + k], {row.x + static_cast<float>(k) * (tw + gap), row.y, tw, row.h}, detail_h, details[k]);
        if (region.w <= 0) region = row;
        else region.h = row.bottom() - region.y;
        i += n;
    }
    if (pass != TilePass::Full) return;
    // Hizli yol bolgesi (yalniz tamami gorunur govdedeyken): 1 Hz ornek ve deger gecisi yalniz burayi yeniden cizer.
    region = {region.x - kRegionPad, region.y - kRegionPad, region.w + 2 * kRegionPad, region.h + 2 * kRegionPad};
    if (region.y >= s.view_top + 4.f && region.bottom() <= s.view_bottom) {
        s.tiles_region = {region.x, region.y - s.scroll, region.w, region.h};
        s.tiles_body = {body.x, body.y - s.scroll, body.w, body.h};
        s.tiles_top = top;
        s.tiles_h = st.cursor - top;
        s.tiles_seq = first_seq;
        s.tiles_shadow_begin = first_shadow;
        s.tiles_shadow_end = shadow_log_.size();
    }
}

void Screen::paint_tiles_region(Graphics& g, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    const RectF& r = s.tiles_region;
    if (r.w <= 0 || r.h <= 0) return;
    // Tam karedeki sira bolgeye kirpilarak yeniden kurulur: pencere zemini, kutucuklardan once cizilen kartlarin
    // golgeleri (Panel kahraman karti), kutucuklar (kendi golgeleriyle), sonra cizilen kartlarin bolgeye dusen golgeleri.
    g.push_clip(r);
    paint_backdrop(g, r);
    const std::size_t keep = shadow_log_.size();
    replay_shadows(g, s, 0, s.tiles_shadow_begin);
    Stack st{s.tiles_body, theme_.card_gap};
    st.cursor = s.tiles_top;
    StatusSeq seq{s.tiles_seq};
    live_tiles(g, st, s.tiles_body, s, vm, now, hits, seq, TilePass::Region);
    replay_shadows(g, s, s.tiles_shadow_end, s.shadow_casters.size());
    g.pop_clip();
    shadow_log_.resize(keep);
    card_log_.clear();  // kart cerceveleri yalniz tam cizimde kaydedilir
}

void update_live_peaks(std::array<LedPeak, kLiveTileCount>& peaks, std::array<double, kLiveTileCount>& shown,
                       const core::metrics::LiveSnapshot& l, std::int64_t now_ms, bool still) {
    // Kutucuk yuzdeleri cizimle ayni kaynaktan (hedef deger; gecis ara degeri degil). Ag: indirme / baglanti.
    const std::array<std::optional<float>, kLiveTileCount> pct{pct_of(l.cpu_pct), pct_of(l.ram_pct), pct_of(l.gpu_pct),
                                                               pct_of(l.disk_pct), net_pct_of(l)};
    for (std::size_t i = 0; i < kLiveTileCount; ++i) {
        const auto v = led_peak_update(peaks[i], pct[i] ? std::optional<double>(*pct[i]) : std::nullopt, now_ms, still);
        shown[i] = v ? *v : -1.0;
    }
}

void push_live_history(std::array<std::vector<float>, kLiveTileCount>& history, const core::metrics::LiveSnapshot& l) {
    const std::array<std::optional<float>, kLiveTileCount> pct{pct_of(l.cpu_pct), pct_of(l.ram_pct), pct_of(l.gpu_pct), pct_of(l.disk_pct),
                                                               net_pct_of(l)};
    for (std::size_t i = 0; i < kLiveTileCount; ++i) {
        auto& h = history[i];
        h.push_back(pct[i] ? *pct[i] : std::numeric_limits<float>::quiet_NaN());
        if (h.size() > ViewModel::kLiveHistoryMax) h.erase(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(h.size() - ViewModel::kLiveHistoryMax));
    }
}

bool Screen::tiles_layout_unchanged(Graphics& g, UiState& s, const ViewModel& vm) {
    if (s.tiles_region.w <= 0 || s.tiles_region.h <= 0) return false;
    Stack st{s.tiles_body, theme_.card_gap};
    st.cursor = s.tiles_top;
    StatusSeq seq{s.tiles_seq};
    HitMap scratch;
    live_tiles(g, st, s.tiles_body, s, vm, 0, scratch, seq, TilePass::Measure);
    return std::fabs((st.cursor - s.tiles_top) - s.tiles_h) < 0.5f;
}

}  // namespace ef::ui
