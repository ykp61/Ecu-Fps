#include "core/cleanup/admin_transfer.h"

#include <algorithm>
#include <map>
#include <optional>
#include <utility>

#include "core/serialization/json.h"

namespace ef::core::cleanup {

namespace {

namespace js = core::json;
using contracts::ErrorCode;
using R = contracts::Result<std::string>;

js::Limits limits() {
    js::Limits l;
    l.max_bytes = 32 * 1024;
    l.max_depth = 6;
    l.max_string = 64;
    l.max_items = 256;
    return l;
}

bool ascii_volume(std::string_view v, std::string& out) {
    if (v.size() > 8) return false;
    for (const char c : v)
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) return false;
    const std::wstring w(v.begin(), v.end());
    const auto n = normalize_path(w);
    if (!n || n->size() != 3) return false;  // yalniz "X:\" koku
    out.assign(v);
    if (std::size_t pos = out.find('/'); pos != std::string::npos) out[pos] = '\\';
    return true;
}

std::string_view status_id(PreviewStatus s) noexcept {
    switch (s) {
        case PreviewStatus::Ready: return "ready";
        case PreviewStatus::NotPresent: return "not_present";
        case PreviewStatus::NeedsAdmin: return "needs_admin";
        case PreviewStatus::OtherVolume: return "other_volume";
        case PreviewStatus::NotScanned: return "not_scanned";
        case PreviewStatus::Error: return "error";
    }
    return "error";
}

std::optional<PreviewStatus> status_from(std::string_view s) noexcept {
    if (s == "ready") return PreviewStatus::Ready;
    if (s == "not_present") return PreviewStatus::NotPresent;
    if (s == "needs_admin") return PreviewStatus::NeedsAdmin;
    if (s == "other_volume") return PreviewStatus::OtherVolume;
    if (s == "not_scanned") return PreviewStatus::NotScanned;
    if (s == "error") return PreviewStatus::Error;
    return std::nullopt;
}

contracts::Result<std::vector<CleanupCategory>> parse_categories(const js::Value& root, std::size_t expected_keys) {
    using RC = contracts::Result<std::vector<CleanupCategory>>;
    if (root.keys().size() != expected_keys) return RC::fail(ErrorCode::ParseError, "request fields");
    const js::Value* arr = root.find("categories");
    if (!arr || !arr->is(js::Value::Type::Array)) return RC::fail(ErrorCode::MissingValue, "categories");
    const auto& items = arr->items();
    if (items.empty() || items.size() > admin_product_categories().size())
        return RC::fail(ErrorCode::OutOfRange, "categories count");
    std::vector<CleanupCategory> out;
    for (const auto& it : items) {
        if (!it.is(js::Value::Type::String)) return RC::fail(ErrorCode::ParseError, "category id");
        const auto c = category_from_id(it.as_string());
        if (!c || !is_admin_product_category(*c)) return RC::fail(ErrorCode::InvalidEnum, "category id");
        if (std::find(out.begin(), out.end(), *c) != out.end()) return RC::fail(ErrorCode::InvalidEnum, "duplicate category");
        out.push_back(*c);
    }
    return out;
}

void set_tally(js::Value& obj, std::string_view key, const Tally& t) {
    auto o = js::Value::object();
    o.set("files", js::Value::integer(static_cast<std::int64_t>(t.files)));
    o.set("bytes", js::Value::integer(static_cast<std::int64_t>(std::min<std::uint64_t>(t.bytes, 0x7FFFFFFFFFFFFFFFll))));
    obj.set(std::string(key), std::move(o));
}

contracts::Result<Tally> get_tally(const js::Value& obj, std::string_view key) {
    using RT = contracts::Result<Tally>;
    const js::Value* v = obj.find(key);
    if (!v || !v->is(js::Value::Type::Object)) return RT::fail(ErrorCode::MissingValue, std::string(key));
    auto f = js::get_int(*v, "files", 0, 0x7FFFFFFFFFFFFFFFll);
    auto b = js::get_int(*v, "bytes", 0, 0x7FFFFFFFFFFFFFFFll);
    if (!f.ok()) return RT(b.error());
    if (!b.ok()) return RT(b.error());
    Tally t;
    t.files = static_cast<std::size_t>(f.value());
    t.bytes = static_cast<std::uint64_t>(b.value());
    return t;
}

std::string wroot(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (const wchar_t c : w) {
        if (c < 0x20 || c > 0x7E) return {};
        s.push_back(static_cast<char>(c));
    }
    return s;
}

}  // namespace

