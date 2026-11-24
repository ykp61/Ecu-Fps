#include "core/games/consent.h"

#include <algorithm>

#include "core/serialization/json.h"

namespace ef::core::games {

using contracts::ErrorCode;
using contracts::Result;

std::string_view to_string(Consent c) noexcept {
    switch (c) {
        case Consent::Ask: return "ask";
        case Consent::Allow: return "allow";
        case Consent::Deny: return "deny";
    }
    return "ask";
}

namespace {
std::optional<Consent> parse_consent(std::string_view s) {
    if (s == "allow") return Consent::Allow;
    if (s == "deny") return Consent::Deny;
    if (s == "ask") return Consent::Ask;
    return std::nullopt;
}
}  // namespace

std::string normalize_game_id(std::string_view p) {
    std::size_t b = 0, e = p.size();
    while (b < e && (p[b] == ' ' || p[b] == '"')) ++b;
    while (e > b && (p[e - 1] == ' ' || p[e - 1] == '"')) --e;
    std::string out;
    out.reserve(e - b);
    for (std::size_t i = b; i < e; ++i) {
        char c = p[i];
        if (c == '/') c = '\\';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        out.push_back(c);
    }
    return out;
}

Consent ConsentStore::get(const std::string& id) const {
    const auto it = entries_.find(id);
    return it == entries_.end() ? Consent::Ask : it->second.decision;
}

void ConsentStore::set(const std::string& id, std::string display_name, Consent c, std::int64_t when) {
    if (id.empty()) return;
    if (c == Consent::Ask) {
        clear(id);
        return;
    }
    if (!entries_.count(id) && entries_.size() >= kMaxEntries) {
        // Sinir: en eski karar dusurulur (sonsuz buyume yok).
        auto oldest = std::min_element(entries_.begin(), entries_.end(),
                                       [](const auto& a, const auto& b) { return a.second.decided_unix_ms < b.second.decided_unix_ms; });
        entries_.erase(oldest);
    }
    entries_[id] = ConsentEntry{id, std::move(display_name), c, when};
}

bool ConsentStore::clear(const std::string& id) { return entries_.erase(id) > 0; }

std::vector<ConsentEntry> ConsentStore::list() const {
    std::vector<ConsentEntry> v;
    for (const auto& [id, e] : entries_) v.push_back(e);
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        return a.display_name != b.display_name ? a.display_name < b.display_name : a.id < b.id;
    });
    return v;
}

std::string ConsentStore::serialize() const {
    auto root = json::Value::object();
    root.set("schema_version", json::Value::integer(1));
    auto arr = json::Value::array();
    for (const auto& e : list()) {
        auto o = json::Value::object();
        o.set("id", json::Value::string(e.id));
        o.set("name", json::Value::string(e.display_name));
        o.set("decision", json::Value::string(std::string(to_string(e.decision))));
        o.set("decided_unix_ms", json::Value::integer(e.decided_unix_ms));
        arr.push(std::move(o));
    }
    root.set("games", std::move(arr));
    return json::write(root);
}

Result<ConsentStore> ConsentStore::parse(std::string_view text) {
    auto v = json::parse(text);
    if (!v.ok()) return v.error();
    const auto& root = v.value();
    const auto* ver = root.find("schema_version");
    if (!ver || ver->type() != json::Value::Type::Int) return Result<ConsentStore>::fail(ErrorCode::ParseError, "schema_version");
    if (ver->as_int() != 1) return Result<ConsentStore>::fail(ErrorCode::SchemaTooNew, "consent schema");
    const auto* games = root.find("games");
    if (!games || games->type() != json::Value::Type::Array) return Result<ConsentStore>::fail(ErrorCode::ParseError, "games");
    ConsentStore s;
    for (const auto& g : games->items()) {
        const auto* id = g.find("id");
        const auto* name = g.find("name");
        const auto* dec = g.find("decision");
        const auto* when = g.find("decided_unix_ms");
        if (!id || !name || !dec || id->type() != json::Value::Type::String || name->type() != json::Value::Type::String ||
            dec->type() != json::Value::Type::String)
            return Result<ConsentStore>::fail(ErrorCode::ParseError, "entry");
        const auto c = parse_consent(dec->as_string());
        if (!c) return Result<ConsentStore>::fail(ErrorCode::InvalidEnum, "decision");
        s.set(normalize_game_id(id->as_string()), name->as_string(), *c,
              when && when->type() == json::Value::Type::Int ? when->as_int() : 0);
    }
    return s;
}

}  // namespace ef::core::games
