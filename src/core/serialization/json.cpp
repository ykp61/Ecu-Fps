#include "core/serialization/json.h"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace ef::core::json {

using contracts::ErrorCode;
using contracts::Result;

const Value* Value::find(std::string_view key) const noexcept {
    if (type_ != Type::Object) return nullptr;
    for (std::size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) return &items_[i];
    return nullptr;
}

void Value::set(std::string key, Value v) {
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) {
            items_[i] = std::move(v);
            return;
        }
    }
    keys_.push_back(std::move(key));
    items_.push_back(std::move(v));
}

namespace {

class Parser {
public:
    Parser(std::string_view t, const Limits& l) : t_(t), l_(l) {}

    Result<Value> run() {
        if (t_.size() > l_.max_bytes) return fail(ErrorCode::ParseLimit, "input too large");
        if (t_.size() >= 3 && static_cast<unsigned char>(t_[0]) == 0xEF &&
            static_cast<unsigned char>(t_[1]) == 0xBB && static_cast<unsigned char>(t_[2]) == 0xBF)
            p_ = 3;  // UTF-8 BOM atlanir
        Value v;
        if (auto s = value(v, 0); !s.ok()) return Result<Value>(s.error());
        ws();
        if (p_ != t_.size()) return fail(ErrorCode::ParseError, "trailing data");
        return v;
    }

private:
    std::string_view t_;
    const Limits& l_;
    std::size_t p_ = 0;
    std::size_t items_ = 0;

    static Result<Value> fail(ErrorCode c, const char* d) { return Result<Value>::fail(c, d); }
    contracts::Status err(ErrorCode c, const char* d) const {
        return contracts::Status::fail(c, std::string(d) + " @" + std::to_string(p_));
    }

    void ws() {
        while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\n' || t_[p_] == '\r')) ++p_;
    }

    bool lit(std::string_view s) {
        if (t_.substr(p_, s.size()) == s) {
            p_ += s.size();
            return true;
        }
        return false;
    }

    contracts::Status value(Value& out, int depth) {
        if (depth > l_.max_depth) return err(ErrorCode::ParseLimit, "too deep");
        ws();
        if (p_ >= t_.size()) return err(ErrorCode::ParseError, "unexpected end");
        const char c = t_[p_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            std::string s;
            if (auto st = str(s); !st.ok()) return st;
            out = Value::string(std::move(s));
            return contracts::ok_status();
        }
        if (lit("true")) { out = Value::boolean(true); return contracts::ok_status(); }
        if (lit("false")) { out = Value::boolean(false); return contracts::ok_status(); }
        if (lit("null")) { out = Value::null(); return contracts::ok_status(); }
        if (c == '-' || (c >= '0' && c <= '9')) return number(out);
        return err(ErrorCode::ParseError, "unexpected character");
    }

    contracts::Status count_item() {
        if (++items_ > l_.max_items) return err(ErrorCode::ParseLimit, "too many items");
        return contracts::ok_status();
    }

    contracts::Status object(Value& out, int depth) {
        ++p_;  // {
        out = Value::object();
        ws();
        if (p_ < t_.size() && t_[p_] == '}') { ++p_; return contracts::ok_status(); }
        for (;;) {
            if (auto s = count_item(); !s.ok()) return s;
            ws();
            if (p_ >= t_.size() || t_[p_] != '"') return err(ErrorCode::ParseError, "expected key");
            std::string key;
            if (auto s = str(key); !s.ok()) return s;
            if (out.find(key)) return err(ErrorCode::ParseError, "duplicate key");
            ws();
            if (p_ >= t_.size() || t_[p_] != ':') return err(ErrorCode::ParseError, "expected ':'");
            ++p_;
            Value v;
            if (auto s = value(v, depth + 1); !s.ok()) return s;
            out.set(std::move(key), std::move(v));
            ws();
            if (p_ >= t_.size()) return err(ErrorCode::ParseError, "unterminated object");
            if (t_[p_] == ',') { ++p_; continue; }
            if (t_[p_] == '}') { ++p_; return contracts::ok_status(); }
            return err(ErrorCode::ParseError, "expected ',' or '}'");
        }
    }

    contracts::Status array(Value& out, int depth) {
        ++p_;  // [
        out = Value::array();
        ws();
        if (p_ < t_.size() && t_[p_] == ']') { ++p_; return contracts::ok_status(); }
        for (;;) {
            if (auto s = count_item(); !s.ok()) return s;
            Value v;
            if (auto s = value(v, depth + 1); !s.ok()) return s;
            out.push(std::move(v));
            ws();
            if (p_ >= t_.size()) return err(ErrorCode::ParseError, "unterminated array");
            if (t_[p_] == ',') { ++p_; continue; }
            if (t_[p_] == ']') { ++p_; return contracts::ok_status(); }
            return err(ErrorCode::ParseError, "expected ',' or ']'");
        }
    }