const std::vector<CleanupCategory>& admin_product_categories() {
    static const std::vector<CleanupCategory> cats = {
        CleanupCategory::WindowsTemp,
        CleanupCategory::WerReportsSystem,
        CleanupCategory::DeliveryOptimization,
    };
    return cats;
}

bool is_admin_product_category(CleanupCategory c) noexcept {
    const auto& cats = admin_product_categories();
    return std::find(cats.begin(), cats.end(), c) != cats.end();
}

std::optional<CleanupCategory> category_from_id(std::string_view id) noexcept {
    for (const auto c : kAllCategories)
        if (to_string(c) == id) return c;
    return std::nullopt;
}

std::string encode_admin_scan_request(const AdminScanRequest& r) {
    auto root = js::Value::object();
    root.set("v", js::Value::integer(kAdminTransferVersion));
    auto arr = js::Value::array();
    for (const auto c : r.categories) arr.push(js::Value::string(std::string(to_string(c))));
    root.set("categories", std::move(arr));
    root.set("volume", js::Value::string(r.volume));
    return js::write(root);
}

contracts::Result<AdminScanRequest> decode_admin_scan_request(std::string_view payload) {
    using RS = contracts::Result<AdminScanRequest>;
    auto doc = js::parse(payload, limits());
    if (!doc.ok()) return RS(doc.error());
    const auto& root = doc.value();
    if (!root.is(js::Value::Type::Object)) return RS::fail(ErrorCode::ParseError, "request object expected");
    for (const auto& k : root.keys())
        if (k != "v" && k != "categories" && k != "volume") return RS::fail(ErrorCode::ParseError, "unknown request field");
    auto v = js::get_int(root, "v", 0, 1000);
    if (!v.ok()) return RS(v.error());
    if (v.value() != kAdminTransferVersion)
        return RS::fail(v.value() > kAdminTransferVersion ? ErrorCode::SchemaTooNew : ErrorCode::InvalidEnum, "version");
    auto cats = parse_categories(root, 3);
    if (!cats.ok()) return RS(cats.error());
    auto vol = js::get_string(root, "volume", 8);
    if (!vol.ok()) return RS(vol.error());
    AdminScanRequest r;
    r.categories = std::move(cats).value();
    if (!vol.value().empty() && !ascii_volume(vol.value(), r.volume)) return RS::fail(ErrorCode::ParseError, "volume");
    return r;
}

std::string encode_admin_scan_result(const std::vector<CleanupPreview>& previews) {
    auto root = js::Value::object();
    root.set("kind", js::Value::string("scan"));
    auto arr = js::Value::array();
    for (const auto& p : previews) {
        auto o = js::Value::object();
        o.set("id", js::Value::string(std::string(to_string(p.category))));
        o.set("status", js::Value::string(std::string(status_id(p.status))));
        o.set("files", js::Value::integer(static_cast<std::int64_t>(p.files)));
        o.set("bytes", js::Value::integer(static_cast<std::int64_t>(std::min<std::uint64_t>(p.bytes, 0x7FFFFFFFFFFFFFFFll))));
        o.set("allocated", js::Value::integer(static_cast<std::int64_t>(std::min<std::uint64_t>(p.allocated, 0x7FFFFFFFFFFFFFFFll))));
        o.set("reparse", js::Value::integer(static_cast<std::int64_t>(p.skipped_reparse)));
        o.set("partial", js::Value::boolean(p.partial));
        arr.push(std::move(o));
    }
    root.set("categories", std::move(arr));
    return js::write(root);
}

