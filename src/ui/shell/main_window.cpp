#include "ui/shell/main_window.h"
#include "ui/shell/hit_dispatch.h"
#include "ui/motion/motion.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "core/guided/site_links.h"
#include "ui/brand/brand.h"
#include "ui/shell/window_fit.h"
#include "ui/theme/system_palette.h"

namespace ef::ui {

namespace {
constexpr UINT_PTR kSmokeTimer = 1;
constexpr UINT_PTR kAnimTimer = 2;
constexpr UINT kAnimIntervalMs = 16;  // ~60 Hz yalniz gecis surerken (UI-08 olcumu: 120 Hz gecis basi 28 ms CPU)
constexpr UINT_PTR kTickTimer = 3;    // M04: test surerken gecen sure (1 Hz)
constexpr UINT kTickIntervalMs = 1000;
constexpr UINT_PTR kStripTimer = 4;   // RGB serit kaymasi (spec §3: ~20 kare/sn, yalniz serit gecersiz kilinir)
constexpr UINT kStripIntervalMs = 50;
constexpr UINT_PTR kBreatheTimer = 5;  // Panel Performans dugmesinin nefes halkasi / kalp atisi (yalniz dugme bolgesi gecersiz kilinir)
constexpr UINT kBreatheIntervalMs = 66;  // ~15 kare/sn: yavas nefes icin yeterli, bos yuk dusuk
constexpr UINT kHeartbeatIntervalMs = 33;  // ~30 kare/sn: 240 ms'lik vurus icin; dinlenme suresinde kare cizilmez
constexpr UINT_PTR kPulseTimer = 7;      // Oyunlar "Calisiyor" noktasinin nabzi (yalniz nokta kutulari gecersiz kilinir)
constexpr UINT kPulseIntervalMs = 100;   // ~10 kare/sn: 1,6 sn'lik yavas nabiz icin yeterli, bos yuk dusuk

COLORREF colorref(const Color& c) {
    return RGB(static_cast<int>(c.r * 255.f + 0.5f), static_cast<int>(c.g * 255.f + 0.5f),
               static_cast<int>(c.b * 255.f + 0.5f));
}

// Yasayan ana pencereler (UI is parcacigi; pratikte en cok bir tane). MainWindow::from_hwnd bununla bulur.
std::vector<MainWindow*>& live_windows() {
    static std::vector<MainWindow*> v;
    return v;
}

// R-C4: yapistirma (yalniz kisa metin; izinli karakter suzgeci modal_input_paste'tedir). Pano okunamazsa bos.
std::wstring clipboard_text(HWND hwnd) {
    std::wstring out;
    if (!OpenClipboard(hwnd)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            const std::size_t cap = GlobalSize(h) / sizeof(wchar_t);
            std::size_t n = 0;
            while (n < cap && p[n] != L'\0' && n < 256) ++n;
            out.assign(p, n);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}
}  // namespace

MainWindow* MainWindow::from_hwnd(HWND hwnd) {
    if (!hwnd) return nullptr;
    for (MainWindow* w : live_windows())
        if (w->hwnd_ == hwnd) return w;
    return nullptr;
}

// ---- Uygulama ici kip penceresi (spec 2026-11-12 E) ----

ModalResult MainWindow::run_modal(ModalSpec spec) {
    // Pencere gosterilemiyorsa (yok / tepside gizli / simge durumunda) soru sorulmaz: sessiz varsayilan Hayir.
    if (!hwnd_ || !IsWindowVisible(hwnd_) || IsIconic(hwnd_)) return modal_not_shown();
    ModalFrame frame;
    frame.st = modal_open(std::move(spec));
    frame.prev_focus = state_.focus;
    frame.prev_focus_visible = state_.focus_visible;
    modals_.push_back(&frame);
    state_.modal = &frame.st;
    // Yarim kalan fare basisi / hover / metin girisi sayfada kalmaz: kip acikken sayfa ogeleri etkilesimsizdir.
    if (GetCapture() == hwnd_) ReleaseCapture();
    pressed_ = Hit::None;
    {
        const auto now = now_ms();
        screen_.set_pressed(state_, Hit::None, now);
        screen_.set_hover(state_, Hit::None, now);
        screen_.open_modal_motion(state_, now);
    }
    set_focus(modal_hit(frame.st.focus), true);  // ilk odak Hayir; halka gorunur (Enter neyi calistirir belli olsun)
    modal_uia_event_ = true;
    if (frame.st.spec.countdown_s > 0) SetTimer(hwnd_, kModalTimer, 1000, nullptr);
    update_strip_timer();  // karartilmis icerik duragandir: serit / dugme hareketi durur
    invalidate();

    // Ic ice ileti dongusu: uygulamanin geri kalani (model iletileri, zamanlayicilar, cizim) calismaya devam eder.
    MSG msg{};
    while (!frame.st.done && !frame.gone) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) {
            PostQuitMessage(static_cast<int>(msg.wParam));  // cikis istegi dis donguye birakilir
            break;
        }
        if (r == -1) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (frame.gone) return modal_not_shown();  // pencere yok edildi (nesne silinmis olabilir): uyelere dokunulmaz
    modal_dismiss(frame.st);                   // dongu yanitsiz bittiyse (cikis): Hayir
    const ModalResult result = frame.st.result;
    modals_.erase(std::remove(modals_.begin(), modals_.end(), &frame), modals_.end());
    state_.modal = modals_.empty() ? nullptr : &modals_.back()->st;
    bool countdown = false;
    for (const ModalFrame* f : modals_) countdown = countdown || (f->st.spec.countdown_s > 0 && !f->st.done);
    if (!countdown && hwnd_) KillTimer(hwnd_, kModalTimer);
    // Odak geri doner: alttaki kip penceresine ya da acilmadan onceki ogeye.
    if (state_.modal) set_focus(modal_hit(state_.modal->focus), true);
    else set_focus(frame.prev_focus, frame.prev_focus_visible);
    modal_uia_event_ = true;
    {
        const auto now = now_ms();
        screen_.set_pressed(state_, Hit::None, now);
        screen_.set_hover(state_, Hit::None, now);
    }
    update_strip_timer();
    invalidate();
    return result;
}

void MainWindow::modal_sync() {
    if (modals_.empty()) return;
    const ModalState& st = modals_.back()->st;
    if (!st.done) set_focus(modal_hit(st.focus), state_.focus_visible);
    invalidate();
}

void MainWindow::modal_key_input(ModalKey k) {
    if (modals_.empty()) return;
    ModalState& st = modals_.back()->st;
    const int choice = st.choice;
    modal_key(st, k);
    if (st.choice != choice && st.choice >= 0) note_selection(hit_of(HitKind::ModalChoice, static_cast<std::size_t>(st.choice)));  // TH-04
    state_.focus_visible = true;  // klavye: odak halkasi gorunur
    modal_sync();
}

void MainWindow::abandon_modals() {
    for (ModalFrame* f : modals_) {
        f->gone = true;
        modal_dismiss(f->st);
    }
    modals_.clear();
    state_.modal = nullptr;
    if (hwnd_) KillTimer(hwnd_, kModalTimer);
}

std::int64_t MainWindow::now_ms() { return static_cast<std::int64_t>(GetTickCount64()); }

void MainWindow::read_contrast_pref() {
    // UI-14: uygulama teması her zaman koyudur; yüksek kontrast etkinse sistem renkleri öncelik alır.
    hc_palette_ = system_high_contrast_palette();
    const Palette* p = hc_palette_ ? &*hc_palette_ : &theme_.dark;
    screen_.set_palette(p);
    if (hwnd_) {
        const HWND hw = hwnd_;
        (void)apply_dark_caption([hw](unsigned attr) {
            const BOOL dark = TRUE;
            return static_cast<long>(DwmSetWindowAttribute(hw, attr, &dark, sizeof(dark)));
        });
        // Uyumluluk B6 (bosluk 27): baslik rengi yalniz Windows 11'de; Windows 10'da reddedilir (HRESULT) ve
        // yukaridaki koyu baslik kabul edilir.
        COLORREF cap = hc_palette_ ? 0xFFFFFFFF /*DWMWA_COLOR_DEFAULT*/ : colorref(p->sidebar);
        (void)DwmSetWindowAttribute(hwnd_, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
    }
}

void MainWindow::set_focus(Hit h, bool visible) {
    const bool changed = state_.focus != h;
    state_.focus = h;
    state_.focus_visible = visible;
    if (changed && uia_) uia_->notify_focus(h);
}

void MainWindow::update_visibility() {
    if (!hwnd_) {
        activity_.set(Visibility::Closed);
        return;
    }
    if (!IsWindowVisible(hwnd_)) activity_.set(Visibility::Hidden);
    else if (IsIconic(hwnd_)) activity_.set(Visibility::Minimized);
    else activity_.set(Visibility::Visible);
    // Gorunmezken animasyon zamanlayicisi calismaz (gereksiz uyanma yok).
    if (activity_.get() != Visibility::Visible && anim_timer_) {
        KillTimer(hwnd_, kAnimTimer);
        anim_timer_ = false;
    }
    update_strip_timer();  // gorunmez/kucukken serit durur
}

StripInputs MainWindow::strip_inputs() const {
    StripInputs in;
    in.visible = hwnd_ && activity_.get() == Visibility::Visible;
    in.foreground = hwnd_ && GetForegroundWindow() == hwnd_;
    in.game_blocked = vm_.game_gate_blocked;
    in.test_running = core::measure::busy(vm_.measure) || vm_.measure.endurance.running || vm_.speed.running;  // spec §3: hiz testi de durdurur
    SYSTEM_POWER_STATUS ps{};
    in.on_battery = GetSystemPowerStatus(&ps) && ps.ACLineStatus == 0;
    in.reduced_motion = state_.reduced_motion;
    in.app_animations = app_animations_;
    in.strip_setting = vm_.settings.strip_enabled && theme_.strip_animation;
    in.high_contrast = hc_palette_.has_value();
    in.modal = state_.modal != nullptr;
    return in;
}

void MainWindow::invalidate_strip() {
    if (!hwnd_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    rc.bottom = std::min<LONG>(rc.bottom, static_cast<LONG>(std::ceil(to_px(theme_.strip_h, scale()))));
    if (rc.bottom > 0) InvalidateRect(hwnd_, &rc, FALSE);
}

void MainWindow::update_strip_timer() {
    const StripInputs in = strip_inputs();
    const bool want = hwnd_ && strip_should_animate(in);
    if (want != state_.strip_live) {
        state_.strip_live = want;
        invalidate_strip();  // durunca sabit gradyan (kayma 0) cizilir
    }
    if (want && !strip_timer_) strip_timer_ = SetTimer(hwnd_, kStripTimer, kStripIntervalMs, nullptr) != 0;
    else if (!want && strip_timer_) {
        if (hwnd_) KillTimer(hwnd_, kStripTimer);
        strip_timer_ = false;
    }
    // Spec 2026-11-12 F: Panel hareketi ayni kapilara baglidir. Deger gecisleri (LED, sayac, cubuk) pencere
    // gorunmezken / oyun acikken / test surerken / pilde aninda oturur; dugmenin hareketi (acikken nefes halkasi,
    // kapaliyken kalp atisi: spec B.1) ayrica on plan, hareket tercihi ve yuksek kontrast kosullarina bakar ve yalniz
    // dugme etkinken (Performans Acik ya da Kapali; is / onay surerken degil), gorunur govdedeyken calisir.
    state_.motion_paused = motion_paused_for(in);
    const auto phase = vm_.perf.phase;
    const bool panel_ring = state_.page == Page::Overview && (phase == core::perf::Phase::On || phase == core::perf::Phase::Off);
    // Spec 2026-11-12 C: Olcumler Test kartinin yuvarlak baslat dugmesi bosta (baslatilabilirken) ayni nefes kapilarina
    // bagli camgobegi halkayla nefes alir; bolgeyi cizim yazar (yalniz baslatilabilir dugme tamami gorunurken).
    const bool test_ring = state_.page == Page::Measurements && core::measure::can_start(vm_.measure) && !vm_.measure.endurance.running;
    const bool ring = (panel_ring || test_ring) && state_.power_region.w > 0 && !update_gate_shown(vm_);
    const bool breathe = hwnd_ && breathe_should_animate(in, ring);
    if (breathe != state_.breathe_live) {
        state_.breathe_live = breathe;
        RECT rc{};
        if (dip_region_px(state_.power_region, rc)) InvalidateRect(hwnd_, &rc, FALSE);  // durunca sabit halka cizilir
    }
    const UINT interval = panel_ring && phase == core::perf::Phase::Off ? kHeartbeatIntervalMs : kBreatheIntervalMs;
    if (breathe && (!breathe_timer_ || breathe_interval_ != interval)) {
        breathe_timer_ = SetTimer(hwnd_, kBreatheTimer, interval, nullptr) != 0;  // ayni kimlik: araligi degistirir
        breathe_interval_ = interval;
        beat_rest_drawn_ = false;
    } else if (!breathe && breathe_timer_) {
        if (hwnd_) KillTimer(hwnd_, kBreatheTimer);
        breathe_timer_ = false;
    }
    // Spec 2026-11-12 B2: Oyunlar'da calisan oyunun "Calisiyor" noktasi nabiz atar (pulse_should_animate: nokta yalniz
    // oyun acikken vardir; pencere on planda olmali). Durunca nokta sabit cizilir.
    const bool dots = state_.page == Page::Games && !state_.pulse_regions.empty() && !update_gate_shown(vm_);
    const bool pulse = hwnd_ && pulse_should_animate(in, dots);
    if (pulse != state_.pulse_live) {
        state_.pulse_live = pulse;
        invalidate_pulse();
    }
    if (pulse && !pulse_timer_) pulse_timer_ = SetTimer(hwnd_, kPulseTimer, kPulseIntervalMs, nullptr) != 0;
    else if (!pulse && pulse_timer_) {
        if (hwnd_) KillTimer(hwnd_, kPulseTimer);
        pulse_timer_ = false;
    }
}

void MainWindow::invalidate_pulse() {
    if (!hwnd_) return;
    for (const RectF& r : state_.pulse_regions) {
        RECT rc{};
        if (dip_region_px(r, rc)) InvalidateRect(hwnd_, &rc, FALSE);
    }
}

bool MainWindow::paint_pulse_fast(HDC dc, const std::vector<RECT>& rects) {
    if (rects.empty() || hits_.items.empty() || state_.page != Page::Games || state_.pulse_regions.empty() || update_gate_shown(vm_))
        return false;
    // Tam kare isteyen bir gecis (sayfa belirmesi, hover, basma) surerken bolgesel cizim yapilmaz.
    if (screen_.animating(state_, now_ms(), false)) return false;
    RECT client{};
    GetClientRect(hwnd_, &client);
    const LONG strip_px = static_cast<LONG>(std::ceil(to_px(theme_.strip_h, scale())));
    std::vector<RECT> dots;
    for (const RectF& r : state_.pulse_regions) {
        RECT rc{};
        if (dip_region_px(r, rc)) dots.push_back(rc);
    }
    bool want_strip = false;
    std::vector<bool> want(dots.size(), false);
    for (const RECT& r : rects) {
        if (r.right <= r.left || r.bottom <= r.top) continue;
        if (strip_px > 0 && r.top >= 0 && r.bottom <= strip_px) {
            want_strip = true;
            continue;
        }
        bool found = false;
        for (std::size_t k = 0; k < dots.size() && !found; ++k)
            if (r.left >= dots[k].left && r.top >= dots[k].top && r.right <= dots[k].right && r.bottom <= dots[k].bottom) want[k] = found = true;
        if (!found) return false;  // baska bir yer de gecersiz: tam kare
    }
    if (std::find(want.begin(), want.end(), true) == want.end()) return false;  // yalniz serit: mevcut serit hizli yolu
    const auto now = now_ms();
    bool ok = true;
    if (want_strip && gfx_->bind_dc(dc, client.right, std::min(strip_px, client.bottom), dpi_) && gfx_->begin()) {
        screen_.paint_strip(*gfx_, static_cast<float>(client.right) / scale(), state_, now);
        ok = gfx_->end() && ok;
    }
    for (std::size_t k = 0; k < dots.size(); ++k) {
        if (!want[k]) continue;
        if (!gfx_->bind_dc(dc, dots[k], dpi_) || !gfx_->begin()) {
            ok = false;
            continue;
        }
        // Alt hedefin tamami yeniden yazilir: once cip zemini (kutunun tamami "Calisiyor" cipinin duz kismindadir;
        // disa yuvarlanan kenar pikselleri de zemindir), sonra nokta (paint_pulse_region).
        gfx_->clear(chip_colors(screen_.pal(), ChipTone::Good).bg);
        gfx_->set_offset(-static_cast<float>(dots[k].left) / scale(), -static_cast<float>(dots[k].top) / scale());  // pencere koordinati
        screen_.paint_pulse_region(*gfx_, state_, now);
        gfx_->set_offset(0, 0);
        ok = gfx_->end() && ok;
    }
    if (!ok) invalidate();  // cihaz kayboldu / hedef baglanamadi: sonraki karede tam cizim
    return true;
}

bool MainWindow::dip_region_px(const RectF& r, RECT& out) const {
    if (!hwnd_ || r.w <= 0 || r.h <= 0) return false;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float sc = scale();
    out.left = std::max<LONG>(0, static_cast<LONG>(std::floor(to_px(r.x, sc))));
    out.top = std::max<LONG>(0, static_cast<LONG>(std::floor(to_px(r.y, sc))));
    out.right = std::min<LONG>(rc.right, static_cast<LONG>(std::ceil(to_px(r.right(), sc))));
    out.bottom = std::min<LONG>(rc.bottom, static_cast<LONG>(std::ceil(to_px(r.bottom(), sc))));
    return out.left < out.right && out.top < out.bottom;
}

void MainWindow::invalidate_tiles(bool new_sample) {
    RECT rc{};
    // Yeni ornekle kutucuk satiri boy degistirdiyse (olculemeyen degerin neden metni sardi / tek satira dondu) alttaki
    // kartlar da kayar: bolgesel cizim yetmez. Deger gecisi kareleri ayni ornegi cizer; yerlesim degismez.
    if (state_.page == Page::Overview && !hits_.items.empty() && dip_region_px(state_.tiles_region, rc) &&
        (!new_sample || screen_.tiles_layout_unchanged(*gfx_, state_, vm_)))
        InvalidateRect(hwnd_, &rc, FALSE);
    else invalidate();  // bolge henuz bilinmiyor / kirpilmis / satir boyu degisti: tam cizim
}

bool MainWindow::paint_panel_fast(HDC dc, const std::vector<RECT>& rects) {
    // Olcumler: yalniz Test karti dugmesinin nefes bolgesi (tiles_region orada bostur).
    if (rects.empty() || hits_.items.empty() || (state_.page != Page::Overview && state_.page != Page::Measurements) || update_gate_shown(vm_))
        return false;
    // Tam kare isteyen bir gecis (sayfa belirmesi, hover, sayac) surerken bolgesel cizim yapilmaz: bolge o karenin
    // opakligiyla / durumuyla cizilmelidir (tam kare yalniz gecersiz bolgeye yazar).
    if (screen_.animating(state_, now_ms(), false)) return false;
    RECT client{};
    GetClientRect(hwnd_, &client);
    const LONG strip_px = static_cast<LONG>(std::ceil(to_px(theme_.strip_h, scale())));
    RECT power{}, tiles{};
    const bool has_power = dip_region_px(state_.power_region, power);
    const bool has_tiles = dip_region_px(state_.tiles_region, tiles);
    const auto inside = [](const RECT& r, const RECT& box) {
        return r.left >= box.left && r.top >= box.top && r.right <= box.right && r.bottom <= box.bottom;
    };
    bool want_strip = false, want_power = false, want_tiles = false;
    for (const RECT& r : rects) {
        if (r.right <= r.left || r.bottom <= r.top) continue;
        if (strip_px > 0 && r.top >= 0 && r.bottom <= strip_px) want_strip = true;
        else if (has_power && inside(r, power)) want_power = true;
        else if (has_tiles && inside(r, tiles)) want_tiles = true;
        else return false;  // baska bir yer de gecersiz: tam kare
    }
    if (!want_power && !want_tiles) return false;  // yalniz serit: mevcut serit hizli yolu
    const auto now = now_ms();
    bool ok = true;
    if (want_strip && gfx_->bind_dc(dc, client.right, std::min(strip_px, client.bottom), dpi_) && gfx_->begin()) {
        screen_.paint_strip(*gfx_, static_cast<float>(client.right) / scale(), state_, now);
        ok = gfx_->end() && ok;
    }
    const auto region = [&](const RECT& sub, auto&& draw) {
        if (!gfx_->bind_dc(dc, sub, dpi_) || !gfx_->begin()) {
            ok = false;
            return;
        }
        // Alt hedefin tamami yeniden yazilir: once zemin (bolgenin kenar pikselleri yalniz zemindir), sonra bolge.
        gfx_->clear(screen_.pal().background);
        gfx_->set_offset(-static_cast<float>(sub.left) / scale(), -static_cast<float>(sub.top) / scale());  // pencere koordinati
        draw();
        gfx_->set_offset(0, 0);
        ok = gfx_->end() && ok;
    };
    if (want_power) region(power, [&] { screen_.paint_power_region(*gfx_, state_, vm_, now); });
    if (want_tiles)
        region(tiles, [&] {
            HitMap fresh;
            screen_.paint_tiles_region(*gfx_, state_, vm_, now, fresh);
            // Kutucuklarin guncel UIA adlari mevcut haritaya aktarilir (kutular ve ekran disi durumu degismez).
            for (auto& [id, meta] : fresh.meta)
                if (const auto it = hits_.meta.find(id); it != hits_.meta.end()) {
                    meta.offscreen = it->second.offscreen;
                    it->second = std::move(meta);
                }
            uia_model_->hits = hits_;
        });
    if (!ok) invalidate();  // cihaz kayboldu / hedef baglanamadi: sonraki karede tam cizim
    return true;
}

bool MainWindow::arrow_tabs(int delta) {
    if (update_gate_shown(vm_)) return false;  // guncelleme kartinda sekme yok
    const Hit f = state_.focus;
    const auto subs = systools_tab_hits();  // Sistem, Temizlik, Baslangic
    const auto sub = std::find(subs.begin(), subs.end(), f);
    Hit next = Hit::None;
    if (systools_page(state_.page) && sub != subs.end()) {
        const auto n = static_cast<std::ptrdiff_t>(subs.size());
        next = subs[static_cast<std::size_t>(((sub - subs.begin()) + delta + n) % n)];  // alt sekmeler arasinda (uclarda sarar)
    } else {
        const auto it = std::find_if(kNavOrder.begin(), kNavOrder.end(), [f](Page p) { return nav_hit(static_cast<std::size_t>(p)) == f; });
        if (it == kNavOrder.end()) return false;
        const auto n = static_cast<std::ptrdiff_t>(kNavOrder.size());
        const auto k = ((it - kNavOrder.begin()) + delta + n) % n;  // uclarda sarar
        next = nav_hit(static_cast<std::size_t>(kNavOrder[static_cast<std::size_t>(k)]));
    }
    set_focus(next, true);
    activate(next);  // sekme listesi: ok tusu secimi de tasir
    return true;
}

bool MainWindow::arrow_games(int dx, int dy) {
    if (state_.page != Page::Games || update_gate_shown(vm_)) return false;
    const Hit next = games_arrow_target(state_.game_card_order, state_.games_cols, state_.focus, dx, dy);
    if (next == Hit::None) return false;  // odak kart degil / o yonde kart yok: eski davranis (govde kayar)
    set_focus(next, true);
    state_.reveal_focus = true;
    invalidate();
    return true;
}

void MainWindow::update_window_icons() {
    if (!hwnd_) return;
    const UINT dpi = dpi_ ? dpi_ : 96;
    HICON sm = brand::load_app_icon(inst_, GetSystemMetricsForDpi(SM_CXSMICON, dpi));
    HICON bg = brand::load_app_icon(inst_, GetSystemMetricsForDpi(SM_CXICON, dpi));
    if (sm) SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(sm));
    if (bg) SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bg));
    // Eski ikonlar artik pencerede degil: birak (HICON sizintisi yok).
    if (sm && icon_small_) DestroyIcon(icon_small_);
    if (bg && icon_big_) DestroyIcon(icon_big_);
    if (sm) icon_small_ = sm;
    if (bg) icon_big_ = bg;
}

