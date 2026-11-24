#include "core/insights/history.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "core/insights/comparison_policy.h"
#include "core/serialization/json.h"

namespace ef::core::insights {

using contracts::ErrorCode;

bool safe_metric_key(std::string_view k) noexcept {
    if (k.empty() || k.size() > 64) return false;
    for (char c : k)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ':' || c == '-')) return false;
    return true;
}

bool allowed_condition_key(std::string_view k) noexcept {
    for (std::string_view a : {"resolution", "tier", "mode", "os", "gpu", "gpu_driver", "cpu", "power", "storage_target", "build", "game_build", "live_panel",
                               // Denge testi (spec 2026-11-12 A5): canli test ekrani durumu, cizen bagdastirici, D3D surucu is parcacigi ozelligi.
                               "preview", "adapter", "d3d_threading",
                               // D6: kararsiz adim tekrar sayisi ("0".."2") ve ilk adimdan onceki arka plan GPU yuku
                               // sinifi ("low" / "high" / "unknown"). Kayit amaclidir; karsilastirma politikasina girmez.
                               "retries", "other_gpu"})
        if (k == a) return true;
    return false;
}

std::size_t History::add(HistoryEntry e) {
    entries_.insert(entries_.begin(), std::move(e));
    std::size_t dropped = 0;
    while (entries_.size() > kMaxEntries) {
        entries_.pop_back();
        ++dropped;
    }
    dropped_ += dropped;
    return dropped;
}

namespace {
json::Value entry_json(const HistoryEntry& e) {
    auto o = json::Value::object();
    o.set("when_unix_ms", json::Value::integer(e.when_unix_ms));
    o.set("source", json::Value::string(e.source));
    o.set("title", json::Value::string(e.title));
    o.set("fingerprint", json::Value::string(e.fingerprint));
    o.set("app_version", json::Value::string(e.app_version));
    // B3: ek alanlar; eski surumler bilinmeyen anahtari yok sayar, yeni surum eski dosyada yoksa fail-closed.
    o.set("scene_version", json::Value::string(e.scene_version));
    o.set("protocol_version", json::Value::string(e.protocol_version));
    o.set("run_valid", json::Value::boolean(e.run_valid));
    if (!e.diagnosis_version.empty() && safe_metric_key(e.diagnosis_version))
        o.set("diagnosis_version", json::Value::string(e.diagnosis_version));
    auto m = json::Value::object();
    for (const auto& [k, v] : e.metrics)
        if (safe_metric_key(k) && std::isfinite(v)) m.set(k, json::Value::number(v));
    o.set("metrics", std::move(m));
    auto x = json::Value::array();
    for (const auto& s : e.exclusions) x.push(json::Value::string(s));
    o.set("exclusions", std::move(x));
    auto cond = json::Value::object();
    for (const auto& [k, v] : e.conditions)
        if (allowed_condition_key(k) && v.size() <= 160) cond.set(k, json::Value::string(v));
    o.set("conditions", std::move(cond));
    auto series = json::Value::array();
    for (const double v : e.fps_series)
        if (std::isfinite(v) && series.items().size() < 120) series.push(json::Value::number(v));
    o.set("fps_series", std::move(series));
    return o;
}
std::string str(const json::Value& o, const char* k) {
    const auto* v = o.find(k);
    return v && v->type() == json::Value::Type::String ? v->as_string() : std::string{};
}
}  // namespace

std::string History::serialize() const {
    auto o = json::Value::object();
    o.set("schema_version", json::Value::integer(kHistorySchema));
    o.set("dropped_total", json::Value::integer(static_cast<std::int64_t>(dropped_)));
    auto arr = json::Value::array();
    for (const auto& e : entries_) arr.push(entry_json(e));
    o.set("entries", std::move(arr));
    return json::write(o);
}