contracts::Result<std::vector<CleanupPreview>> decode_admin_scan_result(std::string_view payload) {
    using RP = contracts::Result<std::vector<CleanupPreview>>;
    auto doc = js::parse(payload, limits());
    if (!doc.ok()) return RP(doc.error());
    const auto& root = doc.value();
    if (!root.is(js::Value::Type::Object)) return RP::fail(ErrorCode::ParseError, "result object expected");
    auto kind = js::get_string(root, "kind", 16);
    if (!kind.ok() || kind.value() != "scan") return RP::fail(ErrorCode::ParseError, "kind");
    const js::Value* arr = root.find("categories");
    if (!arr || !arr->is(js::Value::Type::Array)) return RP::fail(ErrorCode::MissingValue, "categories");
    if (arr->items().size() > admin_product_categories().size()) return RP::fail(ErrorCode::OutOfRange, "categories");
    std::vector<CleanupPreview> out;
    for (const auto& it : arr->items()) {
        if (!it.is(js::Value::Type::Object)) return RP::fail(ErrorCode::ParseError, "preview object");
        auto id = js::get_string(it, "id", 32);
        auto st = js::get_string(it, "status", 16);
        auto files = js::get_int(it, "files", 0, 0x7FFFFFFFFFFFFFFFll);
        auto bytes = js::get_int(it, "bytes", 0, 0x7FFFFFFFFFFFFFFFll);
        auto alloc = js::get_int(it, "allocated", 0, 0x7FFFFFFFFFFFFFFFll);
        auto rep = js::get_int(it, "reparse", 0, 0x7FFFFFFFFFFFFFFFll);
        auto partial = js::get_bool(it, "partial");
        if (!id.ok()) return RP(id.error());
        if (!st.ok()) return RP(st.error());
        if (!files.ok()) return RP(files.error());
        if (!bytes.ok()) return RP(bytes.error());
        if (!alloc.ok()) return RP(alloc.error());
        if (!rep.ok()) return RP(rep.error());
        if (!partial.ok()) return RP(partial.error());
        const auto c = category_from_id(id.value());
        const auto s = status_from(st.value());
        if (!c || !is_admin_product_category(*c)) return RP::fail(ErrorCode::InvalidEnum, "category id");
        if (!s) return RP::fail(ErrorCode::InvalidEnum, "preview status");
        CleanupPreview p;
        p.category = *c;
        p.status = *s;
        p.files = static_cast<std::size_t>(files.value());
        p.bytes = static_cast<std::uint64_t>(bytes.value());
        p.allocated = static_cast<std::uint64_t>(alloc.value());
        p.skipped_reparse = static_cast<std::size_t>(rep.value());
        p.partial = partial.value();
        p.needs_admin = true;
        out.push_back(p);
    }
    return out;
}

std::string encode_admin_run_request(const AdminRunRequest& r) {
    auto root = js::Value::object();
    root.set("v", js::Value::integer(kAdminTransferVersion));
    auto arr = js::Value::array();
    for (const auto c : r.categories) arr.push(js::Value::string(std::string(to_string(c))));
    root.set("categories", std::move(arr));
    return js::write(root);
}

contracts::Result<AdminRunRequest> decode_admin_run_request(std::string_view payload) {
    using RR = contracts::Result<AdminRunRequest>;
    auto doc = js::parse(payload, limits());
    if (!doc.ok()) return RR(doc.error());
    const auto& root = doc.value();
    if (!root.is(js::Value::Type::Object)) return RR::fail(ErrorCode::ParseError, "request object expected");
    for (const auto& k : root.keys())
        if (k != "v" && k != "categories") return RR::fail(ErrorCode::ParseError, "unknown request field");
    auto v = js::get_int(root, "v", 0, 1000);
    if (!v.ok()) return RR(v.error());
    if (v.value() != kAdminTransferVersion)
        return RR::fail(v.value() > kAdminTransferVersion ? ErrorCode::SchemaTooNew : ErrorCode::InvalidEnum, "version");
    auto cats = parse_categories(root, 2);
    if (!cats.ok()) return RR(cats.error());
    AdminRunRequest r;
    r.categories = std::move(cats).value();
    return r;
}

std::string encode_admin_progress(const AdminProgress& p) {
    auto root = js::Value::object();
    root.set("kind", js::Value::string("progress"));
    root.set("id", js::Value::string(std::string(to_string(p.category))));
    root.set("done", js::Value::integer(static_cast<std::int64_t>(p.done)));
    root.set("total", js::Value::integer(static_cast<std::int64_t>(p.total)));
    return js::write(root);
}

