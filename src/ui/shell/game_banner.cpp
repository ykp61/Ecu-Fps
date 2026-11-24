// EXT-AC-02: oyun calisirken veya oyun durumu dogrulanamadiginda ust bilgi karti. Ayar degisikligi, disk temizligi
// ve agir test dugmeleri bu sirada gosterilmez; GERCEK neden burada yazilir (ilk tarama, kitaplik hatasi, surec
// kimligi belirsizligi, bayat veri ayri). Yeniden taranabilen nedenlerde dugme gercek taramayi tetikler.
#include <windows.h>

#include <algorithm>
#include <string>

#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;

namespace {
std::wstring to_wide_local(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), wchar_t{});
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::wstring replace_count(std::wstring s, std::size_t count) {
    const std::wstring_view k = L"{count}";
    if (const auto p = s.find(k); p != std::wstring::npos) s.replace(p, k.size(), std::to_wstring(count));
    return s;
}
}  // namespace

void Screen::game_banner(Graphics& g, Stack& st, const RectF& body, const ViewModel& vm, UiState& s, std::int64_t now, HitMap& hits) {
    if (!vm.game_gate_blocked) return;
    const auto& p = pal();
    const float pad = theme_.card_pad;
    const float inner_w = body.w - 2 * pad;
    auto head = tk(vm.game_block_title_key);
    if (head.empty()) head = t(StringId::GateDetectionFailedTitle);
    // Neden daima yazilir; renk/ikon tek basina anlam tasimaz. Katalogda metin yoksa genel nedene duser.
    auto msg = tk(vm.game_block_reason_key);
    if (msg.empty()) msg = t(StringId::GateDetectionFailed);
    std::wstring detail;
    const std::size_t shown = std::min<std::size_t>(vm.library_issues.size(), 3);
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& is = vm.library_issues[i];
        if (!detail.empty()) detail += L"\n";
        detail += tk(is.key);
        if (!is.path.empty()) detail += L" \u2014 " + to_wide_local(is.path);
    }
    if (vm.library_issues.size() > shown) {
        detail += L"\n";
        detail += replace_count(t(StringId::GateLibraryMore), vm.library_issues.size() - shown);
    }
    const float hh = g.measure(Role::Section, head, inner_w).h;
    const float mh = g.measure(Role::Secondary, msg, inner_w).h;
    const float dh = detail.empty() ? 0.f : g.measure(Role::Secondary, detail, inner_w).h + theme_.space(1);
    const bool retry = vm.game_block_can_retry;
    const float bh = retry ? theme_.space(2) + button_rect(g, 0, 0, t(StringId::GateRetry)).h : 0.f;
    const RectF card = st.next(pad + hh + theme_.space(1) + mh + dh + bh + pad);
    g.fill_round(card, theme_.radius_card, p.surface_raised);
    g.stroke_round(card, theme_.radius_card, p.warning.with_alpha(alpha_scale::kOutline));
    float y = card.y + pad;
    g.text(Role::Section, head, {card.x + pad, y, inner_w, hh}, p.text_primary);
    y += hh + theme_.space(1);
    g.text(Role::Secondary, msg, {card.x + pad, y, inner_w, mh}, p.text_secondary);
    y += mh;
    if (!detail.empty()) {
        y += theme_.space(1);
        g.text(Role::Secondary, detail, {card.x + pad, y, inner_w, dh}, p.text_secondary);
        y += dh - theme_.space(1);
    }
    if (retry) {
        y += theme_.space(2);
        button(g, card.x + pad, y, Hit::DetectionRetry, t(vm.detection_retrying ? StringId::GateRetrying : StringId::GateRetry), false, s, now, hits);
    }
}

}  // namespace ef::ui
