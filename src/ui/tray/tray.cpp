#include "ui/tray/tray.h"

#include <shellapi.h>
#include <windowsx.h>

#include <cwchar>

namespace ef::ui {

namespace {
constexpr UINT kIconId = 1;

NOTIFYICONDATAW base(HWND owner) {
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = owner;
    n.uID = kIconId;
    return n;
}
}  // namespace

bool Tray::add(HWND owner, const std::wstring& tip) {
    owner_ = owner;
    tip_ = tip;
    auto n = base(owner);
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    n.uCallbackMessage = kCallbackMsg;
    n.hIcon = icon_ ? icon_ : LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(n.szTip, tip.c_str(), _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_ADD, &n)) return false;
    n.uVersion = NOTIFYICON_VERSION_4;  // klavye olaylari ve WM_CONTEXTMENU
    Shell_NotifyIconW(NIM_SETVERSION, &n);
    added_ = true;
    return true;
}

void Tray::readd() {
    if (!owner_) return;
    added_ = false;
    if (add(owner_, tip_)) return;
    // Simge hala kayitli (Explorer aslinda yeniden baslamadi): ikinci kopya eklenmez, mevcut guncellenir.
    auto n = base(owner_);
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    n.uCallbackMessage = kCallbackMsg;
    n.hIcon = icon_ ? icon_ : LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(n.szTip, tip_.c_str(), _TRUNCATE);
    if (Shell_NotifyIconW(NIM_MODIFY, &n)) {
        n.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &n);
        added_ = true;
    }
}

void Tray::set_tip(const std::wstring& tip) {
    tip_ = tip;
    if (!added_) return;
    auto n = base(owner_);
    n.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcsncpy_s(n.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void Tray::set_icon(HICON icon) {
    icon_ = icon;
    if (!added_ || !icon) return;
    auto n = base(owner_);
    n.uFlags = NIF_ICON;
    n.hIcon = icon;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void Tray::notify(const std::wstring& title, const std::wstring& text) {
    if (!added_) return;
    auto n = base(owner_);
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcsncpy_s(n.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void Tray::remove() {
    if (!added_) return;
    auto n = base(owner_);
    Shell_NotifyIconW(NIM_DELETE, &n);
    added_ = false;
}

UINT Tray::show_menu(int x, int y, const TrayMenu& m) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return 0;
    AppendMenuW(menu, MF_STRING, CmdShow, m.show_text.c_str());
    AppendMenuW(menu, MF_STRING | (m.toggle_enabled ? 0u : MF_GRAYED), CmdToggle, m.toggle_text.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CmdExitRestore, m.exit_text.c_str());
    SetMenuDefaultItem(menu, CmdShow, FALSE);
    // Menu disina tiklaninca kapanmasi icin sahip on plana alinir (belgelenmis tepsi deseni).
    SetForegroundWindow(owner_);
    const UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
    const UINT cmd = static_cast<UINT>(TrackPopupMenuEx(menu, flags, x, y, owner_, nullptr));
    PostMessageW(owner_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return cmd;
}

UINT Tray::handle(WPARAM wp, LPARAM lp, const TrayMenu& menu) {
    // Surum 4: LOWORD(lParam) olay, HIWORD(lParam) simge kimligi, wParam bagli koordinat.
    switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            return CmdShow;
        case WM_CONTEXTMENU:
            return show_menu(GET_X_LPARAM(wp), GET_Y_LPARAM(wp), menu);
        default:
            return 0;
    }
}

}  // namespace ef::ui
