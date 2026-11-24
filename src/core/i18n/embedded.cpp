#include <string_view>

#include "core/i18n/catalog.h"

namespace ef::core::i18n::embedded_data {
extern const std::string_view tr_TR;
extern const std::string_view en_US;
}  // namespace ef::core::i18n::embedded_data

namespace ef::core::i18n {

std::string_view embedded_json(Locale l) noexcept {
    return l == Locale::TrTR ? embedded_data::tr_TR : embedded_data::en_US;
}

}  // namespace ef::core::i18n
