#include "core/capabilities/registry.h"

#include <algorithm>

#include "core/support/zip_writer.h"  // crc32

namespace ef::core::capabilities {

using contracts::Capability;
using contracts::ErrorCode;
using contracts::SupportState;

namespace embedded_data {
extern const std::string_view setting_catalog;
}

std::string_view embedded_catalog_json() noexcept { return embedded_data::setting_catalog; }

std::string_view to_string(FeatureGroup g) noexcept {
    switch (g) {
        case FeatureGroup::Windows: return "windows";
        case FeatureGroup::Gpu: return "gpu";
        case FeatureGroup::Game: return "game";
        case FeatureGroup::Hardware: return "hardware";
        case FeatureGroup::Startup: return "startup";
        case FeatureGroup::Excluded: return "excluded";
    }
    return "?";
}

std::string_view to_string(InvalidationEvent e) noexcept {
    switch (e) {
        case InvalidationEvent::DriverChanged: return "driver_changed";
        case InvalidationEvent::OsUpdated: return "os_updated";
        case InvalidationEvent::DeviceChanged: return "device_changed";
        case InvalidationEvent::AppUpdated: return "app_updated";
        case InvalidationEvent::UserRequested: return "user_requested";
    }
    return "?";
}

namespace {
std::optional<FeatureGroup> group_from_id(std::string_view id) {
    if (id.size() < 2) return std::nullopt;
    switch (id[0]) {
        case 'W': return FeatureGroup::Windows;
        case 'G': return FeatureGroup::Gpu;
        case 'O': return FeatureGroup::Game;
        case 'H': return FeatureGroup::Hardware;
        case 'B': return FeatureGroup::Startup;
        case 'X': return FeatureGroup::Excluded;
        default: return std::nullopt;
    }
}
}  // namespace

contracts::Result<std::vector<FeatureDef>> parse_feature_catalog(std::string_view text) {
    using R = contracts::Result<std::vector<FeatureDef>>;
    json::Limits lim;
    lim.max_items = 5000;
    auto doc = json::parse(text, lim);
    if (!doc.ok()) return R(doc.error());
    if (!doc.value().is(json::Value::Type::Array)) return R::fail(ErrorCode::ParseError, "catalog not array");
    std::vector<FeatureDef> out;
    for (const auto& item : doc.value().items()) {
        auto id = json::get_string(item, "id", 8);
        if (!id.ok()) return R(id.error());
        auto g = group_from_id(id.value());
        if (!g) return R::fail(ErrorCode::InvalidEnum, "feature id " + id.value());
        auto name = json::get_string(item, "name", 256);
        if (!name.ok()) return R(name.error());
        auto rel = json::get_bool(item, "release_enabled");
        if (!rel.ok()) return R(rel.error());
        for (const auto& f : out)
            if (f.id == id.value()) return R::fail(ErrorCode::ParseError, "duplicate " + id.value());
        out.push_back({id.value(), *g, name.value(), rel.value()});
    }
    return out;
}

const std::vector<FeatureDef>& embedded_features() {
    static const std::vector<FeatureDef> f = [] {
        auto r = parse_feature_catalog(embedded_catalog_json());
        return r.ok() ? r.value() : std::vector<FeatureDef>{};
    }();
    return f;
}

std::string fingerprint(const Environment& e) {
    std::vector<std::string> d = e.gpu_drivers;
    std::sort(d.begin(), d.end());
    std::string s = "os=" + e.os_build + ";app=" + e.app_version;
    for (const auto& x : d) s += ";gpu=" + x;
    char buf[16];
    const auto c = support::crc32(s);
    static constexpr char hex[] = "0123456789abcdef";
    for (int i = 7; i >= 0; --i) buf[7 - i] = hex[(c >> (i * 4)) & 0xF];
    return std::string(buf, 8) + ":" + s;
}

const FeatureDef* Registry::feature(std::string_view id) const noexcept {
    for (const auto& f : features_)
        if (f.id == id) return &f;
    return nullptr;
}

contracts::Status Registry::record(const Capability& cap, const std::string& env_fp) {
    if (!feature(cap.feature_id)) return contracts::Status::fail(ErrorCode::InvalidEnum, "unknown feature " + cap.feature_id);
    if (!contracts::capability_is_well_formed(cap)) return contracts::Status::fail(ErrorCode::ParseError, "malformed capability");
    entries_[cap.feature_id + "|" + cap.device_scope] = Entry{cap, env_fp};
    return contracts::ok_status();
}

Capability Registry::query(std::string_view feature_id, std::string_view device_scope, const std::string& current_env_fp) const {
    Capability c;
    c.feature_id = std::string(feature_id);
    c.device_scope = std::string(device_scope);
    const auto* f = feature(feature_id);
    if (!f) {
        c.state = SupportState::Unknown;
        c.reason = "unknown feature";
        return c;
    }
    if (f->group == FeatureGroup::Excluded) {
        c.state = SupportState::Unsupported;
        c.reason = "excluded by product policy";
        return c;
    }
    const auto it = entries_.find(c.feature_id + "|" + c.device_scope);
    if (it == entries_.end()) {
        c.state = SupportState::Unknown;
        c.reason = "not queried";
        return c;
    }
    if (it->second.env_fingerprint.empty() || it->second.env_fingerprint != current_env_fp) {
        c = it->second.cap;
        c.state = SupportState::Unknown;
        c.reason = "stale: environment changed since validation";
        return c;
    }
    return it->second.cap;
}

bool Registry::release_enabled(std::string_view feature_id) const {
    const auto* f = feature(feature_id);
    if (!f || f->group == FeatureGroup::Excluded) return false;
    return contracts::may_enable_release(feature_id, evidence_);
}

std::size_t Registry::invalidate(InvalidationEvent ev) {
    std::size_t n = 0;
    for (auto& [key, e] : entries_) {
        const auto* f = feature(e.cap.feature_id);
        bool hit = true;
        if (ev == InvalidationEvent::DriverChanged) hit = f && f->group == FeatureGroup::Gpu;
        if (hit && !e.env_fingerprint.empty()) {
            e.env_fingerprint.clear();
            ++n;
        }
    }
    return n;
}

json::Value Registry::support_matrix(const std::string& current_env_fp) const {
    auto arr = json::Value::array();
    for (const auto& f : features_) {
        // Kaydedilen tum cihaz kapsamlari; hic yoksa "system" kapsaminda bilinmeyen satir.
        std::vector<std::string> scopes;
        for (const auto& [key, e] : entries_)
            if (e.cap.feature_id == f.id) scopes.push_back(e.cap.device_scope);
        if (scopes.empty()) scopes.push_back("system");
        for (const auto& sc : scopes) {
            const auto c = query(f.id, sc, current_env_fp);
            auto o = json::Value::object();
            o.set("feature", json::Value::string(f.id));
            o.set("group", json::Value::string(std::string(to_string(f.group))));
            o.set("name", json::Value::string(f.name));
            o.set("scope", json::Value::string(sc));
            o.set("state", json::Value::string(std::string(contracts::to_string(c.state))));
            o.set("reason", json::Value::string(c.reason));
            o.set("provider", json::Value::string(c.provider));
            o.set("release_enabled", json::Value::boolean(release_enabled(f.id)));
            arr.push(std::move(o));
        }
    }
    auto root = json::Value::object();
    root.set("schema_version", json::Value::integer(1));
    root.set("environment", json::Value::string(current_env_fp));
    root.set("features", std::move(arr));
    return root;
}

json::Value Registry::save() const {
    auto arr = json::Value::array();
    for (const auto& [key, e] : entries_) {
        auto o = json::Value::object();
        o.set("feature", json::Value::string(e.cap.feature_id));
        o.set("scope", json::Value::string(e.cap.device_scope));
        o.set("state", json::Value::string(std::string(contracts::to_string(e.cap.state))));
        o.set("provider", json::Value::string(e.cap.provider));
        o.set("provider_version", json::Value::string(e.cap.provider_version));
        o.set("reason", json::Value::string(e.cap.reason));
        o.set("privilege", json::Value::string(std::string(contracts::to_string(e.cap.required))));
        o.set("evidence", json::Value::string(std::string(contracts::to_string(e.cap.evidence))));
        o.set("env", json::Value::string(e.env_fingerprint));
        arr.push(std::move(o));
    }
    auto root = json::Value::object();
    root.set("schema_version", json::Value::integer(1));
    root.set("entries", std::move(arr));
    return root;
}

contracts::Status Registry::load(const json::Value& v) {
    using contracts::Status;
    if (auto s = json::check_schema(v, 1); !s.ok()) return Status(s.error());
    const auto* arr = v.find("entries");
    if (!arr || !arr->is(json::Value::Type::Array)) return Status::fail(ErrorCode::ParseError, "entries");
    std::map<std::string, Entry> loaded;
    for (const auto& o : arr->items()) {
        Capability c;
        auto f = json::get_string(o, "feature", 8);
        auto sc = json::get_string(o, "scope", 128);
        auto st = json::get_string(o, "state", 32);
        auto pr = json::get_string(o, "provider", 128);
        auto pv = json::get_string(o, "provider_version", 64);
        auto rs = json::get_string(o, "reason", 512);
        auto pg = json::get_string(o, "privilege", 16);
        auto ev = json::get_string(o, "evidence", 32);
        auto en = json::get_string(o, "env", 1024);
        if (!f.ok() || !sc.ok() || !st.ok() || !pr.ok() || !pv.ok() || !rs.ok() || !pg.ok() || !ev.ok() || !en.ok())
            return Status::fail(ErrorCode::ParseError, "entry fields");
        auto state = contracts::parse_support_state(st.value());
        auto priv = contracts::parse_privilege(pg.value());
        auto evl = contracts::parse_evidence_level(ev.value());
        if (!state || !priv || !evl) return Status::fail(ErrorCode::InvalidEnum, "entry enum");
        if (!feature(f.value())) continue;  // katalogdan kaldirilmis ozellik sessizce dusurulur
        c = Capability{f.value(), sc.value(), *state, pr.value(), pv.value(), rs.value(), *priv, *evl};
        if (!contracts::capability_is_well_formed(c)) return Status::fail(ErrorCode::ParseError, "malformed entry");
        loaded[c.feature_id + "|" + c.device_scope] = Entry{c, en.value()};
    }
    entries_ = std::move(loaded);
    return contracts::ok_status();
}

}  // namespace ef::core::capabilities
