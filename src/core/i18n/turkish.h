#pragma once
// Turkce buyuk/kucuk harf ve alfabe sirasi (UTF-8). Sistem yerel ayarina bagli degildir:
// i <-> I tuzagi: Turkcede i -> I degil, i -> I (noktali buyuk I, U+0130); I -> i degil, I -> ı (U+0131).

#include <string>
#include <string_view>

namespace ef::core::i18n {

std::string tr_upper(std::string_view utf8);
std::string tr_lower(std::string_view utf8);

// Turkce alfabe sirasina gore karsilastirma: a b c ç d e f g ğ h ı i j k l m n o ö p r s ş t u ü v y z.
// Buyuk/kucuk harf once yok sayilir, esitlikte kucuk harf once gelir. Sonuc <0, 0, >0.
int tr_compare(std::string_view a, std::string_view b);

}  // namespace ef::core::i18n
