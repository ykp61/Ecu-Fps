// B7 (P9 i18n/gizlilik): metin katalogu denetimleri. Yalniz kaynak dosyalari okur (ag yok, yazma yok).
//  1) Anahtar esligi: design/strings/tr-TR.json, en-US.json ve src/core/i18n/string_ids.h ayni anahtar kumesi.
//  2) Yer tutucu esligi: {n}, {e}... her anahtarda tr ve en ayni coklukta.
//  3) Bekleyen metin dosyalari (design/strings/pending/*.json) bicim ve islenmislik.
//  4) en-US terim sozlugu (docs/EN_US_GLOSSARY.md) katalogla tutarli.
//  5) Kanitsiz sayi / FPS vaadi tarayan metin testi.
//  6) Rakip adi gecmez.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/capabilities/vendor_matrix.h"
#include "core/i18n/catalog.h"
#include "core/i18n/turkish.h"
#include "core/net/speed_test.h"
#include "core/serialization/json.h"
#include "ef_test.h"
#include "site_test_util.h"

namespace {

namespace fs = std::filesystem;
namespace json = ef::core::json;
using ef::test::b11::read_repo;
using ef::test::b11::repo_exists;

using TextMap = std::map<std::string, std::string>;  // anahtar -> metin (JSON sirasi onemsiz)

bool load_strings(const std::string& rel, TextMap& out) {
    auto doc = json::parse(read_repo(rel));  // tekrarli anahtar okuyucuda reddedilir
    if (!doc.ok()) return false;
    const json::Value* s = doc.value().find("strings");
    if (!s || !s->is(json::Value::Type::Object)) return false;
    for (std::size_t i = 0; i < s->keys().size(); ++i) {
        if (!s->items()[i].is(json::Value::Type::String)) return false;
        out[s->keys()[i]] = s->items()[i].as_string();
    }
    return true;
}

struct IdLine {
    std::string ident, key;
};

// string_ids.h icindeki X(Kimlik, "anahtar") satirlari.
std::vector<IdLine> load_ids() {
    std::vector<IdLine> out;
    const std::string text = read_repo("src/core/i18n/string_ids.h");
    const auto def = text.find("#define EF_STRING_IDS");
    // Bir satirda birden cok X(...) olabilir.
    for (std::size_t x = text.find("X(", def + 1); x != std::string::npos; x = text.find("X(", x + 2)) {
        const auto comma = text.find(',', x);
        const auto q1 = text.find('"', comma);
        const auto q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
        if (comma == std::string::npos || q2 == std::string::npos) break;
        out.push_back({text.substr(x + 2, comma - x - 2), text.substr(q1 + 1, q2 - q1 - 1)});
    }
    return out;
}

bool is_ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// {ad} yer tutucularini siralayip dondurur; bozuk suslu parantez varsa ok=false.
std::vector<std::string> placeholders(const std::string& s, bool& ok) {
    std::vector<std::string> out;
    ok = true;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '}') {
            ok = false;
        } else if (s[i] == '{') {
            std::size_t j = i + 1;
            while (j < s.size() && is_ident_char(s[j])) ++j;
            if (j >= s.size() || s[j] != '}' || j == i + 1 || std::isdigit(static_cast<unsigned char>(s[i + 1]))) {
                ok = false;
                continue;
            }
            out.push_back(s.substr(i, j - i + 1));
            i = j;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool valid_key(const std::string& k) {
    if (k.empty() || k.front() == '.' || k.back() == '.') return false;
    for (char c : k)
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '_')) return false;
    return k.find("..") == std::string::npos;
}

