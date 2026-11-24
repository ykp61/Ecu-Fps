// UI-06 + spec 2026-11-12 E: oyun onayini uygulama ici kip penceresiyle sorar ve sonucu yazar.
//   nowindow=none            ana pencere yokken soru sorulmaz (karar kaydedilmez)
//   consent=yes|no|none      ana pencere gorunurken sorulan sorunun yaniti (none = Esc / kapat)
// check_consent.ps1 kip penceresini ana pencerenin UI Automation agacinda bulur (AutomationId "modal", Window +
// IsModal) ve dugmeye UIA Invoke ile basar (ekran okuyucu yolu). Pencere etkinlestirilmez ve en alta konur.
#include <windows.h>

#include <cstdio>

#include "core/i18n/catalog.h"
#include "ui/consent/consent_dialog.h"
#include "ui/shell/main_window.h"
#include "ui/theme/theme.h"

namespace {
HWND g_window = nullptr;
// Emniyet: yanit gelmezse 60 sn sonra pencere kapatilir (kip penceresi acikken kapatma = Hayir / karar yok).
void CALLBACK give_up(HWND, UINT, UINT_PTR, DWORD) {
    if (g_window) PostMessageW(g_window, WM_CLOSE, 0, 0);
}
const char* text(const std::optional<bool>& r) { return !r ? "none" : (*r ? "yes" : "no"); }
}  // namespace

int wmain() {
    using namespace ef::ui;
    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    if (!cat.ok()) return 2;
    // Ana pencere yok: soru sorulmaz, hicbir pencere acilmaz.
    std::printf("nowindow=%s\n", text(ask_game_consent(nullptr, cat.value(), L"Test Oyunu")));
    std::fflush(stdout);

    MainWindow::Options o;
    o.window_class = L"EcuFps.ConsentProbe";
    o.title = L"Ecu Fps consent probe";
    o.quit_on_destroy = false;
    auto w = MainWindow::create(GetModuleHandleW(nullptr), default_theme(), cat.value(), o);
    if (!w) return 2;
    g_window = w->hwnd();
    ShowWindow(w->hwnd(), SW_SHOWNOACTIVATE);
    SetWindowPos(w->hwnd(), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);  // kullanicinin penceresinin onune cikmaz
    UpdateWindow(w->hwnd());
    const UINT_PTR timer = SetTimer(nullptr, 0, 60000, give_up);
    const auto r = ask_game_consent(w->hwnd(), cat.value(), L"Test Oyunu");
    KillTimer(nullptr, timer);
    std::printf("consent=%s\n", text(r));
    std::fflush(stdout);
    g_window = nullptr;
    return 0;
}
