#pragma once
// UI-04: ozel cizilen arayuz icin UI Automation saglayicisi. Pencerenin son isabet haritasindan
// (ad, rol, secim/anahtar durumu, sinir kutusu) ogeleri uretir. Ekran okuyucular (Ayrac/Narrator,
// NVDA) ve otomasyon araclari ogeleri gorur, odak degisimini duyar, dugmeleri calistirir.
// Tum cagrilar UI is parcaciginda gelir (HWND tabanli sunucu saglayici).

#include <windows.h>
#include <ole2.h>  // WIN32_LEAN_AND_MEAN OLE tiplerini dislar
#include <uiautomation.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "ui/shell/screen.h"

namespace ef::ui {

// P9-B6: UIA'ya giden metinler (Name, HelpText, pencere adi) bas / son bosluk tasimaz. Kaynak metin (ornek: WMI disk modeli
// "MLD M500 NVMe SSD " sonundaki dolgu bosluklari) gorunen yazida zararsizdir ama ekran okuyucuya ve otomasyon araclarina
// kirli ad olarak gider; temizlik tek yerde, saglayicida yapilir. Bosluk: bosluk, sekme, CR/LF, NBSP, dar NBSP, ZWSP.
std::wstring uia_clean_text(std::wstring_view s);

class UiaModel {
public:
    HWND hwnd = nullptr;
    float scale = 1.f;
    HitMap hits;                    // son cizimden
    Hit focus = Hit::None;
    std::wstring window_name;
    std::function<void(Hit)> activate;  // Invoke/Toggle/Select -> pencerenin etkinlestirmesi
    // Value.SetValue (Edit): pencere metni dogrular ve uygular; reddedilirse false (UIA E_INVALIDARG).
    std::function<bool(Hit, const std::wstring&)> set_value;
    bool alive() const { return hwnd != nullptr; }
};

// WM_GETOBJECT icin kok saglayici (pencere omru boyunca tek nesne).
class UiaHost {
public:
    explicit UiaHost(std::shared_ptr<UiaModel> model);
    ~UiaHost();
    LRESULT on_get_object(WPARAM wp, LPARAM lp);
    // Odak degistiginde ekran okuyucuya bildir.
    void notify_focus(Hit h);
    // TH-04: secim degistiginde oge icin SelectionItem_ElementSelected, kok/konteyner icin Selection_Invalidated.
    void notify_selection(Hit h);
    // Spec 2026-11-12 E: uygulama ici kip penceresi acildi (UIA Window_WindowOpened; oge = Hit::Modal).
    void notify_window_opened(Hit h);
    // Kip penceresi acildi / kapandi: kokun cocuklari degisti (UIA istemcileri agaci yeniden okur).
    void notify_structure_changed();
    // Ekran okuyucuya kisa duyuru (UiaRaiseNotificationEvent, ImportantMostRecent: yeni duyuru eskisini keser).
    // completed=true -> NotificationKind_ActionCompleted (islem bitti), false -> NotificationKind_Other (durum/ilerleme).
    // activity_id ayni turdeki duyurulari gruplar (bos olabilir). Dinleyen yoksa veya API yoksa (Windows 10 1709
    // oncesi) false doner; arayuz metni zaten ekranda oldugu icin sessizce atlanir.
    bool announce(const std::wstring& text, bool completed, const std::wstring& activity_id = L"ecufps.status");
    // Pencere kapanirken: tum ogeler "artik yok" doner.
    void disconnect();

private:
    std::shared_ptr<UiaModel> model_;
    IRawElementProviderSimple* root_ = nullptr;
};

}  // namespace ef::ui