bool valid_utf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (std::size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

std::string lower_for(const std::string& s, bool tr) { return tr ? ef::core::i18n::tr_lower(s) : ef::test::b11::lower_ascii(s); }

bool contains(const std::string& s, std::string_view needle) { return s.find(needle) != std::string::npos; }

struct Pending {
    std::string file;
    std::map<std::string, std::pair<std::string, std::string>> items;  // anahtar -> {tr, en}
    bool well_formed = true;
};

std::vector<Pending> load_pending() {
    std::vector<Pending> out;
    const fs::path dir = fs::path(EF_SOURCE_ROOT) / "design" / "strings" / "pending";
    if (!fs::exists(dir)) return out;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        Pending p;
        p.file = f.filename().string();
        auto doc = json::parse(read_repo("design/strings/pending/" + p.file));
        if (!doc.ok() || !doc.value().is(json::Value::Type::Object)) {
            p.well_formed = false;
        } else {
            for (std::size_t i = 0; i < doc.value().keys().size(); ++i) {
                const auto& v = doc.value().items()[i];
                if (!v.is(json::Value::Type::Array) || v.items().size() != 2 || !v.items()[0].is(json::Value::Type::String) ||
                    !v.items()[1].is(json::Value::Type::String)) {
                    p.well_formed = false;
                    continue;
                }
                p.items[doc.value().keys()[i]] = {v.items()[0].as_string(), v.items()[1].as_string()};
            }
        }
        out.push_back(std::move(p));
    }
    return out;
}

// ---- Kanitsiz sayi / FPS vaadi tarayicisi ----------------------------------------------------------------------

// Cumleye boler: . ! ? ; : sonrasi bosluk veya satir sonu. Yer tutucular '#' olur ({gain} gibi adlar kelime sayilmaz).
std::vector<std::string> sentences(const std::string& text) {
    std::string t;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{') {
            const auto close = text.find('}', i);
            if (close != std::string::npos) {
                t += '#';
                i = close;
                continue;
            }
        }
        t += text[i];
    }
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i < t.size(); ++i) {
        cur += t[i];
        const char c = t[i];
        const bool end = c == '\n' || ((c == '.' || c == '!' || c == '?' || c == ';' || c == ':') && (i + 1 == t.size() || t[i + 1] == ' '));
        if (end) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool has_number_percent(const std::string& s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') continue;
        std::size_t b = i;
        while (b > 0 && s[b - 1] == ' ') --b;
        if (b > 0 && (std::isdigit(static_cast<unsigned char>(s[b - 1])) || s[b - 1] == '#')) return true;
        std::size_t a = i + 1;
        while (a < s.size() && s[a] == ' ') ++a;
        if (a < s.size() && (std::isdigit(static_cast<unsigned char>(s[a])) || s[a] == '#')) return true;
    }
    return false;
}

// "+15 FPS", "+ 20%": arti isaretli sayi + (fps | %).
bool has_plus_number_gain(const std::string& s) {
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        if (s[i] != '+') continue;
        std::size_t j = i + 1;
        while (j < s.size() && s[j] == ' ') ++j;
        const std::size_t d0 = j;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
        if (j == d0) continue;
        while (j < s.size() && s[j] == ' ') ++j;
        if (j < s.size() && s[j] == '%') return true;
        if (s.compare(j, 3, "fps") == 0) return true;
    }
    return false;
}

bool any_of(const std::string& s, std::initializer_list<std::string_view> needles) {
    for (auto n : needles)
        if (s.find(n) != std::string::npos) return true;
    return false;
}

// Tam kelime eslesmesi (harf = ASCII harf/rakam veya UTF-8 baytlari): "garanti" garantiyi/garantisi icinde sayilmaz.
bool has_word(const std::string& s, std::string_view w) {
    auto is_letter = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80; };
    for (std::size_t p = s.find(w); p != std::string::npos; p = s.find(w, p + 1)) {
        const bool left = p == 0 || !is_letter(s[p - 1]);
        const bool right = p + w.size() >= s.size() || !is_letter(s[p + w.size()]);
        if (left && right) return true;
    }
    return false;
}

// Olumsuzlama / sinirlama isaretleri: bunlar varsa cumle bir vaat degil, bir uyaridir ("FPS artisi vaat edilmez").
bool negated(const std::string& l, bool tr) {
    if (tr) {
        if (any_of(l, {"değil", "edilmez", "verilmez", "etmez", "vermez", " yok", "olmaz", "sağlamaz", "beklenmez", "yapmaz", "iddia"})) return true;
        // -maz / -mez ile biten herhangi bir kelime ("artırmaz", "hızlandırmaz")
        std::string w;
        for (char c : l + ' ') {
            if (c == ' ' || c == ',' || c == '(' || c == ')' || c == ';' || c == ':' || c == '.' || c == '!' || c == '?') {
                if (w.size() > 3 && (w.compare(w.size() - 3, 3, "maz") == 0 || w.compare(w.size() - 3, 3, "mez") == 0)) return true;
                w.clear();
            } else {
                w += c;
            }
        }
        return false;
    }
    const std::string p = " " + l + " ";
    return any_of(p, {" not ", "n't", " no ", "never", "without", "cannot"});
}

