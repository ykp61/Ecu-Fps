// Denetim 5.1: Windows "Metin boyutu" temaya uygulanir; oran %100-%125 ile sinirli (yerlesimin dogrulandigi aralik).
#include <cmath>

#include "ef_test.h"
#include "ui/theme/theme.h"

using namespace ef::ui;

EF_TEST(text_scale_clamps_and_scales_text_and_controls) {
    EF_CHECK(clamp_text_scale(0.5f) == 1.f);
    EF_CHECK(clamp_text_scale(std::nanf("")) == 1.f);
    EF_CHECK(clamp_text_scale(1.2f) == 1.2f);
    EF_CHECK(clamp_text_scale(2.25f) == kMaxTextScale);
    const Theme& base = default_theme();
    const Theme same = with_text_scale(base, 1.f);
    EF_CHECK(same.role(Role::Body).size == base.role(Role::Body).size && same.button_h == base.button_h);
    const Theme big = with_text_scale(base, 2.f);  // %125'e kirpilir
    EF_CHECK(big.role(Role::Body).size == base.role(Role::Body).size * kMaxTextScale);
    // P9-B6: satir TAM DIP'e (asagi) yuvarlanir (18 x 1,25 = 22,5 -> 22); yerlesim y'leri tam sayi kalir ve dugme etiketi
    // kutuda 1 DIP kaymaz. Satir hicbir rolde tasarim satirinin altina inmez, olcekli satiri da asmaz.
    EF_CHECK(big.role(Role::Body).line == std::floor(base.role(Role::Body).line * kMaxTextScale));
    for (std::size_t i = 0; i < big.type.size(); ++i) {
        const float l = big.type[i].line;
        EF_CHECK(l == std::floor(l));
        EF_CHECK(l >= base.type[i].line && l <= base.type[i].line * kMaxTextScale);
    }
    const float d = 2.f * std::ceil((big.role(Role::Body).line - base.role(Role::Body).line) / 2.f);  // ic bosluk korunur, cift
    EF_CHECK(d > 0 && big.button_h == base.button_h + d && big.row2_h == base.row2_h + 2 * d);
    EF_CHECK(big.row_h > base.row_h && big.control_max_h > base.control_max_h);
    // Bosluk, pencere asgarisi ve dokunma hedefi degismez.
    EF_CHECK(big.page_margin == base.page_margin && big.min_window_w == base.min_window_w && big.touch_min == base.touch_min);
}
