#pragma once
// Uyumluluk B6 (bosluk 18, 22): pencere boyut/yerlesim hesaplari (saf; Win32 cagrisi yok, birim test edilir).
// Calisma alani (rcWork) gorev cubugunu disarida birakir: 1366x768 veya 1080p %150-%200 olcekte ana pencerenin alt
// kismi gorev cubugunun altina dusmesin ve en kucuk izleme boyutu ekrana sigsin.

namespace ef::ui {

struct PxRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    constexpr int width() const noexcept { return right - left; }
    constexpr int height() const noexcept { return bottom - top; }
    constexpr bool operator==(const PxRect&) const = default;
};

struct PxSize {
    int w = 0, h = 0;
    constexpr bool operator==(const PxSize&) const = default;
};

// Istenen dis boyut calisma alanina sigacak sekilde kirpilir ve alanin ortasina konur. Bos/gecersiz calisma
// alaninda istenen boyut sol-ust koseden aynen dondurulur (tahmin yok).
constexpr PxRect fit_centered(PxRect work, int want_w, int want_h) noexcept {
    if (want_w < 1) want_w = 1;
    if (want_h < 1) want_h = 1;
    if (work.width() <= 0 || work.height() <= 0) return {work.left, work.top, work.left + want_w, work.top + want_h};
    const int w = want_w < work.width() ? want_w : work.width();
    const int h = want_h < work.height() ? want_h : work.height();
    const int x = work.left + (work.width() - w) / 2;
    const int y = work.top + (work.height() - h) / 2;
    return {x, y, x + w, y + h};
}

// En kucuk izleme boyutu calisma alanini asamaz (aksi halde pencere ekrana sigacak kadar kucultulemez).
constexpr PxSize cap_min_track(PxSize min_track, PxRect work) noexcept {
    if (work.width() > 0 && min_track.w > work.width()) min_track.w = work.width();
    if (work.height() > 0 && min_track.h > work.height()) min_track.h = work.height();
    return min_track;
}

// Canli panel: verilen calisma alaninin sol-ust kosesinden `margin` iceride.
constexpr PxRect overlay_rect(PxRect work, int w, int h, int margin) noexcept {
    return {work.left + margin, work.top + margin, work.left + margin + w, work.top + margin + h};
}

// Uyumluluk B6 (bosluk 27): koyu baslik cubugu. DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 20H1+ / 11); daha
// eski Windows 10 (1809-1909) ayni ozelligi belgesiz 19 numarasiyla kabul eder. `set(attr)` HRESULT benzeri deger
// dondurur (>= 0 basari). 20 reddedilirse 19 denenir; basarili ozellik numarasi (yoksa 0) doner.
// DWMWA_CAPTION_COLOR (35) yalniz Windows 11'dedir; Windows 10'da reddedilmesi beklenir ve koyu baslik kabul edilir.
inline constexpr unsigned kDwmDarkModeAttr = 20, kDwmDarkModeAttrLegacy = 19;
template <class SetAttr>
unsigned apply_dark_caption(SetAttr&& set) {
    if (set(kDwmDarkModeAttr) >= 0) return kDwmDarkModeAttr;
    if (set(kDwmDarkModeAttrLegacy) >= 0) return kDwmDarkModeAttrLegacy;
    return 0;
}

}  // namespace ef::ui
