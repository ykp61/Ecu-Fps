#include "ui/consent/consent_dialog.h"

#include "ui/shell/main_window.h"

namespace ef::ui {

namespace {
std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
}  // namespace

ModalSpec game_consent_modal_spec(const core::i18n::Catalog& cat, const std::wstring& game) {
    using core::i18n::StringId;
    ModalSpec spec;
    std::wstring q = widen(cat.get(StringId::ConsentQuestion));
    if (const auto pos = q.find(L"{game}"); pos != std::wstring::npos) q.replace(pos, 6, game);
    // Eski pencere basligi ("Ecu Fps — Oyun algilandi") ekranda yoktur; kip penceresinin UIA HelpText'inde okunur.
    spec.caption = widen(cat.get(StringId::AppTitle)) + L" — " + widen(cat.get(StringId::ConsentTitle));
    spec.title = std::move(q);
    spec.body.push_back(widen(cat.get(StringId::ConsentDetail)));
    spec.yes = widen(cat.get(StringId::ActionConfirm));
    spec.no = widen(cat.get(StringId::ActionCancel));
    return spec;
}

std::optional<bool> ask_game_consent(HWND owner, const core::i18n::Catalog& cat, const std::wstring& game) {
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return std::nullopt;  // ana pencere yok (tepside): soru sorulmaz, karar kaydedilmez
    const ModalResult r = w->run_modal(game_consent_modal_spec(cat, game));
    if (r.answer == ModalAnswer::Yes) return true;
    if (!r.dismissed) return false;  // Hayir dugmesi
    return std::nullopt;             // Esc / kapat / gosterilemedi: karar yok, sonraki algilamada yeniden sorulur
}

}  // namespace ef::ui
