#pragma once
// BRAND-01: marka varliklari (2.0.0 "Boost Line": italik EF ligaturu, hiz cizgisi + ok ucu). SVG'ler derleme
// zamaninda gomulur (assets/branding/ecu-fps), ilk kullanimda bir kez ayristirilir; ICO'lar EXE
// kaynaklarindadir (brand_resources.h).

#include <windows.h>

#include "ui/brand/brand_resources.h"
#include "ui/brand/svg_path.h"
#include "ui/theme/theme.h"

namespace ef::ui::brand {

// LockupDark: renkli yatay logo (yesil amblem, camgobegi ok ucu, acik yazi) koyu zemin icin.
// LockupLight: acik zemin renkleri. LockupWhite: tek renk (cizimde mono renk verilir).
// MarkColor: renkli EF amblemi (256 birim tuval). MarkCurrent: ayni amblem, tum yollar currentColor.
// MarkOptical16/20/24: boyuta ozel piksel izgarali tam amblem (ok ucu dahil; tepsi/kucuk ikon icin, currentColor).
enum class Logo { LockupDark, LockupLight, LockupWhite, MarkColor, MarkCurrent, MarkOptical16, MarkOptical20, MarkOptical24 };
inline constexpr Logo kAllLogos[] = {Logo::LockupDark,  Logo::LockupLight,   Logo::LockupWhite,   Logo::MarkColor,
                                     Logo::MarkCurrent, Logo::MarkOptical16, Logo::MarkOptical20, Logo::MarkOptical24};
// Gomulu ve ayristirilmis SVG (gecersizse bos belge; birim testi gomulu hepsinin gecerli oldugunu dogrular).
const SvgDoc& logo(Logo l);
std::string_view embedded_svg(Logo l) noexcept;
// Piksel boyutuna gore amblem cizimi: <=16 -> 16 px optik, <=20 -> 20, <=24 -> 24, ustu ana amblem (currentColor).
Logo mark_for_px(int px) noexcept;

// Marka tokenlari (brand-tokens.json): yatay logonun en kucuk genisligi.
inline constexpr float kLockupMinWidthDip = 160.f;

// Uygulama ikonu (pencere / Alt+Tab): istenen piksel boyutunda EXE kaynagindan. Kaynak yoksa nullptr.
HICON load_app_icon(HINSTANCE inst, int px);
// Tepsi ikonu kaynaktan (IDI_TRAY_*), piksel boyutunda.
HICON load_tray_icon(HINSTANCE inst, int resource_id, int px);
// Yuksek kontrast: amblem yollarini verilen renkle seffaf zemine cizip HICON uretir (sahiplik cagirana).
HICON render_mark_icon(int px, const Color& fg);

}  // namespace ef::ui::brand
