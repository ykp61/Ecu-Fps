#pragma once
// P9/B5: Olcumler sayfasi — gorunum / model ayrimi. Screen::measure_page yerlesimi kurar; kartlar measure_*_card islevleridir
// (Screen uyeleri, ayri .cpp dosyalarinda) ve sayfa genelindeki olculeri MeasureCtx uzerinden okur. Metin / turetim
// yardimcilari (model) measure_page_model.cpp'dedir. Gorunum degismedi (ui_snapshot ONCE/SONRA ayni).
#include <cstdint>
#include <string>
#include <string_view>

#include "ui/shell/screen.h"

namespace ef::ui {

// Sayfa genelinde bir kez hesaplanan olculer + paylasilan durum. Referanslar measure_page() cagrisi boyunca yasar.
struct MeasureCtx {
    Graphics& g;
    UiState& s;
    const ViewModel& vm;
    std::int64_t now;
    HitMap& hits;
    StatusSeq& seq;
    const Palette& p;
    const core::measure::MeasureState& m;
    float pad, gap, section_h, sl, bl, label_h, g0, g1, g2, chip_h, opt_h, src_h;
    bool tr, busy, locked, dark, balance_scope;
    Color divider, pill_idle;
};

namespace mp {  // measure page: model yardimcilari + bilesen geometrisi (token degil)
inline constexpr float kUnbounded = 10000.f;
inline constexpr float kOptRadius = 4.f;  // secenek cipi rounded (4); kaynak dugmesi rounded-md (6)
inline constexpr float kSrcGap = 10.f, kSrcRadius = 6.f, kStripGap = 12.f;

std::wstring widen(std::string_view s);
std::wstring replace_all(std::wstring s, std::wstring_view key, const std::wstring& val);
std::wstring date_text(std::int64_t unix_ms, bool turkish);
Color tone_color(const Palette& p, core::measure::StatusTone t);
// Onceki sonuc: yeni test surerken veya yeni test sonuc uretmeden bittiyse eski sonuc "Onceki sonuc" olarak kalir.
bool result_is_previous(const core::measure::MeasureState& m);
}  // namespace mp

}  // namespace ef::ui
