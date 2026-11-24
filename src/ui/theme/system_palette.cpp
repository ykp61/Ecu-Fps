#include "ui/theme/system_palette.h"

#include <windows.h>

namespace ef::ui {

namespace {
Color sys(int idx) {
    const COLORREF c = GetSysColor(idx);
    return {GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, 1.f};
}
}  // namespace

bool high_contrast_on() {
    HIGHCONTRASTW hc{sizeof(hc)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

bool taskbar_uses_light_theme() {
    DWORD v = 0, size = sizeof(v);
    const LSTATUS r = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                   L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size);
    return r == ERROR_SUCCESS && v != 0;
}

Palette high_contrast_palette_from(const SystemColorTable& c) {
    Palette p;
    p.background = c.window;
    p.sidebar = c.window;
    p.surface = c.window;
    p.surface_raised = c.button_face;
    p.border = c.window_text;
    p.text_primary = c.window_text;
    p.text_secondary = c.gray_text;  // DS-11: HC'de ikincil metin COLOR_GRAYTEXT (semaya gore ayri ton)
    p.accent = c.highlight;
    p.on_accent = c.highlight_text;
    // TH-02: odak halkasi secim/vurgu renginden farkli olmali; sistem metin rengi (WINDOWTEXT) vurguyla
    // karisabilen kullanici semalarinda da secim cercevesinden ayrilir ve pencere zeminine karsi yuksektir.
    p.focus_ring = c.window_text;
    p.violet = c.hotlight;
    // Durum renkleri: anlam metin, ikon ve UIA adiyla da verilir; HC semasinda renk secenegi sinirlidir
    // (uyari ve hata ayni HOTLIGHT'a duser, bkz. TH-06 - bu yuzden renk tek basina anlam tasimaz).
    p.success = c.window_text;
    p.warning = c.hotlight;
    p.error = c.hotlight;
    // Zehir yesili yuvalari: rampa turuncusu HOTLIGHT, yukleme/serit rengi HIGHLIGHT, hero karti pencere zemini.
    // HC'de rampa/serit/parlama renk yerine kelime ve tek renk cizgiyle anlatilir.
    p.orange = c.hotlight;
    p.cyan = c.highlight;
    p.hero = c.window;
    // Veri rolleri (spec 2026-11-12 F): HC'de seri rengi yok; sayilar sistem metin rengiyle, notr bilgi GRAYTEXT ile.
    p.data_cpu = p.data_gpu = p.data_net_down = p.data_net_up = c.window_text;
    p.data_info = c.gray_text;
    return p;
}

std::optional<Palette> system_high_contrast_palette() {
    if (!high_contrast_on()) return std::nullopt;
    return high_contrast_palette_from(SystemColorTable{sys(COLOR_WINDOW), sys(COLOR_BTNFACE), sys(COLOR_WINDOWTEXT),
                                                       sys(COLOR_HIGHLIGHT), sys(COLOR_HIGHLIGHTTEXT), sys(COLOR_HOTLIGHT),
                                                       sys(COLOR_GRAYTEXT)});
}

}  // namespace ef::ui