// Bir cumle kanitsiz bir sayi / FPS / hizlanma vaadi tasiyor mu? Aciklama (neden) doner, yoksa bos.
std::string promise_reason(const std::string& sentence, bool tr) {
    const std::string l = lower_for(sentence, tr);
    if (has_plus_number_gain(l)) return "+N FPS / +N% kalibi";  // olumsuzlamayla da yasak: sayi yazilmaz
    if (negated(l, tr)) return {};
    const bool gain = tr ? any_of(l, {"hızlan", "daha hızlı", "daha yüksek", "artış", "artar", "artır", "yüksel", "kazanç", "kazandır", "iyileş"})
                         : any_of(l, {"faster", "boost", "more fps", "higher fps", "increase", "gain", "improve", "speed up", "speeds up"});
    if (gain && has_number_percent(l)) return "sayi + yuzde + kazanc sozu";
    const bool promise = tr ? (has_word(l, "garanti") || has_word(l, "garantili") || any_of(l, {"hızlandırır", "hızlandırıyor", "kazandırır"}))
                            : any_of(l, {"guarantee", "speeds up", "makes games faster", "boosts", "supercharge"});
    if (promise) return "garanti / hizlandirir vaadi";
    if (gain && l.find("fps") != std::string::npos) return "FPS artisi sozu";
    return {};
}

// Olculmus deney sonucu cumleleri (bu deneyde, %95 aralikla): kosula bagli, vaat degil. Gerekce teste yazili.
const std::set<std::string> kMeasuredResultKeys{"proof.result.better", "proof.result.worse"};

}  // namespace

// ---- 1) Anahtar esligi ------------------------------------------------------------------------------------------

EF_TEST(i18n_key_sets_match_across_tr_en_and_string_ids) {
    TextMap tr, en;
    EF_CHECK(load_strings("design/strings/tr-TR.json", tr));
    EF_CHECK(load_strings("design/strings/en-US.json", en));
    const auto ids = load_ids();
    EF_CHECK(tr.size() > 1000);
    EF_CHECK(ids.size() == tr.size());
    std::set<std::string> id_keys;
    for (const auto& i : ids) id_keys.insert(i.key);
    EF_CHECK(id_keys.size() == ids.size());  // string_ids.h'ta tekrarli anahtar yok
    for (const auto& [k, v] : tr) {
        if (!en.count(k)) {
            ef::test::report_failure("anahtar en-US'te yok", __FILE__, __LINE__);
            std::fprintf(stderr, "  tr-TR only: %s\n", k.c_str());
        }
        if (!id_keys.count(k)) {
            ef::test::report_failure("anahtar string_ids.h'ta yok", __FILE__, __LINE__);
            std::fprintf(stderr, "  no X(...) for: %s\n", k.c_str());
        }
    }
    for (const auto& [k, v] : en) {
        if (!tr.count(k)) {
            ef::test::report_failure("anahtar tr-TR'de yok", __FILE__, __LINE__);
            std::fprintf(stderr, "  en-US only: %s\n", k.c_str());
        }
    }
    for (const auto& k : id_keys) {
        if (!tr.count(k)) {
            ef::test::report_failure("string_ids.h anahtari JSON'da yok", __FILE__, __LINE__);
            std::fprintf(stderr, "  X(...) without text: %s\n", k.c_str());
        }
    }
    // Kod tarafindaki tablo (derlenmis) ayni sayida ve her anahtar ters aranabilir.
    EF_CHECK(ef::core::i18n::kStringCount == ids.size());
}