void MainWindow::read_motion_pref() {
    BOOL anim = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
    // UI-13: uygulama hareketi kullanicinin Windows tercihini izler. Performans bu ayari kapattiysa
    // journal'daki ozgun deger kullanilir (Performans Acik diye uygulama hareketi sonmez).
    // R33: kullanici uygulama animasyonlarini kapattiysa Windows animasyonlari acik olsa bile hareket yok.
    const bool reduced = reduced_motion_for(app_motion_enabled(anim != FALSE, motion_original_), app_animations_);
    const bool was_reduced = state_.reduced_motion;
    state_.reduced_motion = reduced;
    if (reduced && !was_reduced) {
        // Kapatma aninda yarim kalan gecisler hedefe atlar: bundan sonra hicbir animasyon kare istemez.
        const auto now = now_ms();
        const auto snap = [now](Tween& t) { t.retarget(t.target(), now, 0); };
        for (auto& t : state_.nav_hover) snap(t);
        for (auto& kv : state_.hover_tween) snap(kv.second);
        for (auto& kv : state_.press_tween) snap(kv.second);
        snap(state_.toggle_hover);
        snap(state_.toggle_knob);
        snap(state_.page_fade);
        snap(state_.progress_fill);
        snap(state_.card_fade);
        snap(state_.brand_reveal);
        for (auto& t : state_.live_value) snap(t);  // canli kutucuk sayilari
        for (auto& t : state_.panel_num) snap(t);   // Panel sayaclari
        for (auto& t : state_.measure_num) snap(t); // Olcumler sonuc sayilari
        snap(state_.measure_bars);                  // Olcumler darbogaz cubuklari
        snap(state_.readiness_fill);                // Panel hazirlik cubugu
        snap(state_.readiness_ring);                // Oyun hazirligi halkasi
        snap(state_.speed_needle);                  // hiz testi gostergesi
        snap(state_.modal_fade);                    // kip penceresi acilisi
        snap(state_.cleanup_ring);                  // Sistem araclari temizlik halkasi
        snap(state_.history_anim);                  // Gecmis satir acilisi
        snap(state_.history_bars);                  // Gecmis karsilastirma cubuklari
    }
}

