#include "core/i18n/turkish.h"

#include <cstdint>
#include <vector>

namespace ef::core::i18n {

namespace {

// UTF-8 coz; gecersiz bayt U+FFFD olarak tek bayt ilerler.
std::vector<char32_t> decode(std::string_view s) {
    std::vector<char32_t> out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        int n = 0;
        char32_t cp = 0;
        if (b < 0x80) { cp = b; n = 0; }
        else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; n = 1; }
        else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; n = 2; }
        else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; n = 3; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (n > 0 && i + static_cast<std::size_t>(n) >= s.size()) {  // kesik dizi
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        bool ok = true;
        for (int k = 1; k <= n; ++k) {
            const auto c = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            if ((c & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (c & 0x3F);
        }
        if (!ok) { out.push_back(0xFFFD); ++i; continue; }
        out.push_back(cp);
        i += static_cast<std::size_t>(n) + 1;
    }
    return out;
}

void encode(std::string& o, char32_t cp) {
    if (cp < 0x80) o += static_cast<char>(cp);
    else if (cp < 0x800) {
        o += static_cast<char>(0xC0 | (cp >> 6));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        o += static_cast<char>(0xE0 | (cp >> 12));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        o += static_cast<char>(0xF0 | (cp >> 18));
        o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

constexpr char32_t I_DOT = 0x0130;     // İ
constexpr char32_t DOTLESS_I = 0x0131; // ı

char32_t up(char32_t c) {
    if (c == U'i') return I_DOT;
    if (c == DOTLESS_I) return U'I';
    if (c >= U'a' && c <= U'z') return c - 32;
    switch (c) {
        case 0x00E7: return 0x00C7;  // ç
        case 0x011F: return 0x011E;  // ğ
        case 0x00F6: return 0x00D6;  // ö
        case 0x015F: return 0x015E;  // ş
        case 0x00FC: return 0x00DC;  // ü
        case 0x00E2: return 0x00C2;  // â
        case 0x00EE: return 0x00CE;  // î
        case 0x00FB: return 0x00DB;  // û
        default: return c;
    }
}

char32_t low(char32_t c) {
    if (c == U'I') return DOTLESS_I;
    if (c == I_DOT) return U'i';
    if (c >= U'A' && c <= U'Z') return c + 32;
    switch (c) {
        case 0x00C7: return 0x00E7;
        case 0x011E: return 0x011F;
        case 0x00D6: return 0x00F6;
        case 0x015E: return 0x015F;
        case 0x00DC: return 0x00FC;
        case 0x00C2: return 0x00E2;
        case 0x00CE: return 0x00EE;
        case 0x00DB: return 0x00FB;
        default: return c;
    }
}

// Turkce alfabe sirasi icin birincil agirlik (kucuk harf uzerinden).
std::uint32_t weight(char32_t lower_c) {
    // Sapkali harfler birincil sirada temel harfle esittir (â=a, î=i, û=u).
    if (lower_c == 0x00E2) lower_c = U'a';
    if (lower_c == 0x00EE) lower_c = U'i';
    if (lower_c == 0x00FB) lower_c = U'u';
    static constexpr char32_t kOrder[] = {U'a', U'b', U'c', 0x00E7, U'd', U'e', U'f', U'g', 0x011F, U'h', DOTLESS_I,
                                          U'i', U'j', U'k', U'l', U'm', U'n', U'o', 0x00F6, U'p', U'q', U'r',
                                          U's', 0x015F, U't', U'u', 0x00FC, U'v', U'w', U'x', U'y', U'z'};
    for (std::uint32_t i = 0; i < sizeof(kOrder) / sizeof(kOrder[0]); ++i)
        if (kOrder[i] == lower_c) return 1000 + i;
    if (lower_c < 0x80) return static_cast<std::uint32_t>(lower_c);  // rakam/noktalama harflerden once
    return 0x10000 + static_cast<std::uint32_t>(lower_c);
}

}  // namespace

std::string tr_upper(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 4);
    for (char32_t c : decode(s)) encode(o, up(c));
    return o;
}

std::string tr_lower(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (char32_t c : decode(s)) encode(o, low(c));
    return o;
}

int tr_compare(std::string_view a, std::string_view b) {
    const auto da = decode(a), db = decode(b);
    const std::size_t n = da.size() < db.size() ? da.size() : db.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto wa = weight(low(da[i])), wb = weight(low(db[i]));
        if (wa != wb) return wa < wb ? -1 : 1;
    }
    if (da.size() != db.size()) return da.size() < db.size() ? -1 : 1;
    // Ikincil: kucuk harf once
    for (std::size_t i = 0; i < n; ++i) {
        const bool la = low(da[i]) == da[i], lb = low(db[i]) == db[i];
        if (la != lb) return la ? -1 : 1;
    }
    return 0;
}

}  // namespace ef::core::i18n
