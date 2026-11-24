#include "core/maintenance/standby_purge.h"

#include <charconv>

namespace ef::core::maintenance {

PurgeRun run_standby_purge(MemoryListApi& api) {
    PurgeRun r;
    auto before = api.standby_bytes();
    if (before.ok()) r.before_bytes = before.value();
    const auto st = api.purge_standby();
    if (!st.ok()) {
        r.ok = false;
        r.error_key = "purge";
        r.before_bytes.reset();  // basarisiz islemde sayi gosterilmez
        return r;
    }
    r.ok = true;
    auto after = api.standby_bytes();
    if (after.ok()) r.after_bytes = after.value();
    if (!r.before_bytes || !r.after_bytes) r.before_bytes.reset(), r.after_bytes.reset();  // yarim sayi yok
    return r;
}

std::string encode_purge_detail(const PurgeRun& r) {
    if (!r.ok) return "mj:failed:purge";
    const auto num = [](const std::optional<std::uint64_t>& v) { return v ? std::to_string(*v) : std::string("?"); };
    return "mj:purged:b=" + num(r.before_bytes) + ":a=" + num(r.after_bytes);
}

namespace {
std::optional<std::uint64_t> take_num(std::string_view s, std::string_view key) {
    const auto p = s.find(key);
    if (p == std::string_view::npos) return std::nullopt;
    auto v = s.substr(p + key.size());
    v = v.substr(0, v.find(':'));
    if (v.empty() || v == "?") return std::nullopt;
    std::uint64_t n = 0;
    const auto res = std::from_chars(v.data(), v.data() + v.size(), n);
    if (res.ec != std::errc() || res.ptr != v.data() + v.size()) return std::nullopt;
    return n;
}
}  // namespace

PurgeRun parse_purge_detail(std::string_view d) {
    PurgeRun r;
    if (d.rfind("mj:purged:", 0) != 0) {
        r.error_key = "purge";
        return r;
    }
    r.ok = true;
    r.before_bytes = take_num(d, "b=");
    r.after_bytes = take_num(d, "a=");
    if (!r.before_bytes || !r.after_bytes) r.before_bytes.reset(), r.after_bytes.reset();
    return r;
}

}  // namespace ef::core::maintenance