EF_TEST(i18n_string_ids_have_unique_idents_and_well_formed_keys) {
    const auto ids = load_ids();
    std::set<std::string> idents;
    for (const auto& i : ids) {
        EF_CHECK(!i.ident.empty() && std::isupper(static_cast<unsigned char>(i.ident.front())));
        if (!idents.insert(i.ident).second) {
            ef::test::report_failure("tekrarli kimlik", __FILE__, __LINE__);
            std::fprintf(stderr, "  duplicate ident %s\n", i.ident.c_str());
        }
        if (!valid_key(i.key)) {
            ef::test::report_failure("bozuk anahtar bicimi", __FILE__, __LINE__);
            std::fprintf(stderr, "  bad key %s\n", i.key.c_str());
        }
    }
}

// ---- 2) Yer tutucu esligi + metin sagligi -------------------------------------------------------------------------

EF_TEST(i18n_placeholders_match_between_tr_and_en) {
    TextMap tr, en;
    EF_CHECK(load_strings("design/strings/tr-TR.json", tr));
    EF_CHECK(load_strings("design/strings/en-US.json", en));
    std::size_t with_placeholders = 0;
    for (const auto& [k, tv] : tr) {
        const auto it = en.find(k);
        if (it == en.end()) continue;
        bool ok_tr = true, ok_en = true;
        const auto ptr = placeholders(tv, ok_tr);
        const auto pen = placeholders(it->second, ok_en);
        if (!ok_tr || !ok_en) {
            ef::test::report_failure("bozuk yer tutucu / bosta suslu parantez", __FILE__, __LINE__);
            std::fprintf(stderr, "  %s (%s)\n", k.c_str(), ok_tr ? "en-US" : "tr-TR");
        }
        if (ptr != pen) {
            ef::test::report_failure("yer tutucular tr/en farkli", __FILE__, __LINE__);
            std::fprintf(stderr, "  %s: tr %zu, en %zu\n", k.c_str(), ptr.size(), pen.size());
        }
        if (!ptr.empty()) ++with_placeholders;
    }
    EF_CHECK(with_placeholders > 100);  // tarama gercekten yer tutuculu metinleri gordu
}

EF_TEST(i18n_placeholder_checker_catches_mismatch_examples) {
    bool ok = true;
    EF_CHECK((placeholders("{n} dosya, {e} hata", ok) == std::vector<std::string>{"{e}", "{n}"}) && ok);
    placeholders("{n eksik", ok);
    EF_CHECK(!ok);
    placeholders("fazla }", ok);
    EF_CHECK(!ok);
    placeholders("bos {} yer", ok);
    EF_CHECK(!ok);
    EF_CHECK(placeholders("{n} {n}", ok).size() == 2);  // coklukla karsilastirilir
    EF_CHECK(placeholders("%{pct} bos", ok) == std::vector<std::string>{"{pct}"});
}

EF_TEST(i18n_texts_are_well_formed_and_not_untranslated) {
    TextMap tr, en;
    EF_CHECK(load_strings("design/strings/tr-TR.json", tr));
    EF_CHECK(load_strings("design/strings/en-US.json", en));
    // Sozlesme: lisans metinleri ve dil-bagimsiz ifadeler iki dilde ayni olabilir.
    const std::set<std::string> same_ok{"chk.apply_change", "measure.result.balance_pair"};
    for (const auto* m : {&tr, &en}) {
        for (const auto& [k, v] : *m) {
            if (v.empty() || v.size() > 512 || !valid_utf8(v) || v.front() == ' ' || v.back() == ' ' ||
                v.find('\t') != std::string::npos || v.find('\r') != std::string::npos) {
                ef::test::report_failure("bos / uzun / bozuk bicimli metin", __FILE__, __LINE__);
                std::fprintf(stderr, "  %s\n", k.c_str());
            }
            for (const char* bad : {"TODO", "FIXME", "lorem", "XXX"})
                if (contains(v, bad)) {
                    ef::test::report_failure("yer tutucu metin", __FILE__, __LINE__);
                    std::fprintf(stderr, "  %s has %s\n", k.c_str(), bad);
                }
        }
    }
    for (const auto& [k, v] : tr) {
        const auto it = en.find(k);
        if (it == en.end() || v != it->second || v.size() <= 24) continue;
        if (k.rfind("about.", 0) == 0 || same_ok.count(k)) continue;
        ef::test::report_failure("uzun metin iki dilde ayni (ceviri eksik)", __FILE__, __LINE__);
        std::fprintf(stderr, "  %s\n", k.c_str());
    }
}