contracts::Result<History> History::parse(std::string_view text) {
    using R = contracts::Result<History>;
    json::Limits lim;
    lim.max_bytes = 4u << 20;  // 500 kayit icin yeterli, sinirli
    lim.max_items = 50000;
    auto v = json::parse(text, lim);
    if (!v.ok()) return R(v.error());
    const auto& o = v.value();
    const auto* ver = o.find("schema_version");
    if (!ver || ver->type() != json::Value::Type::Int) return R::fail(ErrorCode::ParseError, "schema_version");
    if (ver->as_int() > kHistorySchema) return R::fail(ErrorCode::SchemaTooNew, "history schema");
    History h;
    if (const auto* d = o.find("dropped_total"); d && d->type() == json::Value::Type::Int && d->as_int() >= 0)
        h.dropped_ = static_cast<std::size_t>(d->as_int());
    const auto* arr = o.find("entries");
    if (!arr || arr->type() != json::Value::Type::Array) return R::fail(ErrorCode::ParseError, "entries");
    for (const auto& it : arr->items()) {
        if (it.type() != json::Value::Type::Object) continue;
        HistoryEntry e;
        if (const auto* w = it.find("when_unix_ms"); w && w->type() == json::Value::Type::Int) e.when_unix_ms = w->as_int();
        e.source = str(it, "source");
        e.title = str(it, "title");
        e.fingerprint = str(it, "fingerprint");
        e.app_version = str(it, "app_version");
        e.scene_version = str(it, "scene_version");
        e.protocol_version = str(it, "protocol_version");
        if (auto dv = str(it, "diagnosis_version"); safe_metric_key(dv)) e.diagnosis_version = std::move(dv);
        // Alan yoksa/gecersizse false: eski veya yarim kayit karsilastirmada gecerli sayilmaz.
        if (const auto* rv = it.find("run_valid"); rv && rv->type() == json::Value::Type::Bool) e.run_valid = rv->as_bool();
        if (const auto* m = it.find("metrics"); m && m->type() == json::Value::Type::Object)
            for (const auto& k : m->keys())
                if (const auto* x = m->find(k); x && (x->type() == json::Value::Type::Double || x->type() == json::Value::Type::Int) && safe_metric_key(k))
                    e.metrics[k] = x->as_double();
        if (const auto* x = it.find("exclusions"); x && x->type() == json::Value::Type::Array)
            for (const auto& s : x->items())
                if (s.type() == json::Value::Type::String) e.exclusions.push_back(s.as_string());
        if (const auto* c = it.find("conditions"); c && c->type() == json::Value::Type::Object)
            for (const auto& k : c->keys())
                if (const auto* x = c->find(k); x && x->type() == json::Value::Type::String && allowed_condition_key(k))
                    e.conditions[k] = x->as_string();
        if (const auto* fs = it.find("fps_series"); fs && fs->type() == json::Value::Type::Array)
            for (const auto& pt : fs->items())
                if ((pt.type() == json::Value::Type::Double || pt.type() == json::Value::Type::Int) && e.fps_series.size() < 120) e.fps_series.push_back(pt.as_double());
        h.entries_.push_back(std::move(e));
    }
    // Spec 2026-11-12 C: yalniz en yeni kMaxEntries kayit saklanir. Dosya en yeni basta yazilir; elle / eski surumle
    // bozulmus sira icin zamana gore (esitlerde dosya sirasi) siralanir, fazlasi (en eskiler) alinmaz ve sayilir.
    std::stable_sort(h.entries_.begin(), h.entries_.end(), [](const HistoryEntry& a, const HistoryEntry& b) { return a.when_unix_ms > b.when_unix_ms; });
    if (h.entries_.size() > kMaxEntries) {
        h.trimmed_ = h.entries_.size() - kMaxEntries;
        h.dropped_ += h.trimmed_;
        h.entries_.erase(h.entries_.begin() + static_cast<std::ptrdiff_t>(kMaxEntries), h.entries_.end());
    }
    return h;
}

std::string History::export_json(std::int64_t now_unix_ms, std::string_view only_source) const {
    auto o = json::Value::object();
    o.set("schema_version", json::Value::integer(kHistorySchema));
    o.set("kind", json::Value::string("ecufps_history_export"));
    o.set("exported_unix_ms", json::Value::integer(now_unix_ms));
    auto arr = json::Value::array();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (!only_source.empty() && e.source != only_source) continue;
        auto row = entry_json(e);  // izin listesi alanlar; yol/seri yok
        // Disa aktarim da ekranlarla ayni ortak politikayi tasir: metrik gecerliligi ve ayni kosullu onceki kayitla uyum.
        auto valid = json::Value::object();
        for (const auto& [k, v] : e.metrics)
            if (safe_metric_key(k) && std::isfinite(v)) valid.set(k, json::Value::string(std::string(metric_validity_key(metric_validity(e, k)))));
        row.set("metric_validity", std::move(valid));
        for (std::size_t j = i + 1; j < entries_.size(); ++j) {
            const auto& prev = entries_[j];
            if (prev.source != e.source || prev.fingerprint.empty() || prev.fingerprint != e.fingerprint) continue;
            const auto c = compare_compatibility(comparison_inputs(prev), comparison_inputs(e));
            auto cmp = json::Value::object();
            cmp.set("comparable", json::Value::boolean(c.comparable));
            cmp.set("note_key", json::Value::string(c.note_key));
            auto diff = json::Value::array();
            for (const auto& d : c.differing) diff.push(json::Value::string(d));
            cmp.set("differing", std::move(diff));
            cmp.set("previous_unix_ms", json::Value::integer(prev.when_unix_ms));
            // Metrik basina ayni karar: ekrandaki fark satirlariyla birebir.
            auto per_metric = json::Value::object();
            for (const auto& [k, v] : e.metrics) {
                if (!safe_metric_key(k) || !std::isfinite(v)) continue;
                const auto verdict = metric_comparable(c, prev, e, k);
                per_metric.set(k, json::Value::string(verdict.allowed ? std::string("comparable") : verdict.note_key));
            }
            cmp.set("metrics", std::move(per_metric));
            row.set("compared_to_previous", std::move(cmp));
            break;
        }
        arr.push(std::move(row));
    }
    o.set("entries", std::move(arr));
    return json::write(o);
}

}  // namespace ef::core::insights