contracts::Result<AdminProgress> decode_admin_progress(std::string_view payload) {
    using RP = contracts::Result<AdminProgress>;
    auto doc = js::parse(payload, limits());
    if (!doc.ok()) return RP(doc.error());
    const auto& root = doc.value();
    auto kind = js::get_string(root, "kind", 16);
    if (!kind.ok() || kind.value() != "progress") return RP::fail(ErrorCode::ParseError, "kind");
    auto id = js::get_string(root, "id", 32);
    auto done = js::get_int(root, "done", 0, 0x7FFFFFFFFFFFFFFFll);
    auto total = js::get_int(root, "total", 0, 0x7FFFFFFFFFFFFFFFll);
    if (!id.ok()) return RP(id.error());
    if (!done.ok()) return RP(done.error());
    if (!total.ok()) return RP(total.error());
    const auto c = category_from_id(id.value());
    if (!c || !is_admin_product_category(*c)) return RP::fail(ErrorCode::InvalidEnum, "category id");
    AdminProgress p;
    p.category = *c;
    p.done = static_cast<std::size_t>(done.value());
    p.total = static_cast<std::size_t>(total.value());
    return p;
}

std::string encode_admin_report(const CleanupReport& r) {
    auto root = js::Value::object();
    root.set("kind", js::Value::string("report"));
    auto cats = js::Value::array();
    for (const auto& c : r.categories) {
        auto o = js::Value::object();
        o.set("id", js::Value::string(std::string(to_string(c.category))));
        set_tally(o, "deleted", c.deleted);
        set_tally(o, "locked", c.locked);
        set_tally(o, "denied", c.denied);
        set_tally(o, "missing", c.missing);
        set_tally(o, "reparse", c.reparse);
        set_tally(o, "changed", c.changed);
        set_tally(o, "other", c.other);
        o.set("dirs", js::Value::integer(static_cast<std::int64_t>(c.dirs_removed)));
        o.set("completed", js::Value::boolean(c.completed));
        cats.push(std::move(o));
    }
    root.set("categories", std::move(cats));
    auto vols = js::Value::array();
    for (const auto& v : r.volumes) {
        auto o = js::Value::object();
        o.set("root", js::Value::string(wroot(v.volume)));
        o.set("before", v.free_before ? js::Value::integer(static_cast<std::int64_t>(*v.free_before)) : js::Value::null());
        o.set("after", v.free_after ? js::Value::integer(static_cast<std::int64_t>(*v.free_after)) : js::Value::null());
        vols.push(std::move(o));
    }
    root.set("volumes", std::move(vols));
    root.set("cancelled", js::Value::boolean(r.cancelled));
    if (!r.gate_reason.empty()) root.set("gate_reason", js::Value::string(r.gate_reason));
    root.set("processed", js::Value::integer(static_cast<std::int64_t>(r.processed)));
    root.set("total", js::Value::integer(static_cast<std::int64_t>(r.total)));
    return js::write(root);
}