// ---- 3) Bekleyen metin dosyalari ---------------------------------------------------------------------------------

EF_TEST(i18n_pending_files_are_well_formed_and_applied) {
    TextMap tr, en;
    EF_CHECK(load_strings("design/strings/tr-TR.json", tr));
    EF_CHECK(load_strings("design/strings/en-US.json", en));
    const auto ids = load_ids();
    std::set<std::string> id_keys;
    for (const auto& i : ids) id_keys.insert(i.key);
    for (const auto& p : load_pending()) {
        if (!p.well_formed) {
            ef::test::report_failure("bekleyen dosya {anahtar: [tr, en]} bicimine uymuyor", __FILE__, __LINE__);
            std::fprintf(stderr, "  %s\n", p.file.c_str());
        }
        for (const auto& [k, pair] : p.items) {
            bool ok1 = true, ok2 = true;
            if (placeholders(pair.first, ok1) != placeholders(pair.second, ok2) || !ok1 || !ok2 || pair.first.empty() || pair.second.empty()) {
                ef::test::report_failure("bekleyen metin: yer tutucu / bos", __FILE__, __LINE__);
                std::fprintf(stderr, "  %s: %s\n", p.file.c_str(), k.c_str());
            }
            // Islenmemis bekleyen anahtar: scripts/add_strings.py ile islenip dosya silinmeli.
            if (!tr.count(k) || !en.count(k) || !id_keys.count(k) || tr[k] != pair.first || en[k] != pair.second) {
                ef::test::report_failure("bekleyen anahtar katalogda yok / farkli: scripts/add_strings.py ile isle", __FILE__, __LINE__);
                std::fprintf(stderr, "  %s: %s\n", p.file.c_str(), k.c_str());
            }
        }
    }
}

// B8 destek matrisi gerekce metinleri (kodda tek kaynak) katalogla birebir ayni.
EF_TEST(i18n_vendor_matrix_reason_texts_are_in_the_catalog) {
    using namespace ef::core::capabilities;
    TextMap tr, en;
    EF_CHECK(load_strings("design/strings/tr-TR.json", tr));
    EF_CHECK(load_strings("design/strings/en-US.json", en));
    for (const auto& r : kReasonTexts) {
        const std::string key(r.key);
        EF_CHECK(key.rfind("hw.matrix.", 0) == 0);
        if (!tr.count(key) || !en.count(key)) {
            ef::test::report_failure("gerekce anahtari katalogda yok", __FILE__, __LINE__);
            std::fprintf(stderr, "  %s\n", key.c_str());
            continue;
        }
        EF_CHECK(tr[key] == r.tr);
        EF_CHECK(en[key] == r.en);
    }
}

// ---- 4) en-US terim sozlugu --------------------------------------------------------------------------------------

namespace {

struct Term {
    std::string tr, en;
};

// docs/EN_US_GLOSSARY.md: "| tr terimi | en terimi | ... |" satirlari ("Terim" ve "---" satirlari atlanir).
std::vector<Term> load_glossary() {
    std::vector<Term> out;
    std::istringstream in(read_repo("docs/EN_US_GLOSSARY.md"));
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("| ", 0) != 0) continue;
        std::vector<std::string> cells;
        std::size_t pos = 1;
        while (pos < line.size()) {
            const auto next = line.find('|', pos);
            if (next == std::string::npos) break;
            std::string c = line.substr(pos, next - pos);
            const auto b = c.find_first_not_of(' ');
            const auto e = c.find_last_not_of(' ');
            cells.push_back(b == std::string::npos ? "" : c.substr(b, e - b + 1));
            pos = next + 1;
        }
        if (cells.size() < 2 || cells[0] == "Türkçe" || cells[0].rfind("---", 0) == 0 || cells[0].empty()) continue;
        out.push_back({cells[0], cells[1]});
    }
    return out;
}

}  // namespace

