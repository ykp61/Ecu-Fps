#include "core/logging/redact.h"

#include <algorithm>
#include <cctype>

namespace ef::core::logging {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool ieq_at(std::string_view s, std::size_t pos, std::string_view needle) {
    if (pos + needle.size() > s.size()) return false;
    for (std::size_t i = 0; i < needle.size(); ++i)
        if (lower(s[pos + i]) != lower(needle[i])) return false;
    return true;
}

void replace_all_ci(std::string& s, std::string_view needle, std::string_view repl) {
    if (needle.size() < 2) return;  // tek karakterlik "sir" her seyi bozar
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (ieq_at(s, i, needle)) {
            out += repl;
            i += needle.size();
        } else {
            out += s[i++];
        }
    }
    s.swap(out);
}

bool is_name_char(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x80 || std::isalnum(u) || c == '_' || c == '-' || c == '.' || c == ' ' || c == '$';
}

// "\Users\<ad>" veya "/Users/<ad>" -> "\Users\<user>"
void mask_profile_paths(std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if ((s[i] == '\\' || s[i] == '/') && ieq_at(s, i + 1, "users") && i + 6 < s.size() &&
            (s[i + 6] == '\\' || s[i + 6] == '/')) {
            out.append(s, i, 7);
            std::size_t j = i + 7;
            while (j < s.size() && is_name_char(s[j]) && s[j] != '\\' && s[j] != '/') ++j;
            // sondaki bosluklari ada dahil etme
            std::size_t end = j;
            while (end > i + 7 && s[end - 1] == ' ') --end;
            if (end > i + 7) out += "<user>";
            out.append(s, end, j - end);
            i = j;
        } else {
            out += s[i++];
        }
    }
    s.swap(out);
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

void mask_sids(std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if ((s[i] == 'S' || s[i] == 's') && i + 3 < s.size() && s[i + 1] == '-' && s[i + 2] == '1' && s[i + 3] == '-' &&
            (i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1])))) {
            std::size_t j = i + 4;
            int groups = 0;
            while (j < s.size() && is_digit(s[j])) {
                while (j < s.size() && is_digit(s[j])) ++j;
                ++groups;
                if (j + 1 < s.size() && s[j] == '-' && is_digit(s[j + 1])) ++j;
                else break;
            }
            if (groups >= 2) {
                out += "<sid>";
                i = j;
                continue;
            }
        }
        out += s[i++];
    }
    s.swap(out);
}

bool email_local(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-' || c == '+' || c == '%';
}
bool email_domain(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-'; }

void mask_emails(std::string& s) {
    std::string out;
    std::size_t last = 0;
    for (std::size_t at = s.find('@'); at != std::string::npos; at = s.find('@', at + 1)) {
        std::size_t b = at;
        while (b > last && email_local(s[b - 1])) --b;
        std::size_t e = at + 1;
        while (e < s.size() && email_domain(s[e])) ++e;
        const std::string_view dom(s.data() + at + 1, e - at - 1);
        if (b < at && dom.find('.') != std::string_view::npos && dom.front() != '.' && dom.back() != '.') {
            out.append(s, last, b - last);
            out += "<email>";
            last = e;
            at = e - 1;
        }
    }
    out.append(s, last, std::string::npos);
    s.swap(out);
}

void mask_key_values(std::string& s) {
    static constexpr std::string_view keys[] = {"password=", "passwd=", "token=", "secret=", "apikey=",
                                                "api_key=", "password:", "token:", "bearer "};
    for (const auto key : keys) {
        std::string out;
        for (std::size_t i = 0; i < s.size();) {
            if (ieq_at(s, i, key)) {
                out.append(s, i, key.size());
                std::size_t j = i + key.size();
                while (j < s.size() && s[j] == ' ') ++j;
                std::size_t k = j;
                while (k < s.size() && s[k] != ' ' && s[k] != ';' && s[k] != ',' && s[k] != '&' && s[k] != '"') ++k;
                if (k > j) out += "<redacted>";
                i = k;
            } else {
                out += s[i++];
            }
        }
        s.swap(out);
    }
}

}  // namespace

std::string redact(std::string_view text, const RedactionContext& ctx) {
    std::string s(text);
    std::replace(s.begin(), s.end(), '\r', ' ');
    std::replace(s.begin(), s.end(), '\n', ' ');

    for (const auto& secret : ctx.extra_secrets) replace_all_ci(s, secret, "<redacted>");
    if (!ctx.profile_dir.empty()) replace_all_ci(s, ctx.profile_dir, "<profile>");
    mask_profile_paths(s);
    mask_key_values(s);
    mask_sids(s);
    mask_emails(s);
    if (!ctx.machine_name.empty()) replace_all_ci(s, ctx.machine_name, "<machine>");
    if (!ctx.user_name.empty()) replace_all_ci(s, ctx.user_name, "<user>");
    return s;
}

}  // namespace ef::core::logging
