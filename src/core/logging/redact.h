#pragma once
// Kisisel veri ve sir maskeleme. Normal log ve export bu filtreden gecmeden yazilmaz.

#include <string>
#include <string_view>
#include <vector>

namespace ef::core::logging {

struct RedactionContext {
    std::string user_name;     // ornek: testuser
    std::string machine_name;  // ornek: DESKTOP-ABC
    std::string profile_dir;   // ornek: C:\Users\testuser
    std::vector<std::string> extra_secrets;  // diger bilinen hassas degerler
};

// Maskeler:
//  - baglamdaki kullanici/makine/profil/ek sirlar (ASCII buyuk-kucuk harf duyarsiz)
//  - herhangi bir "\Users\<ad>" profil yolu -> \Users\<user>
//  - Windows SID (S-1-5-...) -> <sid>
//  - e-posta adresleri -> <email>
//  - "password=", "token=", "secret=", "apikey=" ve "Bearer " sonrasi deger -> <redacted>
// Satir sonlari bosluga cevrilir (log enjeksiyonu engellenir).
std::string redact(std::string_view text, const RedactionContext& ctx);

}  // namespace ef::core::logging