EF_TEST(i18n_en_glossary_exists_and_catalog_follows_it) {
    EF_CHECK(repo_exists("docs/EN_US_GLOSSARY.md"));
    const auto terms = load_glossary();
    EF_CHECK(terms.size() >= 6);
    TextMap tr, en;
    EF_CHECK(load_strings("design/strings/tr-TR.json", tr));
    EF_CHECK(load_strings("design/strings/en-US.json", en));
    for (const auto& t : terms) {
        const std::string tl = lower_for(t.tr, true), el = lower_for(t.en, false);
        std::size_t uses = 0;
        for (const auto& [k, v] : tr) {
            if (!contains(lower_for(v, true), tl)) continue;
            ++uses;
            const auto it = en.find(k);
            if (it == en.end() || !contains(lower_for(it->second, false), el)) {
                ef::test::report_failure("terim sozlugune uymayan en-US metin", __FILE__, __LINE__);
                std::fprintf(stderr, "  %s: tr terimi \"%s\" -> en \"%s\" bekleniyor\n", k.c_str(), t.tr.c_str(), t.en.c_str());
            }
        }
        if (uses == 0) {
            ef::test::report_failure("sozluk terimi katalogda hic kullanilmiyor", __FILE__, __LINE__);
            std::fprintf(stderr, "  %s\n", t.tr.c_str());
        }
    }
}

// ---- 5) Kanitsiz sayi / FPS vaadi --------------------------------------------------------------------------------

EF_TEST(i18n_promise_scanner_flags_unproven_claims_and_allows_warnings) {
    // Yasak kaliplar (tr)
    for (const char* bad : {"Oyunlarınız %30 daha hızlı çalışır.", "FPS artışı garanti.", "+15 FPS kazanın.", "Bilgisayarınızı hızlandırır.",
                            "Ortalama %20 daha yüksek FPS.", "Her oyunda garanti performans."})
        EF_CHECK(!promise_reason(bad, true).empty());
    // Yasak kaliplar (en)
    for (const char* bad : {"Games run 30% faster.", "Guaranteed FPS gain.", "Get +20 FPS now.", "Boosts your FPS by 25%.", "Speeds up your games.",
                            "Up to 15% more FPS."})
        EF_CHECK(!promise_reason(bad, false).empty());
    // Olumsuzlayan / uyaran cumleler serbest
    for (const char* ok : {"FPS artışı vaat edilmez.", "Garanti edilen performans değildir.", "Oyun içi FPS'e etkisi genelde küçüktür.",
                           "FPS'i pek artırmaz;", "Fark yok.", "Sonuç yalnız bu sahne içindir; oyun FPS'i tahmini değildir."})
        EF_CHECK(promise_reason(ok, true).empty());
    for (const char* ok : {"No FPS gain is promised.", "It is not a game FPS prediction.", "Does not guarantee performance.",
                           "The difference is {gain} ms ({pct}%) shorter."})
        EF_CHECK(promise_reason(ok, false).empty());
}

EF_TEST(i18n_catalogs_make_no_unproven_number_or_fps_promises) {
    for (const bool tr_locale : {true, false}) {
        TextMap m;
        EF_CHECK(load_strings(tr_locale ? "design/strings/tr-TR.json" : "design/strings/en-US.json", m));
        for (const auto& [k, v] : m) {
            if (kMeasuredResultKeys.count(k)) {
                // Olculmus sonuc: "bu deneyde / in this experiment" + %95 aralik ile sinirli; baska hicbir vaat tasimaz.
                EF_CHECK(contains(lower_for(v, tr_locale), tr_locale ? "bu deneyde" : "in this experiment"));
                EF_CHECK(contains(v, "95%") || contains(v, "%95"));
                continue;
            }
            for (const auto& s : sentences(v)) {
                const auto why = promise_reason(s, tr_locale);
                if (!why.empty()) {
                    ef::test::report_failure("kanitsiz sayi / FPS vaadi", __FILE__, __LINE__);
                    std::fprintf(stderr, "  [%s] %s (%s): %s\n", tr_locale ? "tr-TR" : "en-US", k.c_str(), why.c_str(), s.c_str());
                }
            }
        }
    }
}