contracts::Result<CleanupReport> decode_admin_report(std::string_view payload) {
    using RR = contracts::Result<CleanupReport>;
    auto doc = js::parse(payload, limits());
    if (!doc.ok()) return RR(doc.error());
    const auto& root = doc.value();
    auto kind = js::get_string(root, "kind", 16);
    if (!kind.ok() || kind.value() != "report") return RR::fail(ErrorCode::ParseError, "kind");
    const js::Value* cats = root.find("categories");
    if (!cats || !cats->is(js::Value::Type::Array)) return RR::fail(ErrorCode::MissingValue, "categories");
    if (cats->items().size() > admin_product_categories().size()) return RR::fail(ErrorCode::OutOfRange, "categories");
    CleanupReport rep;
    for (const auto& it : cats->items()) {
        if (!it.is(js::Value::Type::Object)) return RR::fail(ErrorCode::ParseError, "category object");
        auto id = js::get_string(it, "id", 32);
        if (!id.ok()) return RR(id.error());
        const auto c = category_from_id(id.value());
        if (!c || !is_admin_product_category(*c)) return RR::fail(ErrorCode::InvalidEnum, "category id");
        CategoryResult cr;
        cr.category = *c;
        auto d = get_tally(it, "deleted");
        auto l = get_tally(it, "locked");
        auto de = get_tally(it, "denied");
        auto m = get_tally(it, "missing");
        auto rp = get_tally(it, "reparse");
        auto ch = get_tally(it, "changed");
        auto ot = get_tally(it, "other");
        auto dirs = js::get_int(it, "dirs", 0, 0x7FFFFFFFFFFFFFFFll);
        auto comp = js::get_bool(it, "completed");
        if (!d.ok()) return RR(d.error());
        if (!l.ok()) return RR(l.error());
        if (!de.ok()) return RR(de.error());
        if (!m.ok()) return RR(m.error());
        if (!rp.ok()) return RR(rp.error());
        if (!ch.ok()) return RR(ch.error());
        if (!ot.ok()) return RR(ot.error());
        if (!dirs.ok()) return RR(dirs.error());
        if (!comp.ok()) return RR(comp.error());
        cr.deleted = d.value();
        cr.locked = l.value();
        cr.denied = de.value();
        cr.missing = m.value();
        cr.reparse = rp.value();
        cr.changed = ch.value();
        cr.other = ot.value();
        cr.dirs_removed = static_cast<std::size_t>(dirs.value());
        cr.completed = comp.value();
        rep.categories.push_back(std::move(cr));
    }
    const js::Value* vols = root.find("volumes");
    if (!vols || !vols->is(js::Value::Type::Array)) return RR::fail(ErrorCode::MissingValue, "volumes");
    if (vols->items().size() > 32) return RR::fail(ErrorCode::OutOfRange, "volumes");
    for (const auto& it : vols->items()) {
        if (!it.is(js::Value::Type::Object)) return RR::fail(ErrorCode::ParseError, "volume object");
        auto root_s = js::get_string(it, "root", 8);
        if (!root_s.ok()) return RR(root_s.error());
        VolumeSpace vs;
        std::string enc;
        if (!ascii_volume(root_s.value(), enc)) return RR::fail(ErrorCode::ParseError, "volume root");
        vs.volume.assign(enc.begin(), enc.end());
        const js::Value* before = it.find("before");
        const js::Value* after = it.find("after");
        if (before && before->is(js::Value::Type::Int)) vs.free_before = static_cast<std::uint64_t>(before->as_int());
        if (after && after->is(js::Value::Type::Int)) vs.free_after = static_cast<std::uint64_t>(after->as_int());
        rep.volumes.push_back(std::move(vs));
    }
    auto cancelled = js::get_bool(root, "cancelled");
    auto processed = js::get_int(root, "processed", 0, 0x7FFFFFFFFFFFFFFFll);
    auto total = js::get_int(root, "total", 0, 0x7FFFFFFFFFFFFFFFll);
    if (!cancelled.ok()) return RR(cancelled.error());
    if (!processed.ok()) return RR(processed.error());
    if (!total.ok()) return RR(total.error());
    rep.cancelled = cancelled.value();
    if (root.find("gate_reason")) {
        auto gr = js::get_string(root, "gate_reason", 64);
        if (!gr.ok()) return RR(gr.error());
        rep.gate_reason = gr.value();
    }
    rep.processed = static_cast<std::size_t>(processed.value());
    rep.total = static_cast<std::size_t>(total.value());
    return rep;
}

CleanupReport merge_reports(CleanupReport first, const CleanupReport& second) {
    first.categories.insert(first.categories.end(), second.categories.begin(), second.categories.end());
    for (const auto& v : second.volumes) {
        bool merged = false;
        for (auto& own : first.volumes) {
            if (own.volume != v.volume) continue;
            if (!own.free_before && v.free_before) own.free_before = v.free_before;
            if (v.free_after) own.free_after = v.free_after;
            merged = true;
            break;
        }
        if (!merged) first.volumes.push_back(v);
    }
    first.cancelled = first.cancelled || second.cancelled;
    if (!first.standby.ran) first.standby = second.standby;
    if (first.gate_reason.empty()) first.gate_reason = second.gate_reason;
    first.processed += second.processed;
    first.total += second.total;
    return first;
}

}  // namespace ef::core::cleanup
