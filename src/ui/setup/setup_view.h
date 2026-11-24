#pragma once
// ADR-033 / spec 3: kurulum penceresinin cizimi (480x560 DIP, koyu tema, ustte RGB serit + marka lockup'i). Ayni Screen
// bilesenleri (dugme, anahtar, kart, cip, odak halkasi, Evet/Hayir kip penceresi) kullanilir; durum yalniz
// core::setup::SetupVm'den okunur. Pencere ve Win32 mesajlari app/setup_app.cpp'dedir; ayni kod ekran disi denetimde
// (ef_ui_snapshot) de cizer.

#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "core/setup/setup_flow.h"
#include "ui/render/graphics.h"
#include "ui/shell/modal.h"
#include "ui/shell/screen.h"
#include "ui/theme/theme.h"

namespace ef::ui {

class SetupView {
public:
    SetupView(const Theme& theme, const core::i18n::Catalog& catalog) : theme_(theme), screen_(theme, catalog), tr_(catalog.locale() == core::i18n::Locale::TrTR) {}

    // Tum pencereyi cizer; isabet haritasini dondurur. install_dir: gercek kurulum yolu (bos = okunamadi). Confirm
    // evresinde s.modal (cagiranin tuttugu Evet/Hayir durumu) sayfanin ustune cizilir.
    HitMap paint(Graphics& g, float w, float h, UiState& s, const core::setup::SetupVm& vm, const std::wstring& install_dir, std::int64_t now);

    // Kur -> uygulama ici Evet/Hayir (spec 3): baslik, govde, "Evet, kur" / "Hayir".
    ModalSpec confirm_spec() const;
    std::wstring window_title() const;
    // Duyuru (ekran okuyucu): evre degisince okunacak kisa metin (baslik + durum satiri).
    std::wstring announcement(const core::setup::SetupVm& vm, const std::wstring& install_dir) const;

    Screen& screen() { return screen_; }
    const Theme& theme() const { return theme_; }

private:
    struct Line {
        std::wstring text;
        Color color;
    };
    std::wstring title_text(const core::setup::SetupVm& vm) const;
    Line status_line(const core::setup::SetupVm& vm, const std::wstring& install_dir) const;
    std::vector<Line> notes(const core::setup::SetupVm& vm) const;
    std::wstring row_label(core::setup::SetupRow r) const;
    std::wstring version_w(const std::string& v) const;

    float rows_card(Graphics& g, float x, float y, float w, UiState& s, const core::setup::SetupVm& vm, const std::wstring& install_dir,
                    std::int64_t now, HitMap& hits, StatusSeq& seq);
    float steps_card(Graphics& g, float x, float y, float w, const core::setup::SetupVm& vm, HitMap& hits, StatusSeq& seq);
    float download_card(Graphics& g, float x, float y, float w, const core::setup::SetupVm& vm, HitMap& hits, StatusSeq& seq);
    void license_panel(Graphics& g, const RectF& r, UiState& s, HitMap& hits, StatusSeq& seq);
    void footer(Graphics& g, float x, float y, float w, UiState& s, const core::setup::SetupVm& vm, std::int64_t now, HitMap& hits, StatusSeq& seq);

    const Theme& theme_;
    Screen screen_;
    bool tr_ = false;  // yuzde bicimi: TR "%42", EN "42%" (panel_text percent_text)
};

// Tab sirasi (niyet): kip penceresi acikken yalniz kip ogeleri; lisans paneli acikken yalniz "Kapat"; aksi halde
// satir kontrolleri (Windows ile baslat, Lisans ve gizlilik "Goster") ve gorunen sirayla alt dugmeler.
std::vector<Hit> setup_focus_order(const core::setup::SetupVm& vm, const UiState& s);
// Gercek sira: niyet sirasinin son cizimde kayitli ve etkinlestirilebilir ogeleri.
std::vector<Hit> setup_focus_order(const core::setup::SetupVm& vm, const UiState& s, const HitMap& hits);

}  // namespace ef::ui
