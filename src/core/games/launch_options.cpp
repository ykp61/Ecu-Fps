#include "core/games/launch_options.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace ef::core::games {

namespace {

struct Token {
    bool brace = false;
    char brace_char = 0;
    std::string text;
};

// VDF: anahtar/deger tirnakli, bloklar { }. // yorumlari atlanir. Bozuk tirnak -> bos.
std::optional<std::vector<Token>> tokenize(std::string_view s) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
        } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') ++i;
        } else if (c == '{' || c == '}') {
            out.push_back({true, c, {}});
            ++i;
        } else if (c == '"') {
            std::string v;
            ++i;
            bool closed = false;
            while (i < s.size()) {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    v.push_back(s[i + 1]);
                    i += 2;
                } else if (s[i] == '"') {
                    ++i;
                    closed = true;
                    break;
                } else {
                    v.push_back(s[i++]);
                }
            }
            if (!closed) return std::nullopt;
            out.push_back({false, 0, std::move(v)});
        } else {
            return std::nullopt;  // tirnaksiz cop: bozuk kabul et
        }
    }
    return out;
}

}  // namespace

std::map<std::string, std::string> parse_launch_options(std::string_view vdf) {
    std::map<std::string, std::string> out;
    const auto toks = tokenize(vdf);
    if (!toks) return out;

    // apps/<appid>/LaunchOptions yolunu izle. "apps" anahtarı görülünce bir sonraki blok uygulama
    // tablosudur; içindeki her "<appid>" bloğunda LaunchOptions aranır. Tek geçiş, derinlik sınırlı.
    int depth = 0;
    bool in_apps = false;
    int apps_depth = -1;
    std::string pending;           // bekleyen anahtar
    std::string app_id;            // apps bloğu içindeki geçerli uygulama
    int app_depth = -1;
    for (std::size_t i = 0; i < toks->size(); ++i) {
        const auto& t = (*toks)[i];
        if (t.brace) {
            if (t.brace_char == '{') {
                ++depth;
                if (!pending.empty()) {
                    if (pending == "apps" && !in_apps) {
                        in_apps = true;
                        apps_depth = depth;
                    } else if (in_apps && depth == apps_depth + 1) {
                        app_id = pending;
                        app_depth = depth;
                    }
                }
            } else {
                if (in_apps && depth == app_depth) {
                    app_id.clear();
                    app_depth = -1;
                }
                if (in_apps && depth == apps_depth) {
                    in_apps = false;
                    apps_depth = -1;
                }
                if (depth > 0) --depth;
            }
            pending.clear();
            continue;
        }
        if (pending.empty()) {
            pending = t.text;
            continue;
        }
        // anahtar + değer çifti
        if (in_apps && !app_id.empty() && depth == app_depth && pending == "LaunchOptions") out[app_id] = t.text;
        pending.clear();
    }
    return out;
}

}  // namespace ef::core::games
