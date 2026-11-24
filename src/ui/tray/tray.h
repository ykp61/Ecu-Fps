#pragma once
// UI-06 (ADR-013): gorunur tepsi simgesi ve menusu. Shell_NotifyIcon surum 4: fare ve klavye
// (Win+B, ok tuslari, Enter/Shift+F10) ayni olaylari uretir; menu yerel acilir menu oldugu icin
// klavye ve ekran okuyucuyla kullanilabilir. Simge varligi henuz tasarlanmadi (docs/07): sistem
// uygulama simgesi kullanilir.

#include <windows.h>

#include <functional>
#include <string>

namespace ef::ui {

struct TrayMenu {
    std::wstring toggle_text;  // "Performansi ac..." / "Performansi kapat..."
    bool toggle_enabled = true;
    std::wstring show_text;
    std::wstring exit_text;
};

class Tray {
public:
    static constexpr UINT kCallbackMsg = WM_APP + 20;
    enum Command : UINT { CmdToggle = 1, CmdShow = 2, CmdExitRestore = 3 };

    bool add(HWND owner, const std::wstring& tip);
    void set_tip(const std::wstring& tip);
    // BRAND-01: ikon (sahiplik cagiranda; tepsi yalniz kullanir). Yeniden eklemede ayni ikon kullanilir.
    void set_icon(HICON icon);
    void notify(const std::wstring& title, const std::wstring& text);  // bilgi balonu (sessiz)
    void remove();
    // Explorer yeniden basladiginda (TaskbarCreated) simge yeniden eklenir.
    UINT taskbar_created_msg() const { return taskbar_created_; }
    void readd();

    // Sahip pencere mesaji: tepsi olayi ise islenir; secilen komut dondurulur (0 = yok).
    // primary: sol tik / Enter (pencereyi goster); menu: sag tik / Shift+F10 / uygulama tusu.
    UINT handle(WPARAM wp, LPARAM lp, const TrayMenu& menu);
    bool added() const { return added_; }

private:
    UINT show_menu(int x, int y, const TrayMenu& menu);
    HWND owner_ = nullptr;
    bool added_ = false;
    std::wstring tip_;
    HICON icon_ = nullptr;
    UINT taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
};

}  // namespace ef::ui