// Vaat denetimi: "Ecu Fps surucu veya guncelleme kurmaz" metni (kullanilmiyordu, urunle celisiyordu) geri gelmez; urun
// Windows Update surucusunu kurar (Evet/Hayir + geri yukleme noktasi), ve "kurmaz" ifadeleri bunu dogru anlatir.
EF_TEST(i18n_driver_install_wording_does_not_contradict_the_product) {
    TextMap tr, en;
    EF_CHECK(load_strings("design/strings/tr-TR.json", tr));
    EF_CHECK(load_strings("design/strings/en-US.json", en));
    EF_CHECK(!tr.count("update.no_install_note") && !en.count("update.no_install_note"));
    for (const auto& [k, v] : tr) {
        const auto l = lower_for(v, true);
        EF_CHECK(!contains(l, "sürücü veya güncelleme kurmaz"));
        EF_CHECK(!contains(l, "uygulama sürücü kurmaz"));  // yanlis: Windows Update surucusu kurulabilir
        EF_CHECK(!contains(l, "ecu fps sürücü kurmaz"));
    }
    for (const auto& [k, v] : en) {
        const auto l = lower_for(v, false);
        EF_CHECK(!contains(l, "does not install drivers"));
        EF_CHECK(!contains(l, "doesn't install drivers"));
        EF_CHECK(!contains(l, "does not install or remove drivers"));
        EF_CHECK(!contains(l, "app does not install"));
    }
    // Kurulum yolu gercekten var: kur dugmesi ve onay metni iki dilde mevcut.
    EF_CHECK(tr.count("updates.install") && tr.count("updates.install.question") && en.count("updates.install.question"));
}

// ---- Ayarlar'da ag aciklamasi -------------------------------------------------------------------------------------

EF_TEST(i18n_settings_explains_speed_test_and_update_check) {
    TextMap tr, en;
    EF_CHECK(load_strings("design/strings/tr-TR.json", tr));
    EF_CHECK(load_strings("design/strings/en-US.json", en));
    for (const char* key : {"settings.net_note_speed", "settings.net_note_update"}) {
        EF_CHECK(tr.count(key) && en.count(key));
        if (!tr.count(key) || !en.count(key)) continue;
        EF_CHECK(tr[key].size() <= 512 && en[key].size() <= 512);
    }
    if (!tr.count("settings.net_note_speed") || !tr.count("settings.net_note_update")) return;
    // Hiz testi: yalniz Evet sonrasi, en cok 256 MB / 60 sn, kisisel veri yok; oyun aciksa calismaz.
    const auto& s_tr = tr["settings.net_note_speed"];
    const auto& s_en = en["settings.net_note_speed"];
    for (const char* needle : {"Evet", "256 MB", "60 sn", "speed.cloudflare.com", "kişisel veri"}) EF_CHECK(contains(s_tr, needle));
    for (const char* needle : {"Yes", "256 MB", "60 s", "speed.cloudflare.com", "personal data"}) EF_CHECK(contains(s_en, needle));
    // Sayilar koddaki sabitlerle ayni (metin ile politika ayrismaz).
    static_assert(ef::core::net::kSpeedMaxBytes == (256ull << 20) && ef::core::net::kSpeedMaxSeconds == 60.0, "metin 256 MB / 60 sn der");
    // Guncelleme: acilista bir kez + Ayarlar'daki Denetle; yalniz imzali surum bilgisi.
    const auto& u_tr = tr["settings.net_note_update"];
    const auto& u_en = en["settings.net_note_update"];
    for (const char* needle : {"açılışta bir kez", "github.com", "gönderilmez"}) EF_CHECK(contains(u_tr, needle));
    for (const char* needle : {"once", "github.com", "not sent"}) EF_CHECK(contains(u_en, needle));
}

// ---- 6) Rakip adi gecmez ------------------------------------------------------------------------------------------

EF_TEST(i18n_catalogs_and_pending_never_name_the_competitor) {
    for (const auto& name : ef::test::b11::forbidden_competitor_names()) {
        for (const char* rel : {"design/strings/tr-TR.json", "design/strings/en-US.json", "src/core/i18n/string_ids.h", "docs/EN_US_GLOSSARY.md"}) {
            if (!repo_exists(rel)) continue;
            EF_CHECK(lower_for(read_repo(rel), false).find(name) == std::string::npos);
        }
        for (const auto& p : load_pending()) EF_CHECK(lower_for(read_repo("design/strings/pending/" + p.file), false).find(name) == std::string::npos);
    }
}