std::unique_ptr<MainWindow> MainWindow::create(HINSTANCE inst, const Theme& theme, const core::i18n::Catalog& cat,
                                               Options opts) {
    std::unique_ptr<MainWindow> w(new MainWindow(theme, cat));
    live_windows().push_back(w.get());  // from_hwnd; yikici listeden cikarir
    w->opts_ = std::move(opts);
    w->locale_ = cat.locale() == core::i18n::Locale::TrTR ? L"tr-TR" : L"en-US";
    w->gfx_ = Graphics::create(theme, w->locale_);
    if (!w->gfx_) return nullptr;
    // UI-05: varsayilan yazilim cizimi (olcum: artifacts/ui-performance.md). Tani icin EF_UI_RENDERER=hardware.
    {
        wchar_t r[16] = {};
        const DWORD n = GetEnvironmentVariableW(L"EF_UI_RENDERER", r, 16);
        w->gfx_->set_software(!(n > 0 && n < 16 && std::wstring(r) == L"hardware"));
    }
    w->state_.games_guides_open = false;     // uzun rehberler istege bagli
    w->state_.history_all_open = false;      // Gecmis: journal satirlari kapali baslar; ayrinti istege bagli
    w->state_.settings_games_open = false;     // Ayarlar (B7): oyun onaylari ve lisans metni "Goster" ile acilir
    w->state_.settings_licenses_open = false;
    w->state_.settings_advanced_open = false;  // P12-B3: Ayarlar'da yalniz dil, guncelleme, hakkinda gorunur
    w->read_motion_pref();
    w->read_contrast_pref();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // Sinif fircasi surec omru boyunca tek (pencere yeniden olusturulunca GDI nesnesi sizmaz).
    static const HBRUSH bg_brush = CreateSolidBrush(colorref(theme.dark.background));
    wc.hbrBackground = bg_brush;  // ilk karede beyaz parlama yok
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_ECUFPS_APP));  // marka 2.0.0 EF (EXE kaynagi)
    w->inst_ = inst;
    wc.lpszClassName = w->opts_.window_class.c_str();
    // Pencere yeniden olusturulabilir (tepsiden ac/kapat): sinif zaten kayitliysa hata degil.
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;

    const UINT sys_dpi = GetDpiForSystem();
    const int width = MulDiv(1180, static_cast<int>(sys_dpi), 96);
    const int height = MulDiv(720, static_cast<int>(sys_dpi), 96);
    // Uyumluluk B6 (bosluk 18): birincil ekranin calisma alanina (gorev cubugu haric) sigdirilir ve ortalanir
    // (setup_app ile ayni); 1366x768 veya %150-%200 olcekte alt kisim gorev cubugunun altinda kalmaz.
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, cw = width, ch = height;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi)) {
        const PxRect fit = fit_centered({mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom}, width, height);
        x = fit.left;
        y = fit.top;
        cw = fit.width();
        ch = fit.height();
    }
    HWND hwnd = CreateWindowExW(0, w->opts_.window_class.c_str(), w->opts_.title.c_str(), WS_OVERLAPPEDWINDOW,
                                x, y, cw, ch, nullptr, nullptr, inst, w.get());
    if (!hwnd) return nullptr;
    return w;
}