    static void put_utf8(std::string& s, std::uint32_t cp) {
        if (cp < 0x80) {
            s += static_cast<char>(cp);
        } else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(std::uint32_t& out) {
        if (p_ + 4 > t_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t_[p_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    // Ham UTF-8 dizisini dogrular (asiri uzun kodlama ve vekil kod noktalari reddedilir).
    bool utf8_seq(std::string& s) {
        const auto b0 = static_cast<unsigned char>(t_[p_]);
        int n;
        std::uint32_t cp;
        if (b0 >= 0xC2 && b0 <= 0xDF) { n = 1; cp = b0 & 0x1F; }
        else if (b0 >= 0xE0 && b0 <= 0xEF) { n = 2; cp = b0 & 0x0F; }
        else if (b0 >= 0xF0 && b0 <= 0xF4) { n = 3; cp = b0 & 0x07; }
        else return false;
        if (p_ + static_cast<std::size_t>(n) >= t_.size()) return false;
        for (int i = 1; i <= n; ++i) {
            const auto b = static_cast<unsigned char>(t_[p_ + static_cast<std::size_t>(i)]);
            if ((b & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (b & 0x3F);
        }
        if ((n == 2 && cp < 0x800) || (n == 3 && (cp < 0x10000 || cp > 0x10FFFF))) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        s.append(t_.substr(p_, static_cast<std::size_t>(n) + 1));
        p_ += static_cast<std::size_t>(n) + 1;
        return true;
    }

    contracts::Status str(std::string& s) {
        ++p_;  // "
        for (;;) {
            if (p_ >= t_.size()) return err(ErrorCode::ParseError, "unterminated string");
            if (s.size() > l_.max_string) return err(ErrorCode::ParseLimit, "string too long");
            const auto c = static_cast<unsigned char>(t_[p_]);
            if (c == '"') { ++p_; break; }
            if (c < 0x20) return err(ErrorCode::ParseError, "control character");
            if (c == '\\') {
                ++p_;
                if (p_ >= t_.size()) return err(ErrorCode::ParseError, "bad escape");
                const char e = t_[p_++];
                switch (e) {
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/': s += '/'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'n': s += '\n'; break;
                    case 'r': s += '\r'; break;
                    case 't': s += '\t'; break;
                    case 'u': {
                        std::uint32_t cp;
                        if (!hex4(cp)) return err(ErrorCode::ParseError, "bad \\u");
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            std::uint32_t lo;
                            if (!lit("\\u") || !hex4(lo) || lo < 0xDC00 || lo > 0xDFFF)
                                return err(ErrorCode::ParseError, "bad surrogate");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                            return err(ErrorCode::ParseError, "lone surrogate");
                        }
                        if (cp == 0) return err(ErrorCode::ParseError, "NUL in string");
                        put_utf8(s, cp);
                        break;
                    }
                    default: return err(ErrorCode::ParseError, "bad escape");
                }
                continue;
            }
            if (c < 0x80) { s += static_cast<char>(c); ++p_; continue; }
            if (!utf8_seq(s)) return err(ErrorCode::ParseError, "invalid UTF-8");
        }
        if (s.size() > l_.max_string) return err(ErrorCode::ParseLimit, "string too long");
        return contracts::ok_status();
    }

    contracts::Status number(Value& out) {
        // Kati JSON sayi dilbilgisi: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
        const std::size_t start = p_;
        auto digit = [this]() { return p_ < t_.size() && t_[p_] >= '0' && t_[p_] <= '9'; };
        if (t_[p_] == '-') ++p_;
        if (!digit()) return err(ErrorCode::ParseError, "bad number");
        if (t_[p_] == '0') {
            ++p_;
            if (digit()) return err(ErrorCode::ParseError, "leading zero");
        } else {
            while (digit()) ++p_;
        }
        bool is_float = false;
        if (p_ < t_.size() && t_[p_] == '.') {
            is_float = true;
            ++p_;
            if (!digit()) return err(ErrorCode::ParseError, "bad fraction");
            while (digit()) ++p_;
        }
        if (p_ < t_.size() && (t_[p_] == 'e' || t_[p_] == 'E')) {
            is_float = true;
            ++p_;
            if (p_ < t_.size() && (t_[p_] == '+' || t_[p_] == '-')) ++p_;
            if (!digit()) return err(ErrorCode::ParseError, "bad exponent");
            while (digit()) ++p_;
        }
        const std::string_view num = t_.substr(start, p_ - start);
        if (num.size() > 64) return err(ErrorCode::ParseLimit, "number too long");
        if (!is_float) {
            std::int64_t v = 0;
            auto [ptr, ec] = std::from_chars(num.data(), num.data() + num.size(), v);
            if (ec == std::errc::result_out_of_range) return err(ErrorCode::OutOfRange, "integer overflow");
            if (ec != std::errc() || ptr != num.data() + num.size()) return err(ErrorCode::ParseError, "bad integer");
            out = Value::integer(v);
            return contracts::ok_status();
        }
        double d = 0;
        auto [ptr, ec] = std::from_chars(num.data(), num.data() + num.size(), d);
        if (ec == std::errc::result_out_of_range || !std::isfinite(d)) return err(ErrorCode::OutOfRange, "number range");
        if (ec != std::errc() || ptr != num.data() + num.size()) return err(ErrorCode::ParseError, "bad number");
        out = Value::number(d);
        return contracts::ok_status();
    }
};

void write_string(std::string& o, const std::string& s) {
    o += '"';
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    o += buf;
                } else {
                    o += ch;
                }
        }
    }
    o += '"';
}

void write_value(std::string& o, const Value& v) {
    switch (v.type()) {
        case Value::Type::Null: o += "null"; break;
        case Value::Type::Bool: o += v.as_bool() ? "true" : "false"; break;
        case Value::Type::Int: o += std::to_string(v.as_int()); break;
        case Value::Type::Double: {
            char buf[32];
            auto [ptr, ec] = std::to_chars(buf, buf + sizeof buf, v.as_double());
            o.append(buf, ec == std::errc() ? ptr : buf);
            break;
        }
        case Value::Type::String: write_string(o, v.as_string()); break;
        case Value::Type::Array: {
            o += '[';
            bool first = true;
            for (const auto& it : v.items()) {
                if (!first) o += ',';
                first = false;
                write_value(o, it);
            }
            o += ']';
            break;
        }
        case Value::Type::Object: {
            o += '{';
            for (std::size_t i = 0; i < v.keys().size(); ++i) {
                if (i) o += ',';
                write_string(o, v.keys()[i]);
                o += ':';
                write_value(o, v.items()[i]);
            }
            o += '}';
            break;
        }
    }
}

}  // namespace

Result<Value> parse(std::string_view text, const Limits& limits) { return Parser(text, limits).run(); }

std::string write(const Value& v) {
    std::string o;
    write_value(o, v);
    return o;
}

Result<int> check_schema(const Value& root, int supported) {
    if (!root.is(Value::Type::Object)) return Result<int>::fail(ErrorCode::ParseError, "root not object");
    auto v = get_int(root, "schema_version", 1, 1'000'000);
    if (!v.ok()) return Result<int>(v.error());
    if (v.value() > supported) return Result<int>::fail(ErrorCode::SchemaTooNew, std::to_string(v.value()));
    return static_cast<int>(v.value());
}

Result<std::string> get_string(const Value& obj, std::string_view key, std::size_t max_len) {
    const Value* v = obj.find(key);
    if (!v) return Result<std::string>::fail(ErrorCode::MissingValue, std::string(key));
    if (!v->is(Value::Type::String)) return Result<std::string>::fail(ErrorCode::ParseError, std::string(key));
    if (v->as_string().size() > max_len) return Result<std::string>::fail(ErrorCode::ParseLimit, std::string(key));
    return v->as_string();
}

Result<std::int64_t> get_int(const Value& obj, std::string_view key, std::int64_t min, std::int64_t max) {
    const Value* v = obj.find(key);
    if (!v) return Result<std::int64_t>::fail(ErrorCode::MissingValue, std::string(key));
    if (!v->is(Value::Type::Int)) return Result<std::int64_t>::fail(ErrorCode::ParseError, std::string(key));
    if (v->as_int() < min || v->as_int() > max) return Result<std::int64_t>::fail(ErrorCode::OutOfRange, std::string(key));
    return v->as_int();
}

Result<bool> get_bool(const Value& obj, std::string_view key) {
    const Value* v = obj.find(key);
    if (!v) return Result<bool>::fail(ErrorCode::MissingValue, std::string(key));
    if (!v->is(Value::Type::Bool)) return Result<bool>::fail(ErrorCode::ParseError, std::string(key));
    return v->as_bool();
}

}  // namespace ef::core::json
