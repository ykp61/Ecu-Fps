#pragma once
// B11 (P9 site/hukuk) test yardimcilari: depo dosyalarini okur, HTML'den gorunen metni ayiklar.
// Yalniz kaynak denetimidir: ag yok, dagitim yok, depo disina yazmaz.
// Not: MSVC std::regex uzun girdide ozyineleme derinligiyle yigin tasirabilir; bu yuzden ayiklama find tabanlidir.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace ef::test::b11 {

inline std::string read_repo(const std::string& rel) {
    std::ifstream f(std::string(EF_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline bool repo_exists(const std::string& rel) { return std::filesystem::exists(std::string(EF_SOURCE_ROOT) + "/" + rel); }

inline std::string lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// <style>/<script> bloklarini atar.
inline std::string strip_blocks(std::string s) {
    for (const char* tag : {"style", "script"}) {
        const std::string open = std::string("<") + tag, close = std::string("</") + tag + ">";
        std::size_t a;
        while ((a = s.find(open)) != std::string::npos) {
            const auto b = s.find(close, a);
            s.erase(a, b == std::string::npos ? std::string::npos : b + close.size() - a);
        }
    }
    return s;
}

// Etiketleri atar (nitelik degerleri dahil); gorunen metin kalir.
inline std::string strip_tags(const std::string& s) {
    std::string out;
    bool in_tag = false;
    for (char c : s) {
        if (c == '<') {
            in_tag = true;
            out += ' ';
        } else if (c == '>' && in_tag) {
            in_tag = false;
        } else if (!in_tag) {
            out += c;
        }
    }
    return out;
}

// name="..." degerleri (href, src, content, ...). Tirnakli niteliklerin hepsi.
inline std::vector<std::string> attr_values(const std::string& html, const std::string& name) {
    std::vector<std::string> out;
    const std::string key = name + "=\"";
    std::size_t pos = 0;
    while ((pos = html.find(key, pos)) != std::string::npos) {
        const bool boundary = pos == 0 || !(std::isalnum(static_cast<unsigned char>(html[pos - 1])) || html[pos - 1] == '-');
        pos += key.size();
        if (!boundary) continue;
        const auto end = html.find('"', pos);
        if (end == std::string::npos) break;
        out.push_back(html.substr(pos, end - pos));
        pos = end;
    }
    return out;
}

// Okunan metin + baslik + meta aciklamalari: iddia taramasi bunun uzerinde yapilir (CSS/JS ve nitelik gurultusu yok).
inline std::string claim_text(const std::string& html) {
    std::string t = strip_tags(strip_blocks(html));
    for (const auto& c : attr_values(html, "content")) t += "\n" + c;
    return t;
}

inline std::string collapse_ws(const std::string& s) {
    std::string out;
    bool sp = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            sp = true;
        } else {
            if (sp && !out.empty()) out += ' ';
            sp = false;
            out += c;
        }
    }
    return out;
}

// site/ altindaki tum .html dosyalari (site/ gorece, '/' ayiracli).
inline std::vector<std::string> site_html_files() {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    const fs::path root = fs::path(EF_SOURCE_ROOT) / "site";
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file() || e.path().extension() != ".html") continue;
        out.push_back(fs::relative(e.path(), root).generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// "ad" + "daki" gibi bolunmus parcalardan calisma aninda kurulur: bu dosyanin kendisi yasakli adi icermez.
inline std::vector<std::string> forbidden_competitor_names() {
    return {std::string("fec") + "tor"};
}

// Bir dizeden "X = 'a','b'" benzeri C++ sabit listesinin tirnakli ogelerini cikarir.
inline std::vector<std::string> quoted_items(const std::string& s) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while ((pos = s.find('"', pos)) != std::string::npos) {
        const auto end = s.find('"', pos + 1);
        if (end == std::string::npos) break;
        out.push_back(s.substr(pos + 1, end - pos - 1));
        pos = end + 1;
    }
    return out;
}

// `anchor` sonrasindaki ilk {...} blogunun govdesi.
inline std::string braced_after(const std::string& src, const std::string& anchor) {
    const auto a = src.find(anchor);
    if (a == std::string::npos) return {};
    const auto open = src.find('{', a);
    if (open == std::string::npos) return {};
    const auto close = src.find('}', open);
    if (close == std::string::npos) return {};
    return src.substr(open + 1, close - open - 1);
}

}  // namespace ef::test::b11