MainWindow::~MainWindow() {
    // Bekleyen kip pencereleri Hayir ile kapanir; run_modal cerceveleri bu nesneye bir daha dokunmaz.
    abandon_modals();
    std::erase(live_windows(), this);
    // Kaynaklar pencereyle birlikte birakilir (UI-05): UIA baglantisi, cizim hedefi, pencere.
    if (uia_) uia_->disconnect();
    if (gfx_) gfx_->release_target();
    if (hwnd_) {
        KillTimer(hwnd_, kAnimTimer);
        KillTimer(hwnd_, kTickTimer);
        KillTimer(hwnd_, kStripTimer);
        KillTimer(hwnd_, kBreatheTimer);
        KillTimer(hwnd_, kPulseTimer);
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (icon_small_) DestroyIcon(icon_small_);
    if (icon_big_) DestroyIcon(icon_big_);
}

void MainWindow::post_model(HWND hwnd, const ViewModel& vm) {
    auto* copy = new ViewModel(vm);
    if (!PostMessageW(hwnd, kMsgModel, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
}

void MainWindow::post_findings(HWND hwnd, const std::vector<FindingVM>& f) {
    auto* copy = new std::vector<FindingVM>(f);
    if (!PostMessageW(hwnd, kMsgFindings, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
}

void MainWindow::post_run(HWND hwnd, std::function<void()> fn) {
    auto* copy = new std::function<void()>(std::move(fn));
    if (!PostMessageW(hwnd, kMsgRun, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
}

std::size_t MainWindow::image_cache_size() const { return gfx_ ? gfx_->image_cache_size() : 0; }

bool MainWindow::live_region_px(RECT& out) const {
    if (!hwnd_ || !state_.live_view || state_.live_region.h <= 0 || state_.live_region.w <= 0) return false;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    // Ust kenar yukari yuvarlanir: kesirli DPI olceginde ust menunun son (kismi) piksel satirina dokunulmaz.
    out = {0, static_cast<LONG>(std::ceil(to_px(state_.live_region.y, scale()))), rc.right, rc.bottom};
    return out.top < out.bottom && out.right > 0;
}

void MainWindow::set_live_test(const core::measure::LiveSample& v) {
    screen_.set_live_sample(state_, v, now_ms());
    if (!live_view_visible()) return;  // baska sayfa / gorunmez pencere: cizim yok (sayfaya donunce en yeni ornek cizilir)
    RECT rc{};
    if (live_region_px(rc)) InvalidateRect(hwnd_, &rc, FALSE);
    else invalidate();  // bolge henuz bilinmiyor (ilk kare): tam cizim bolgeyi yazar
}

bool MainWindow::live_view_visible() const {
    return hwnd_ && activity_.get() == Visibility::Visible && state_.page == Page::Measurements && core::measure::live_view_shown(vm_.measure) &&
           !update_gate_shown(vm_);
}

void MainWindow::sync_live_view(bool was_shown) {
    const bool shown = core::measure::live_view_shown(vm_.measure);
    if (shown && !was_shown) {
        // Icerik alani canli gorunume gecer: gorunum Olcumler sayfasinin icerigidir (ust menu kalir, odak calinmaz).
        if (state_.page != Page::Measurements && !update_gate_shown(vm_)) {
            screen_.go(state_, Page::Measurements, now_ms());
            note_selection(nav_hit(static_cast<std::size_t>(Page::Measurements)));
            if (on_page_shown) on_page_shown(Page::Measurements);
        }
        // Odak "Testi baslat" dugmesindeyse ayni isi goren "Testi durdur" dugmesine tasinir (Enter / Bosluk durdurur).
        if (state_.focus == Hit::TestStart) set_focus(Hit::TestStop, state_.focus_visible);
    } else if (!shown && was_shown) {
        screen_.set_live_sample(state_, {}, now_ms());  // onizleme pikselleri kosu bitince birakilir
        // Onizlemenin hedef bitmap'i de (960x540) hemen birakilir: aksi halde bir sonraki resim cizimine (Oyunlar
        // sayfasi ikonlari) kadar onbellekte kalirdi. App son kareyi set_measure'dan once birakir (stop_live_view).
        if (gfx_) gfx_->drop_released_images();
        if (state_.focus == Hit::TestStop) set_focus(Hit::TestStart, state_.focus_visible);
    }
}

void MainWindow::set_measure(const core::measure::MeasureState& m) {
    const auto before = vm_.measure;
    vm_.measure = m;
    // Spec 2026-11-12 C "Son 3 test": yeni kayit basa eklenince acik ayrinti ayni testte kalir (baska teste kaymaz).
    if (state_.measure_recent_open >= 0) {
        const auto keys = [](const std::vector<core::measure::ResultView>& h) {
            std::vector<std::int64_t> k;
            k.reserve(h.size());
            for (const auto& r : h) k.push_back(r.when_unix_ms);
            return k;
        };
        state_.measure_recent_open = follow_open_row(state_.measure_recent_open, keys(before.history), keys(m.history));
    }
    // Test islemiyorsa kullanicinin sectigi kaynak korunur.
    if (!core::measure::busy(m)) vm_.measure.source = before.source;
    // M06: baslama, asama degisimi, durdurma, bitis/iptal/hata ekran okuyucuya bir kez duyurulur.
    if (const auto a = core::measure::measure_announcement(before, vm_.measure))
        announce(screen_.measure_announcement_text(*a, vm_.measure), a->completed);
    sync_live_view(core::measure::live_view_shown(before));
    update_measure_timer();
    invalidate();
}

void MainWindow::update_measure_timer() {
    // M04: test islerken gecen sure 1 Hz ile yenilenir (yalniz Olcumler sayfasi gorunurken cizilir); bitince durur.
    // R13: uzun test de gercek gecen sureyi ayni 1 Hz ile gosterir.
    const bool want = hwnd_ && (core::measure::busy(vm_.measure) || vm_.measure.endurance.running);
    if (want && !tick_timer_) tick_timer_ = SetTimer(hwnd_, kTickTimer, kTickIntervalMs, nullptr) != 0;
    else if (!want && tick_timer_) {
        if (hwnd_) KillTimer(hwnd_, kTickTimer);
        tick_timer_ = false;
    }
}

bool MainWindow::announce(const std::wstring& text, bool completed) { return uia_ && uia_->announce(text, completed); }

void MainWindow::set_live(const core::metrics::LiveSnapshot& l) {
    vm_.live = l;
    // Tepe izi ornek gelisinde ilerler (zamana bagli; ek kare yok). Azaltilmis / duraklatilmis harekette kademeli inis yok.
    update_live_peaks(live_peaks_, vm_.live_peak_pct, l, static_cast<std::int64_t>(GetTickCount64()),
                      state_.reduced_motion || state_.motion_paused);
    push_live_history(vm_.live_history, l);  // kutucuk ince grafigi: son ornekler (yalniz gercek degerler)
    // Yalniz Panel gorunurken ve yalniz kutucuk bolgesi yeniden cizilir (canli ornek baska hicbir karti etkilemez;
    // diger sayfalarda gereksiz kare yok).
    if (state_.page == Page::Overview) invalidate_tiles(true);
}

void MainWindow::set_speed(const ViewModel::SpeedView& v) {
    const auto before = vm_.speed;
    vm_.speed = v;
    // Bitis/iptal/hata ekran okuyucuya bir kez duyurulur (ilerleme 10 Hz'de duyurulmaz).
    if (before.running && !v.running) announce(screen_.speed_announcement_text(vm_.speed), true);
    if (state_.page == Page::Measurements) invalidate();  // diger sayfalarda gereksiz kare yok
    if (before.running != v.running) update_strip_timer();  // spec §3: hiz testi surerken serit durur
}

void MainWindow::set_updates(const ViewModel::UpdatesView& v) {
    const bool was_checking = vm_.updates.scan.state == core::drivers::ScanState::Checking;
    const bool was_installing = vm_.updates.install.running;
    const bool was_preparing = vm_.updates.local.stage != ViewModel::UpdatesView::LocalView::Stage::None;
    vm_.updates = v;
    // Spec H2: kurulum sonucu (ve yeniden baslatma notu) ekran okuyucuya bir kez duyurulur. ADR-032: paket hazirligi
    // (kopya / imza) kurulum baslamadan bittiyse sonucu da bir kez duyurulur.
    const bool preparing = v.local.stage != ViewModel::UpdatesView::LocalView::Stage::None;
    if ((was_installing || (was_preparing && !preparing)) && !v.install.running && !v.install.notice_key.empty())
        announce(screen_.updates_install_text(vm_), true);
    // Odak "Durdur"dayken arama bittiyse ayni yerdeki "Denetle" / "Yeniden dene"ye gecer (odak kaybolmaz).
    // Kurulumdan sonra satir kalktiysa "Kur"daki odak da ust karttaki dugmeye gecer (spec H2).
    state_.focus = updates_focus_after(state_.focus, v);
    // Sonuc (bulundu / guncel / denetlenemedi + neden) ekran okuyucuya bir kez duyurulur (ust kartin metni).
    if (was_checking && v.scan.state != core::drivers::ScanState::Checking) announce(screen_.updates_status_text(vm_), true);
    if (state_.page == Page::Updates) invalidate();
}

void MainWindow::set_cleanup(const ViewModel::CleanupView& c) {
    vm_.cleanup = c;
    invalidate();
}

void MainWindow::set_before_after(const ViewModel::BeforeAfterView& v) {
    vm_.before_after = v;
    if (state_.page == Page::Measurements) invalidate();  // kart Olcumler sayfasinda (spec 2026-11-12 B.4)
}

void MainWindow::set_check(const ViewModel::CheckView& v) {
    vm_.check = v;
    if (state_.page == Page::Readiness) invalidate();  // kart Oyun hazirligi sayfasinda (spec 2026-11-12 B.4)
}

void MainWindow::set_startup(const ViewModel::StartupView& v) {
    vm_.startup = v;
    invalidate();
}

void MainWindow::apply_settings_prefs(const SettingsView& s) {
    if (motion_original_ != s.system_motion_original) {
        motion_original_ = s.system_motion_original;
        read_motion_pref();
    }
    if (app_animations_ != s.app_animations) {
        app_animations_ = s.app_animations;
        read_motion_pref();
    }
}

void MainWindow::show_page(Page p) {
    screen_.go(state_, p, now_ms());
    note_selection(nav_hit(static_cast<std::size_t>(p)));
    if (on_page_shown) on_page_shown(p);
    invalidate();
}

void MainWindow::set_settings(const SettingsView& s) {
    vm_.settings = s;
    apply_settings_prefs(s);
    invalidate();
}

void MainWindow::set_view(const ViewModel& vm) {
    // Anahtar animasyonu yalniz gercek durum degisince calisir (ayni durumda yeniden animasyon yok).
    const PerfView old_perf = vm_.perf;
    const auto source = vm_.measure.source;
    const bool live_was = core::measure::live_view_shown(vm_.measure);
    const auto peaks = vm_.live_peak_pct;  // tepe izi yalniz set_live ile ilerler; uygulama modeli onu tasimaz
    auto live_history = std::move(vm_.live_history);  // ince grafik ornekleri de yalniz set_live ile ilerler
    vm_ = vm;
    vm_.live_peak_pct = peaks;
    vm_.live_history = std::move(live_history);
    apply_settings_prefs(vm.settings);
    vm_.perf = old_perf;
    if (!core::measure::busy(vm.measure)) vm_.measure.source = source;
    sync_live_view(live_was);
    update_measure_timer();
    set_perf(vm.perf);
}

void MainWindow::set_perf(const PerfView& pv) {
    sync_perf_animations(state_, vm_.perf, pv, theme_, now_ms());
    vm_.perf = pv;
    // Odak artik gecersiz bir ogede kaldiysa (ornek Evet dugmesi kayboldu) asamanin ilk ogesine gec.
    // Is surerken odak yerinde kalir (anahtar o sirada etkisiz; bitince ayni yere doner).
    const bool busy = pv.phase == core::perf::Phase::Planning || pv.phase == core::perf::Phase::Applying ||
                      pv.phase == core::perf::Phase::Restoring;
    const auto order = focus_order(state_);
    // Kip penceresi acikken odak karttadir ve kip durum makinesi yonetir; kapaninca onceki ogeye doner.
    if (!busy && !state_.modal && state_.focus != Hit::None && std::find(order.begin(), order.end(), state_.focus) == order.end()) {
        // Oncelik: Hayir (onay), Tekrar dene (kurtarma), anahtar; karsilama dugmesine kaymaz.
        Hit next = Hit::None;
        // B6-F1 (P12-B1): satir ici Panel onayinda varsayilan odak Hayir (kip penceresi onaylariyla ayni; yanlislikla Enter yazmaz).
        for (Hit want : {Hit::No, Hit::Retry, Hit::Toggle})
            if (std::find(order.begin(), order.end(), want) != order.end()) {
                next = want;
                break;
            }
        set_focus(next, state_.focus_visible);  // ekran okuyucu yeni odagi duyar
    }
    invalidate();
}

LRESULT CALLBACK MainWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* self = static_cast<MainWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->handle(msg, wp, lp);
}

void MainWindow::invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);  // pencere yokken InvalidateRect(nullptr) butun masaustunu cizdirir
}

void MainWindow::paint() {
    LARGE_INTEGER qpc0{}, qpf{};
    QueryPerformanceCounter(&qpc0);
    QueryPerformanceFrequency(&qpf);
    // Guncelleme bolgesinin dikdortgenleri (BeginPaint bolgeyi dogrular; once okunur). Panel hizli yolu, ayni karede
    // birden cok kucuk bolge (serit + dugme + kutucuklar) gecersizken de tam kare cizmemek icin bunlara bakar.
    std::vector<RECT> update_rects;
    if ((state_.page == Page::Overview || (state_.page == Page::Games && !state_.pulse_regions.empty())) && !hits_.items.empty()) {
        if (const HRGN rgn = CreateRectRgn(0, 0, 0, 0)) {
            const int kind = GetUpdateRgn(hwnd_, rgn, FALSE);
            if (kind == SIMPLEREGION || kind == COMPLEXREGION) {
                if (const DWORD bytes = GetRegionData(rgn, 0, nullptr); bytes >= sizeof(RGNDATAHEADER)) {
                    std::vector<char> buf(bytes);
                    auto* data = reinterpret_cast<RGNDATA*>(buf.data());
                    if (GetRegionData(rgn, bytes, data) == bytes && data->rdh.iType == RDH_RECTANGLES && data->rdh.nCount <= 64) {
                        const RECT* r = reinterpret_cast<const RECT*>(data->Buffer);
                        update_rects.assign(r, r + data->rdh.nCount);
                    }
                }
            }
            DeleteObject(rgn);
        }
    }
    PAINTSTRUCT ps;
    BeginPaint(hwnd_, &ps);
    if (IsIconic(hwnd_)) {  // simge durumunda cizim yok; geri acilista WM_SIZE tam yeniden cizer
        EndPaint(hwnd_, &ps);
        return;
    }
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    // Panel hizli yollari (spec 2026-11-12 F): yalniz Performans dugmesi (nefes) ve / veya canli kutucuklar (1 Hz ornek,
    // deger gecisi) — ve ayni karede RGB serit — gecersizse yalniz onlar cizilir; isabet kutulari, odak ve sayfanin
    // geri kalani degismez (onceki tam kare gecerli kalir). Kip penceresi acikken hizli yol yoktur: bolge karartmanin
    // ve kartin altindadir, tam kare cizilir (yalniz gecersiz bolgeye yazar).
    const bool modal = state_.modal != nullptr;
    if (!modal && (paint_panel_fast(ps.hdc, update_rects) || paint_pulse_fast(ps.hdc, update_rects))) {
        EndPaint(hwnd_, &ps);
        const auto now = now_ms();
        if (!anim_timer_ && activity_.get() == Visibility::Visible && screen_.animating(state_, now)) {
            anim_live_only_ = !screen_.animating(state_, now, false);
            SetTimer(hwnd_, kAnimTimer, kAnimIntervalMs, nullptr);
            anim_timer_ = true;
        }
        LARGE_INTEGER qpc1{};
        QueryPerformanceCounter(&qpc1);
        const double ms = static_cast<double>(qpc1.QuadPart - qpc0.QuadPart) * 1000.0 / static_cast<double>(qpf.QuadPart);
        ++paint_stats_.count;
        paint_stats_.total_ms += ms;
        paint_stats_.max_ms = std::max(paint_stats_.max_ms, ms);
        return;
    }
    // RGB serit hizli yolu: yalniz serit gecersizse (serit zamanlayicisi) yalniz serit cizilir; isabet haritasi,
    // UIA modeli ve sayfa cizimi degismez (onceki tam kare gecerli kalir).
    {
        const LONG strip_px = static_cast<LONG>(std::ceil(to_px(theme_.strip_h, scale())));
        if (!modal && !hits_.items.empty() && strip_px > 0 && ps.rcPaint.top >= 0 && ps.rcPaint.bottom <= strip_px) {
            if (gfx_->bind_dc(ps.hdc, rc.right, std::min(strip_px, rc.bottom), dpi_) && gfx_->begin()) {
                screen_.paint_strip(*gfx_, static_cast<float>(rc.right) / scale(), state_, now_ms());
                if (!gfx_->end()) invalidate();
            }
            EndPaint(hwnd_, &ps);
            return;
        }
    }
    // Canli test gorunumu hizli yolu (spec 2026-11-12 A4): yalniz canli bolge gecersizse (yeni onizleme karesi / canli
    // sayi) yalniz o bolge cizilir; ust menu, isabet kutulari ve odak degismez. Canli ogelerin UIA adlari guncellenir.
    {
        RECT live{};
        if (!modal && !hits_.items.empty() && live_view_visible() && live_region_px(live) && ps.rcPaint.top >= live.top) {
            if (gfx_->bind_dc(ps.hdc, live, dpi_) && gfx_->begin()) {
                gfx_->set_offset(0, -static_cast<float>(live.top) / scale());  // cizim pencere koordinatinda kalir
                HitMap fresh;
                screen_.paint_live_region(*gfx_, state_, vm_, now_ms(), fresh);
                gfx_->set_offset(0, 0);
                for (auto& [id, meta] : fresh.meta)
                    if (const auto it = hits_.meta.find(id); it != hits_.meta.end()) it->second = std::move(meta);
                uia_model_->hits = hits_;
                if (!gfx_->end()) invalidate();
            }
            EndPaint(hwnd_, &ps);
            LARGE_INTEGER qpc1{};
            QueryPerformanceCounter(&qpc1);
            const double ms = static_cast<double>(qpc1.QuadPart - qpc0.QuadPart) * 1000.0 / static_cast<double>(qpf.QuadPart);
            ++paint_stats_.count;
            ++paint_stats_.live;
            paint_stats_.total_ms += ms;
            paint_stats_.max_ms = std::max(paint_stats_.max_ms, ms);
            return;
        }
    }
    // Cizim WM_PAINT'in DC'sine yapilir; EndPaint en sonda (D2D EndDraw DC'ye yazar).
    if (!gfx_->bind_dc(ps.hdc, rc.right, rc.bottom, dpi_) || !gfx_->begin()) {
        EndPaint(hwnd_, &ps);
        return;
    }
    const float w = static_cast<float>(rc.right) / scale();
    const float h = static_cast<float>(rc.bottom) / scale();
    const auto now = now_ms();
    hits_ = screen_.paint(*gfx_, w, h, state_, vm_, now);
    if (state_.page == Page::Readiness) {
        // RDY-08/09: islem bitince odaktaki satir dugmesi kalktiysa (ornek: Geri al -> Duzelt) odak ayni satirda kalir.
        if (const Hit fb = readiness_focus_fallback(state_.focus, hits_); fb != state_.focus) {
            set_focus(fb, state_.focus_visible);
            invalidate();  // halka yeni ogede bir sonraki karede
        }
    }
    uia_model_->hits = hits_;
    uia_model_->scale = scale();
    uia_model_->focus = state_.focus;
    // Spec 2026-11-12 E: kip penceresi acildi / kapandi. Olaylar ancak yeni kare kip ogelerini (ya da geri donen sayfa
    // ogelerini) tasirken yayinlanir: agac degisti, pencere acildi, odak iceri tasindi / eski ogeye dondu.
    if (modal_uia_event_) {
        modal_uia_event_ = false;
        if (uia_) {
            uia_->notify_structure_changed();
            if (state_.modal) uia_->notify_window_opened(Hit::Modal);
            uia_->notify_focus(state_.focus);
        }
    }
    // TH-04: secim olayi ancak yeni kare ogenin Selected durumunu tasirken yayinlanir (model guncellemesi
    // bir sonraki kareye kalmis olabilir; o zamana kadar bekler).
    if (pending_selection_ != Hit::None) {
        const auto it = hits_.meta.find(pending_selection_);
        if (it != hits_.meta.end() && it->second.selected) {
            if (uia_) uia_->notify_selection(pending_selection_);
            pending_selection_ = Hit::None;
        }
    }
    const bool end_ok = gfx_->end();
    EndPaint(hwnd_, &ps);
    {
        LARGE_INTEGER qpc1{};
        QueryPerformanceCounter(&qpc1);
        const double ms = static_cast<double>(qpc1.QuadPart - qpc0.QuadPart) * 1000.0 / static_cast<double>(qpf.QuadPart);
        ++paint_stats_.count;
        paint_stats_.total_ms += ms;
        paint_stats_.max_ms = std::max(paint_stats_.max_ms, ms);
    }
    if (!end_ok) invalidate();  // cihaz kayboldu: sonraki karede yeniden olustur
    const bool anim = screen_.animating(state_, now);
    if (anim && !anim_timer_ && activity_.get() == Visibility::Visible) {
        anim_live_only_ = !screen_.animating(state_, now, false);
        SetTimer(hwnd_, kAnimTimer, kAnimIntervalMs, nullptr);
        anim_timer_ = true;
    }
    update_strip_timer();  // ayar/oyun/test/hareket durumu bu karede degismis olabilir
}

void MainWindow::activate(Hit h) {
    // Spec 2026-11-12 E: kip penceresi acikken yalniz kip ogeleri calisir (fare, UIA). Sayfanin bayat ogesi islenmez.
    if (state_.modal) {
        if (!modals_.empty()) {
            ModalState& st = modals_.back()->st;
            const int choice = st.choice;
            if (h == Hit::Modal) modal_key(st, ModalKey::Escape);  // UIA Window.Close = Esc (Hayir)
            else if (const ModalFocus f = modal_focus_of(h); f.item != ModalItem::None && hits_.activatable(h)) modal_activate(st, f);
            if (st.choice != choice && st.choice >= 0) note_selection(hit_of(HitKind::ModalChoice, static_cast<std::size_t>(st.choice)));  // TH-04
        }
        modal_sync();
        return;
    }
    // DS-05: devre disi (veya salt okunur) oge hicbir yoldan etkinlesmez: fare, Enter/Bosluk, UIA.
    // Son cizimde kaydi olmayan sabit kimlikler eski davranisla (asama denetimleri asagida) yorumlanir.
    if (hits_.meta.count(h) && !hits_.activatable(h)) return;
    // Zorunlu guncelleme karti acikken yalniz kart dugmeleri etkinlesir (bayat sekme/sayfa kimligi UIA ile de islenmez).
    if (update_gate_shown(vm_)) {
        if (!update_gate_hit(h) || !hits_.activatable(h)) return;
        if (h == Hit::UpdateNow) {
            if (vm_.update.step == ViewModel::UpdateView::Step::Idle && on_update_now) on_update_now();
        } else if (h == Hit::UpdateQuit) {
            if (on_update_quit) on_update_quit();
        } else if (h == Hit::UpdateRestore) {
            if (on_update_restore) on_update_restore();
        } else if (h == Hit::UpdateOpenPage) {
            if (on_update_open_page) on_update_open_page();
        } else if (h == Hit::UpdateRepairService) {
            if (vm_.update.step == ViewModel::UpdateView::Step::Idle && on_update_repair_service) on_update_repair_service();
        }
        invalidate();
        return;
    }
    // RDY-01: satir kimlikleri cakismasiz bloklardadir (kHitRanges); yonlendirme classify_hit ile yapilir.
    // Satir kimligi yalniz son karede cizilmis ve etkinse islenir (eski sayfanin bayat UIA ogesi islenmez).
    if (const HitSlot slot = classify_hit(h); slot.kind != HitKind::None && slot.kind != HitKind::Nav && slot.kind != HitKind::Fixed) {
        if (slot.kind == HitKind::GameCard) {
            // Oyunlar (spec 2026-11-12 B2): karti sec; secili oyun seridi izgaranin altinda acilir ve gorunur alana
            // getirilir. Yalniz gorunum durumu: oyun listesi okunmaz / yazilmaz.
            if (hits_.activatable(h) && slot.index < vm_.games.size()) {
                const auto key = game_row_key(vm_.games[slot.index]);
                if (key != state_.games_selected) note_selection(h);  // TH-04: UIA secim olayi
                state_.games_selected = key;
                state_.games_reveal_strip = true;
            }
        } else if (slot.kind == HitKind::HistoryRow) {
            // Gecmis: journal satirinin ayrintisini ac/kapa. Yalniz gorunum durumu; journal okunmaz/yazilmaz.
            if (hits_.activatable(h) && slot.index < vm_.history.view.rows.size()) {
                const auto key = history_row_key(vm_.history.view.rows[slot.index]);
                const bool opening = !state_.history_open.erase(key);
                if (opening) state_.history_open.insert(key);
                // Spec 2026-11-12 B6: satir motion.row_expand ile acilir / kapanir (hareket kapaliysa / duraklatildiysa aninda).
                state_.history_anim_key = key;
                state_.history_anim = Tween(opening ? 0.f : 1.f);
                state_.history_anim.retarget(opening ? 1.f : 0.f, now_ms(),
                                             effective_ms(theme_.motion.row_expand, state_.reduced_motion || state_.motion_paused));
            }
        } else if (slot.kind == HitKind::HistoryGo) {
            // Spec 2026-11-12 B6: "Geri almak icin Panel'e git" — kaydin sahibi olan sayfaya gecer (geri alma orada yapilir;
            // journal okunmaz / yazilmaz).
            if (hits_.activatable(h) && slot.index < vm_.history.view.rows.size()) {
                const Page pg = history_undo_page(vm_.history.view.rows[slot.index].scope);
                screen_.go(state_, pg, now_ms());
                // Ust menude secili gorunen sekme (Baslangic bolumu icin "Sistem araclari").
                note_selection(nav_hit(static_cast<std::size_t>(systools_page(pg) ? Page::Cleanup : pg)));
                if (on_page_shown) on_page_shown(pg);
            }
        } else if (slot.kind == HitKind::MeasureRecent) {
            // Olcumler "Son 3 test" (spec 2026-11-12 C): o testin sonuc ayrintisini satirin altinda ac/kapa. Yalniz
            // gorunum durumu; gecmis okunmaz / yazilmaz.
            if (hits_.activatable(h) && slot.index < vm_.measure.history.size()) {
                const int k = static_cast<int>(slot.index);
                state_.measure_recent_open = state_.measure_recent_open == k ? -1 : k;
            }
        } else if (slot.kind == HitKind::ChkDetail) {
            // Oyun hazirligi kutucugu (spec 2026-11-14 B11): "Ayrinti" ac/kapa. Yalniz gorunum durumu; bulgu kimligiyle
            // tutulur (yeniden taramada indeks kaysa da ayni kutucuk acik kalir).
            if (hits_.activatable(h) && slot.index < vm_.readiness.checks.size()) {
                const auto& id = vm_.readiness.checks[slot.index].id;
                if (!state_.readiness_detail_open.erase(id)) state_.readiness_detail_open.insert(id);
            }
        } else if (slot.kind == HitKind::ChkGroup) {
            // "Sistem kontrolu" grup kutucugu: o grubun bulgularini ac/kapa. Bolumun tamami aciksa (ac/kapa dugmesi)
            // tiklanan grup kapanir, digerleri acik kalir. Yalniz gorunum durumu.
            if (hits_.activatable(h) && slot.index < static_cast<std::size_t>(kFindingGroupCount)) {
                const std::uint32_t bit = 1u << slot.index;
                if (state_.readiness_checkup_open) {
                    state_.readiness_checkup_open = false;
                    state_.readiness_group_open = ((1u << kFindingGroupCount) - 1u) & ~bit;
                } else {
                    state_.readiness_group_open ^= bit;
                }
            }
        } else if (hits_.activatable(h)) {
            dispatch_row_hit(h, vm_, *this);
        }
        invalidate();
        return;
    }
    const int i = static_cast<int>(h);
    if (i >= 0 && i < static_cast<int>(kPageCount)) {
        screen_.go(state_, static_cast<Page>(i), now_ms());
        note_selection(h);  // TH-04: UIA TabItem secimi
        if (on_page_shown) on_page_shown(static_cast<Page>(i));
    } else if (h >= Hit::SysJump0 && h <= Hit::SysJump3) {
        // Sistem araclari "Bolumler:" seridi: sonraki cizimde ilgili bolum basligi govdenin ustune kaydirilir.
        state_.systools_reveal = static_cast<int>(h) - static_cast<int>(Hit::SysJump0) + 1;
        note_selection(h);
    } else if (h == Hit::SysTabCleanup) {
        screen_.go(state_, Page::Cleanup, now_ms());
        note_selection(h);
        if (on_page_shown) on_page_shown(Page::Cleanup);
    } else if (h == Hit::Toggle) {
        if (on_toggle) on_toggle();
    } else if (h == Hit::Yes || h == Hit::No) {
        if (on_answer) on_answer(h == Hit::Yes);
    } else if (h == Hit::Retry) {
        if (on_toggle) on_toggle();  // kurtarma: geri yukleme onayini tekrar ac
    } else if (h == Hit::SourceBuiltin || h == Hit::SourceGame) {
        if (!core::measure::busy(vm_.measure)) {
            const auto next = h == Hit::SourceBuiltin ? core::measure::TestSource::Builtin : core::measure::TestSource::InGame;
            if (next != vm_.measure.source) note_selection(h);
            vm_.measure.source = next;
        }
    } else if (h == Hit::ScopeGeneral || h == Hit::ScopeBalance) {
        // Kapsam yalniz Uygulama Testi seciliyken cizilir; test islerken ya da uzun test surerken degismez.
        if (!core::measure::busy(vm_.measure) && !vm_.measure.endurance.running) {
            const auto next = h == Hit::ScopeBalance ? core::measure::BuiltinScope::Balance : core::measure::BuiltinScope::General;
            if (next != vm_.measure.scope) note_selection(h);
            vm_.measure.scope = next;
            if (on_test_scope) on_test_scope(vm_.measure.scope);
        }
    } else if (h == Hit::ModeQuick || h == Hit::ModeFull) {
        if (!core::measure::busy(vm_.measure)) {
            const auto next = h == Hit::ModeFull ? core::measure::TestMode::Full : core::measure::TestMode::Quick;
            if (next != vm_.measure.mode) note_selection(h);
            vm_.measure.mode = next;
            if (on_test_mode) on_test_mode(vm_.measure.mode);
        }
    } else if (h == Hit::TestStart) {
        if (core::measure::can_start(vm_.measure) && on_test_start) on_test_start(vm_.measure.source);
    } else if (h == Hit::TestStop) {
        if (core::measure::can_stop(vm_.measure) && on_test_stop) on_test_stop();
    } else if (h == Hit::EnduranceStart) {
        if (core::measure::endurance_can_start(vm_.measure) && on_endurance_start) on_endurance_start();
    } else if (h == Hit::EnduranceStop) {
        if (vm_.measure.endurance.running && on_endurance_stop) on_endurance_stop();
    } else if (h == Hit::ProofStart) {
        if (core::measure::proof_can_start(vm_.measure) && on_proof_start) on_proof_start();
    } else if (h == Hit::ProofStop) {
        if (vm_.measure.proof.running() && on_proof_stop) on_proof_stop();
    } else if (h == Hit::BalanceReport) {
        if (core::measure::balance_report_offered(vm_.measure) && !vm_.balance_report.busy && on_balance_report) on_balance_report();
    } else if (h == Hit::LiveView) {
        // A4: test islerken degismez (onizleme acik / kapali kosunun kosuludur).
        if (!core::measure::busy(vm_.measure) && !vm_.measure.endurance.running && on_live_view) on_live_view(!vm_.measure.live_view.user_on);
    } else if (h == Hit::LiveStep || h == Hit::LiveFps || h == Hit::LiveLoad) {
        // salt okunur canli gostergeler: etkinlesmez
    } else if (h == Hit::SpeedStart) {
        if (!vm_.speed.running && !vm_.game_gate_blocked && !core::measure::busy(vm_.measure) && !vm_.measure.endurance.running &&
            on_speed_start)
            on_speed_start();
    } else if (h == Hit::SpeedStop) {
        if (vm_.speed.running && on_speed_stop) on_speed_stop();
    } else if (h == Hit::SpeedServerPing) {
        if (!vm_.speed.running && !vm_.speed.server_running && !vm_.game_gate_blocked && !core::measure::busy(vm_.measure) &&
            !vm_.measure.endurance.running && on_speed_server_ping)
            on_speed_server_ping();
    } else if (h == Hit::UpdatesCheck) {
        if (vm_.updates.scan.state != core::drivers::ScanState::Checking && !vm_.game_gate_blocked && on_updates_check) on_updates_check();
    } else if (h == Hit::UpdatesCancel) {
        if (vm_.updates.scan.state == core::drivers::ScanState::Checking && on_updates_cancel) on_updates_cancel();
    } else if (h == Hit::CleanScan || h == Hit::CleanRun || h == Hit::CleanYes || h == Hit::CleanNo ||
               h == Hit::CleanStop || h == Hit::CleanScanCancel || h == Hit::CleanStorageSettings) {
        if (on_cleanup)
            on_cleanup(h == Hit::CleanScan         ? CleanAction::Scan
                       : h == Hit::CleanRun        ? CleanAction::Run
                       : h == Hit::CleanYes        ? CleanAction::Yes
                       : h == Hit::CleanNo         ? CleanAction::No
                       : h == Hit::CleanScanCancel ? CleanAction::ScanCancel
                       : h == Hit::CleanStorageSettings ? CleanAction::StorageSettings
                                                   : CleanAction::Stop);
    } else if (h == Hit::CheckStart || h == Hit::CheckYes || h == Hit::CheckNo) {
        if (on_check) on_check(h == Hit::CheckStart ? CheckAction::Start : h == Hit::CheckYes ? CheckAction::Yes : CheckAction::No);
    } else if (h == Hit::StartupApply || h == Hit::StartupYes || h == Hit::StartupNo) {
        if (on_startup)
            on_startup(h == Hit::StartupApply ? StartupAction::Apply : h == Hit::StartupYes ? StartupAction::Yes : StartupAction::No);
    } else if (h == Hit::Details) {
        state_.details_open = !state_.details_open;
        state_.card_fade = Tween(0);
        state_.card_fade.retarget(1, now_ms(), effective_ms(theme_.motion.details, state_.reduced_motion));
    } else if (h == Hit::PerfAdvanced) {
        state_.advanced_open = !state_.advanced_open;  // T04: yalniz gorunum durumu (kaydedilmez)
        state_.card_fade = Tween(0);
        state_.card_fade.retarget(1, now_ms(), effective_ms(theme_.motion.details, state_.reduced_motion));
    } else if (h == Hit::PerfTips) {
        state_.perf_tips_open = !state_.perf_tips_open;  // T20: yalniz gorunum durumu (kaydedilmez)
        state_.card_fade = Tween(0);
        state_.card_fade.retarget(1, now_ms(), effective_ms(theme_.motion.details, state_.reduced_motion));
    } else if (h == Hit::BiosGuideToggle) {
        state_.bios_guide_open = !state_.bios_guide_open;  // T20: yalniz gorunum durumu; firmware'e dokunulmaz
    } else if (h == Hit::SettingsLinkEula || h == Hit::SettingsLinkPrivacy || h == Hit::SettingsLinkTransparency) {
        // T20: yasal sayfa; yalniz kullanici tiklayinca tarayicida acilir (App URL izin listesini uygular).
        const std::size_t li = h == Hit::SettingsLinkEula ? 0 : h == Hit::SettingsLinkPrivacy ? 1 : 2;
        if (on_updates_link) on_updates_link(std::string(core::guided::kLegalLinks[li].url));
    } else if (h == Hit::WelcomeStart) {
        if (on_welcome_dismiss) on_welcome_dismiss();
    } else if (h == Hit::GamesAddExe) {
        if (on_games_add_exe) on_games_add_exe();
    } else if (h == Hit::GamesGuides) {
        state_.games_guides_open = !state_.games_guides_open;
    } else if (h == Hit::ChkCheckup) {
        state_.readiness_checkup_open = !state_.readiness_checkup_open;
        state_.readiness_group_open = 0;  // B11: bolum ac/kapa tum gruplari birlikte acar / kapatir
    } else if (h == Hit::CleanMore) {
        state_.cleanup_more_open = !state_.cleanup_more_open;  // B12: uzun kategori listesi (yalniz gorunum durumu)
    } else if (h == Hit::GamesWhy) {
        // Spec 2026-11-12 B2: "Neden?" satiri kalkti (metinleri oyun kartinin UIA HelpText'inde); kimlik korunur, artik
        // cizilmez ve etkinlesmez.
    } else if (h == Hit::GamesFilterAll || h == Hit::GamesFilterProtected || h == Hit::GamesFilterAdvice) {
        // Yalniz gorunum suzgeci: oyun listesi okunmaz/yazilmaz; liste basa doner.
        const auto next = static_cast<GamesFilter>(static_cast<int>(h) - static_cast<int>(Hit::GamesFilterAll));
        if (next != state_.games_filter) note_selection(h);  // TH-04: UIA secim olayi
        state_.games_filter = next;
        state_.scroll = 0;
    } else if (h == Hit::GamesRescan) {
        // Oyun tespiti engel kartindaki "Yeniden tara" ile ayni gercek tarama (kitaplik + surec listesi).
        if (!vm_.detection_retrying && on_detection_retry) on_detection_retry();
    } else if (h == Hit::HistoryFilterAll || h == Hit::HistoryFilterPerf || h == Hit::HistoryFilterFixes ||
               h == Hit::HistoryFilterStartup) {
        // Yalniz gorunum filtresi: journal okunmaz/yazilmaz; liste basa doner.
        // Spec 2026-11-14 B13: alt suzgec (Tumu cipi cizili; radyo dugmesi: secili cipe yeniden basmak degistirmez).
        const auto next = static_cast<HistoryFilter>(static_cast<int>(h) - static_cast<int>(Hit::HistoryFilterAll));
        if (next != state_.history_filter) note_selection(history_filter_hit(next));  // TH-04: UIA secim olayi
        state_.history_filter = next;
        state_.scroll = 0;
    } else if (h == Hit::HistoryTabAll || h == Hit::HistoryTabChanges || h == Hit::HistoryTabTests) {
        // B13 gorunum cipleri (Tumu / Degisiklikler / Testler): yalniz gorunum; journal ve test gecmisi okunmaz/yazilmaz.
        const auto next = static_cast<HistoryTab>(static_cast<int>(h) - static_cast<int>(Hit::HistoryTabAll));
        if (next != state_.history_tab) note_selection(h);
        state_.history_tab = next;
        state_.scroll = 0;
    } else if (h == Hit::PerfKeep) {
        if (on_perf_keep) on_perf_keep();
    } else if (h == Hit::PerfDriftReapply) {
        if (on_perf_drift_reapply) on_perf_drift_reapply();
    } else if (h == Hit::PerfDriftKeep) {
        if (on_perf_drift_keep) on_perf_drift_keep();
    } else if (h == Hit::StartupRescan) {
        if (on_startup) on_startup(StartupAction::Rescan);
    } else if (h == Hit::OpenMeasure) {
        screen_.go(state_, Page::Measurements, now_ms());
        if (on_page_shown) on_page_shown(Page::Measurements);
    } else if (h == Hit::OpenReadiness) {
        // Panel "Duzelt" (P12-B1): sayfa degismez; otomatik duzeltilebilen bulgular tek Evet/Hayir'da uygulanir.
        // Duzeltilecek bir sey yoksa dugme devre disi cizilir (etkinlesmez).
        if (on_readiness) on_readiness(ReadinessAction::FixAll);
    } else if (h == Hit::DetectionRetry) {
        if (on_detection_retry) on_detection_retry();
    } else if (h == Hit::ChkRescan || h == Hit::ChkNetTest) {
        if (on_readiness) on_readiness(h == Hit::ChkRescan ? ReadinessAction::Rescan : ReadinessAction::NetTest);
    } else if (h == Hit::Autostart) {
        if (vm_.settings.autostart_known && on_autostart) on_autostart(!vm_.settings.autostart);
    } else if (h == Hit::AutostartNoticeOff) {
        // Spec G: bildirimdeki "Kapat" Ayarlar'daki Windows ile baslat anahtarini kapatir.
        if (vm_.settings.autostart_known && on_autostart) on_autostart(false);
    } else if (h == Hit::AutostartNotice || h == Hit::InstallOfferText) {
        // salt okunur bildirim metni: etkinlesmez
    } else if (h == Hit::InstallOfferInstall) {
        if (vm_.install_offer && on_install_offer) on_install_offer();
    } else if (h == Hit::SettingsGamesShow) {
        state_.settings_games_open = !state_.settings_games_open;
    } else if (h == Hit::SettingsAdvanced) {
        state_.settings_advanced_open = !state_.settings_advanced_open;
    } else if (h == Hit::SettingsLicenses) {
        state_.settings_licenses_open = !state_.settings_licenses_open;
    } else if (h == Hit::AppAnimations) {
        if (on_app_animations) on_app_animations(!vm_.settings.app_animations);
    } else if (h == Hit::SettingsStrip) {
        if (on_strip_enabled) on_strip_enabled(!vm_.settings.strip_enabled);
    } else if (h == Hit::SettingsCleanupHint) {
        if (on_cleanup_hint) on_cleanup_hint(!vm_.settings.cleanup_hint);
    } else if (h == Hit::TopbarStatus) {
        // salt okunur gosterge: etkinlesmez
    } else if (h == Hit::SupportDumps) {
        if (!vm_.settings.bundle_busy && on_bundle_dumps) on_bundle_dumps(!vm_.settings.bundle_include_dumps);
    } else if (h == Hit::SettingsUpdateCheck) {
        if (on_update_check) on_update_check();
    } else if (h == Hit::SettingsLanguage) {
        if (on_language) on_language();
    } else if (h == Hit::SupportCreate) {
        if (!vm_.settings.bundle_busy && on_bundle_create) on_bundle_create();
    } else if (h == Hit::HistoryExport) {
        if (vm_.history.export_enabled && on_history_export) on_history_export();  // R16: veri yokken devre disi
    }
    invalidate();
}

void MainWindow::move_focus(int delta) {
    // TH-01/DS-05: devre disi veya ogesi olmayan duraklar atlanir. RDY-10: yeni odak gorunur alana kaydirilir.
    // RDY-09: odak devre disi kalmis bir dugmedeyse (is suruyor) Tab niyet sirasindaki yerinden devam eder.
    const auto order = focus_order(state_, hits_);
    if (order.empty()) return;
    set_focus(focus_step(focus_order(state_), order, state_.focus, delta), true);
    state_.reveal_focus = true;
    invalidate();
}

bool test_background_mode() noexcept {
    static const bool on = [] {
        wchar_t v[8]{};
        return GetEnvironmentVariableW(L"EF_TEST_BACKGROUND", v, 8) == 1 && v[0] == L'1';
    }();
    return on;
}

LRESULT MainWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (opts_.activate_message && msg == opts_.activate_message) {
        if (test_background_mode()) return 0;  // testte one alma yok
        ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(hwnd_);
        return 0;
    }
    switch (msg) {
        case WM_CREATE: {
            dpi_ = GetDpiForWindow(hwnd_);
            // Baslik cubugu: tema ile ayni (koyu/acik/yuksek kontrast; Windows 11 oznitelikleri, eski surumde yok sayilir).
            if (opts_.smoke) SetTimer(hwnd_, kSmokeTimer, 200, nullptr);
            read_contrast_pref();  // baslik cubugu rengi artik HWND ile ayarlanabilir
            update_window_icons();
            // UI-08: logo acilista bir kez belirir (azaltilmis harekette aninda); sonra statik.
            state_.brand_reveal = Tween(0);
            state_.brand_reveal.retarget(1, now_ms(), effective_ms(theme_.motion.logo_reveal, state_.reduced_motion));
            uia_model_->hwnd = hwnd_;
            uia_model_->window_name = opts_.title;
            {
                const HWND self = hwnd_;
                uia_model_->activate = [self](Hit h) { PostMessageW(self, kMsgUiaActivate, static_cast<WPARAM>(static_cast<int>(h) + 1), 0); };
            }
            uia_ = std::make_unique<UiaHost>(uia_model_);
            return 0;
        }
        case WM_GETOBJECT:
            if (uia_) {
                if (const LRESULT r = uia_->on_get_object(wp, lp)) return r;
            }
            break;
        case kMsgUiaActivate: {
            const Hit h = static_cast<Hit>(static_cast<int>(wp) - 1);
            // Kip penceresi (spec 2026-11-12 E): acikken yalniz kip ogeleri, kapaliyken yalniz sayfa ogeleri islenir
            // (bayat UIA ogesi odagi tasimaz). Hit::Modal = Window.Close: odak tasinmaz.
            const bool modal_item = h == Hit::Modal || modal_focus_of(h).item != ModalItem::None;
            if (modal_item != (state_.modal != nullptr)) return 0;
            if (h == Hit::Modal) {
                activate(h);
                return 0;
            }
            if (h != Hit::None) {
                set_focus(h, true);
                state_.reveal_focus = true;  // ekran okuyucuyla calistirilan oge ekranda gorunsun
                activate(h);
            }
            return 0;
        }
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
            read_contrast_pref();
            invalidate();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paint();
            return 0;
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) invalidate();  // DC hedefi her karede istemci boyutuna baglanir
            update_visibility();
            return 0;
        case WM_SHOWWINDOW:
            PostMessageW(hwnd_, kMsgVisibility, 0, 0);  // gosterim tamamlaninca durumu oku
            break;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            const auto* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            update_window_icons();  // yeni monitor DPI'sinde bulaniklasmis ikon kalmaz
            invalidate();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            const UINT dpi = hwnd_ ? GetDpiForWindow(hwnd_) : 96;
            RECT r{0, 0, MulDiv(static_cast<int>(theme_.min_window_w), static_cast<int>(dpi), 96),
                   MulDiv(static_cast<int>(theme_.min_window_h), static_cast<int>(dpi), 96)};
            AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
            // Uyumluluk B6 (bosluk 18): en kucuk izleme boyutu pencerenin ekranindaki calisma alanini asamaz.
            PxSize min{r.right - r.left, r.bottom - r.top};
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (hwnd_ && GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi))
                min = cap_min_track(min, {mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom});
            mmi->ptMinTrackSize.x = min.w;
            mmi->ptMinTrackSize.y = min.h;
            return 0;
        }
        case WM_SETTINGCHANGE:
            read_motion_pref();
            read_contrast_pref();
            invalidate();
            return 0;
        case WM_TIMER:
            if (wp == kSmokeTimer) {
                KillTimer(hwnd_, kSmokeTimer);
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            } else if (wp == kAnimTimer) {
                // Spec 2026-11-12 F: yalniz canli kutucuk gecisi suruyorsa yalniz kutucuk bolgesi gecersiz kilinir.
                const auto now = now_ms();
                if (screen_.animating(state_, now, false)) {
                    anim_live_only_ = false;
                    invalidate();
                } else if (screen_.live_animating(state_, now)) {
                    // Kutucuk gecisi ~30 kare/sn (her ikinci tik): 250 ms'lik gecis icin yeterli, cizim yuku yari yariya.
                    if ((++anim_live_tick_ & 1u) == 0) invalidate_tiles();
                } else {
                    KillTimer(hwnd_, kAnimTimer);
                    anim_timer_ = false;
                    if (anim_live_only_) invalidate_tiles();  // son kare hedef degerde
                    else invalidate();
                    anim_live_only_ = true;
                }
            } else if (wp == kTickTimer) {
                RECT live{};
                if (live_view_visible() && live_region_px(live)) InvalidateRect(hwnd_, &live, FALSE);  // gecen sure: yalniz canli bolge
                else if (state_.page == Page::Measurements && activity_.get() == Visibility::Visible) invalidate();
            } else if (wp == kStripTimer) {
                update_strip_timer();
                if (strip_timer_) invalidate_strip();
            } else if (wp == kModalTimer) {
                // Geri sayimli kip pencereleri (alttakiler dahil): sure dolunca yanit Hayir'dir.
                bool any = false;
                for (ModalFrame* f : modals_) {
                    modal_tick(f->st);
                    any = any || (f->st.spec.countdown_s > 0 && !f->st.done);
                }
                if (!any) KillTimer(hwnd_, kModalTimer);
                invalidate();
            } else if (wp == kPulseTimer) {
                update_strip_timer();  // kapilar (on plan, pil, test, sayfa) degistiyse zamanlayici durur
                if (pulse_timer_) invalidate_pulse();
            } else if (wp == kBreatheTimer) {
                update_strip_timer();  // kapilar (on plan, pil, oyun, test) degistiyse zamanlayici durur
                RECT ring{};
                if (breathe_timer_ && dip_region_px(state_.power_region, ring)) {
                    // Kalp atisinin dinlenme suresinde kare duragandir: bir kez cizilir, sonraki vurusa kadar cizilmez.
                    const bool rest = state_.page == Page::Overview && vm_.perf.phase == core::perf::Phase::Off &&
                                      heartbeat_resting(now_ms(), theme_.motion.heartbeat_period);
                    if (!rest || !beat_rest_drawn_) InvalidateRect(hwnd_, &ring, FALSE);
                    beat_rest_drawn_ = rest;
                }
            }
            return 0;
        case WM_ACTIVATE:
            PostMessageW(hwnd_, kMsgVisibility, 0, 0);  // on plan degisti: serit durur/surer (etkinlesme tamamlaninca)
            break;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMPOWERSTATUSCHANGE) update_strip_timer();  // pile gecince serit durur
            break;
        case WM_MOUSEMOVE: {
            if (!tracking_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                tracking_ = TrackMouseEvent(&tme) != FALSE;
            }
            const float x = static_cast<float>(GET_X_LPARAM(lp)) / scale();
            const float y = static_cast<float>(GET_Y_LPARAM(lp)) / scale();
            const Hit h = hits_.at(x, y);
            if (h != state_.hover) {
                screen_.set_hover(state_, h, now_ms());
                invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            tracking_ = false;
            screen_.set_hover(state_, Hit::None, now_ms());
            invalidate();
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, state_.hover != Hit::None ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_LBUTTONDOWN: {
            // Dugme anlami: etkinlestirme yalniz ayni ogede basilip birakilinca. Pencere acilirken veya
            // baska pencereden gelen eslesmemis WM_LBUTTONUP hicbir ogeyi tetiklemez.
            const float x = static_cast<float>(GET_X_LPARAM(lp)) / scale();
            const float y = static_cast<float>(GET_Y_LPARAM(lp)) / scale();
            pressed_ = hits_.at(x, y);
            if (pressed_ != Hit::None) SetCapture(hwnd_);
            screen_.set_pressed(state_, pressed_, now_ms());  // DS-02: basma geri bildirimi
            invalidate();
            return 0;
        }
        case WM_LBUTTONUP: {
            const float x = static_cast<float>(GET_X_LPARAM(lp)) / scale();
            const float y = static_cast<float>(GET_Y_LPARAM(lp)) / scale();
            const Hit h = hits_.at(x, y);
            const Hit down = pressed_;
            pressed_ = Hit::None;
            screen_.set_pressed(state_, Hit::None, now_ms());
            if (GetCapture() == hwnd_) ReleaseCapture();
            if (h != Hit::None && h == down) {
                set_focus(h, false);  // fare: odak halkasi gosterilmez
                activate(h);
            }
            invalidate();
            return 0;
        }
        case WM_CAPTURECHANGED:
            pressed_ = Hit::None;
            screen_.set_pressed(state_, Hit::None, now_ms());
            invalidate();
            return 0;
        case WM_CLOSE:
            // Kip penceresi acikken pencereyi kapatma dugmesi kip penceresini kapatir (Esc = Hayir); pencere kalir.
            if (state_.modal) {
                modal_key_input(ModalKey::Escape);
                if (opts_.smoke) PostMessageW(hwnd_, WM_CLOSE, 0, 0);  // duman testi: soru kapaninca pencere de kapanir
                return 0;
            }
            break;
        case WM_MOUSEWHEEL: {
            if (state_.modal) return 0;  // karartilmis sayfa kaydirilmaz
            UINT spi_lines = 3;  // okunamazsa Windows varsayilani
            if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &spi_lines, 0)) spi_lines = 3;
            state_.scroll += wheel_scroll_dip(GET_WHEEL_DELTA_WPARAM(wp), spi_lines);
            invalidate();
            return 0;
        }
        case WM_KEYDOWN:
            // Spec 2026-11-12 E: kip penceresi acikken klavye yalniz karta gider. Esc = Hayir; Enter / Bosluk yalniz
            // odaktaki ogeyi calistirir (tus tekrari calistirmaz: pencereyi acan basili tus yanit sayilmaz); Tab kartin
            // icinde doner; oklar secim listesinde / dugme satirinda gezer.
            // Kip penceresini kapatan tus basili tutuluyorsa tekrarlari ne sayfaya ne alttaki kip penceresine ulasir
            // (odak pencereyi acan ogeye dondu: tekrar ayni soruyu yeniden acardi).
            if (modal_key_guard_.swallow(static_cast<unsigned>(wp), (lp & 0x40000000) != 0)) return 0;
            if (state_.modal) {
                const bool repeat = (lp & 0x40000000) != 0;
                const ModalFrame* top = modals_.empty() ? nullptr : modals_.back();  // run_modal yigininda; bu islev boyunca yasar
                switch (wp) {
                    case VK_TAB: modal_key_input(GetKeyState(VK_SHIFT) < 0 ? ModalKey::ShiftTab : ModalKey::Tab); break;
                    case VK_ESCAPE: modal_key_input(ModalKey::Escape); break;
                    case VK_RETURN:
                        if (!repeat) modal_key_input(ModalKey::Enter);
                        break;
                    case VK_SPACE:
                        if (!repeat) modal_key_input(ModalKey::Space);
                        break;
                    case VK_UP: modal_key_input(ModalKey::Up); break;
                    case VK_DOWN: modal_key_input(ModalKey::Down); break;
                    case VK_LEFT: modal_key_input(ModalKey::Left); break;
                    case VK_RIGHT: modal_key_input(ModalKey::Right); break;
                    case VK_BACK:
                    case VK_DELETE:  // R-C4: yazi alani: Geri sil = son karakter, Delete = tumunu sil
                        if (!modals_.empty() && modals_.back()->st.focus.item == ModalItem::Input)
                            if (wp == VK_BACK ? modal_input_backspace(modals_.back()->st) : modal_input_clear(modals_.back()->st)) modal_sync();
                        break;
                    case 'V':
                    case VK_INSERT:  // Ctrl+V / Shift+Ekle: yapistir (yalniz izinli karakterler)
                        if (!modals_.empty() && modals_.back()->st.focus.item == ModalItem::Input &&
                            ((wp == 'V' && GetKeyState(VK_CONTROL) < 0) || (wp == VK_INSERT && GetKeyState(VK_SHIFT) < 0)))
                            if (modal_input_paste(modals_.back()->st, clipboard_text(hwnd_))) modal_sync();
                        break;
                    default: break;
                }
                if (top && top->st.done) modal_key_guard_.closed_by(static_cast<unsigned>(wp));
                return 0;
            }
            switch (wp) {
                case VK_TAB: move_focus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1); return 0;
                case VK_DOWN:
                case VK_UP: {
                    if (arrow_games(0, wp == VK_DOWN ? 1 : -1)) return 0;  // oyun kartlari: bir satir yukari / asagi
                    const int i = static_cast<int>(state_.focus);
                    if (i >= 0 && i < static_cast<int>(kPageCount)) {
                        move_focus(wp == VK_DOWN ? 1 : -1);
                        if (static_cast<int>(state_.focus) >= static_cast<int>(kPageCount)) move_focus(-1);
                    } else {
                        state_.scroll += wp == VK_DOWN ? 48.f : -48.f;
                        invalidate();
                    }
                    return 0;
                }
                case VK_LEFT:
                case VK_RIGHT:
                    if (arrow_games(wp == VK_RIGHT ? 1 : -1, 0)) return 0;  // oyun kartlari: komsu kart
                    if (arrow_tabs(wp == VK_RIGHT ? 1 : -1)) return 0;
                    break;
                case VK_NEXT: state_.scroll += 400.f; invalidate(); return 0;
                case VK_PRIOR: state_.scroll -= 400.f; invalidate(); return 0;
                case VK_ESCAPE:
                    // Canli test gorunumu: Esc = "Testi durdur" (ayni iptal yolu). Yalniz gorunum ekrandayken.
                    if (live_view_visible()) {
                        if (core::measure::can_stop(vm_.measure) && on_test_stop) on_test_stop();
                        return 0;
                    }
                    // Onay acikken Esc = Hayir (hicbir sey degismez)
                    if (vm_.perf.phase == core::perf::Phase::Confirming || vm_.perf.phase == core::perf::Phase::ConfirmRestore) {
                        if (on_answer) on_answer(false);
                    }
                    return 0;
                case VK_RETURN:
                case VK_SPACE:
                    if (!page_activation_accepted((lp & 0x40000000) != 0)) return 0;  // basili tus tekrari calistirmaz
                    if (state_.focus != Hit::None) {
                        set_focus(state_.focus, true);
                        activate(state_.focus);
                    }
                    return 0;
                default: break;
            }
            break;
        case WM_CHAR:
            if (state_.modal) {  // R-C4: yazi alani odaktaysa karakter girilir; aksi halde yutulur
                if (!modals_.empty() && modals_.back()->st.focus.item == ModalItem::Input &&
                    modal_input_char(modals_.back()->st, static_cast<wchar_t>(wp)))
                    modal_sync();
                return 0;
            }
            break;
        case kMsgModel: {
            std::unique_ptr<ViewModel> vm(reinterpret_cast<ViewModel*>(lp));
            if (vm) {
                const PerfView perf = vm_.perf;  // anahtar durumu denetleyiciden gelir, envanterden degil
                vm_ = std::move(*vm);
                vm_.perf = perf;
                invalidate();
            }
            return 0;
        }
        case kMsgFindings: {
            std::unique_ptr<std::vector<FindingVM>> f(reinterpret_cast<std::vector<FindingVM>*>(lp));
            if (f) {
                vm_.findings = std::move(*f);
                vm_.findings_loading = false;
                invalidate();
            }
            return 0;
        }
        case kMsgVisibility:
            update_visibility();
            return 0;
        case kMsgRun: {
            std::unique_ptr<std::function<void()>> fn(reinterpret_cast<std::function<void()>*>(lp));
            if (fn && *fn) (*fn)();
            return 0;
        }
        case WM_DESTROY:
            abandon_modals();  // bekleyen kip pencereleri Hayir ile kapanir (islem baslatilmaz)
            if (uia_) uia_->disconnect();
            KillTimer(hwnd_, kAnimTimer);
            KillTimer(hwnd_, kTickTimer);
            KillTimer(hwnd_, kStripTimer);
            KillTimer(hwnd_, kBreatheTimer);
            KillTimer(hwnd_, kPulseTimer);
            tick_timer_ = false;
            strip_timer_ = false;
            breathe_timer_ = false;
            pulse_timer_ = false;
            state_.strip_live = false;
            state_.breathe_live = false;
            state_.pulse_live = false;
            gfx_->release_target();
            if (opts_.quit_on_destroy) PostQuitMessage(0);
            hwnd_ = nullptr;
            activity_.set(Visibility::Closed);
            if (on_closed) on_closed();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace ef::ui
