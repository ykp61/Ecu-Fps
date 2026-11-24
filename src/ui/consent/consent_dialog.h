#pragma once
// UI-06: oyun basina tek seferlik onay. Spec 2026-11-12 E: Windows TaskDialog yerine uygulamanin kendi cizdigi kip
// penceresi (MainWindow::run_modal): klavye, ekran okuyucu (UIA Window + IsModal), yuksek kontrast ve DPI uygulama
// tarafindan saglanir. Dugmeler acikca "Evet" / "Hayir"; ilk odak Hayir.

#include <windows.h>

#include <optional>
#include <string>

#include "core/i18n/catalog.h"
#include "ui/shell/modal.h"

namespace ef::ui {

// true = Evet, false = Hayir, nullopt = kullanici cevap vermeden kapatti (Esc / kapat) ya da soru gosterilemedi (sahip
// HWND ana pencere degil: ana pencere yok / tepside). nullopt'ta karar kaydedilmez.
std::optional<bool> ask_game_consent(HWND owner, const core::i18n::Catalog& cat, const std::wstring& game_name);

// Sorunun kip penceresi tanimi (saf; cizim denetimi ve birim testleri ayni tanimi kullanir).
ModalSpec game_consent_modal_spec(const core::i18n::Catalog& cat, const std::wstring& game_name);

}  // namespace ef::ui
