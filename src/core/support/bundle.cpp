#include "core/support/bundle.h"

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "core/serialization/json.h"
#include "core/support/zip_writer.h"

namespace ef::core::support {

namespace {
std::string u8name(const std::filesystem::path& p) {
    const auto u = p.filename().u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::string read_file(const std::filesystem::path& p, std::size_t max) {
    std::ifstream f(p, std::ios::binary);
    std::string s;
    if (!f) return s;
    s.resize(max);
    f.read(s.data(), static_cast<std::streamsize>(max));
    s.resize(static_cast<std::size_t>(f.gcount()));
    return s;
}

std::string redact_lines(const std::string& text, const logging::RedactionContext& ctx) {
    std::string out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        out += logging::redact(line, ctx);
        out += '\n';
    }
    return out;
}
}  // namespace

BundleRequest make_bundle_request(const std::filesystem::path& app_data_dir, bool include_dumps, std::string app_version,
                                  std::string os_build) {
    BundleRequest req;
    req.logs_dir = app_data_dir / L"logs";
    req.crash_dir = app_data_dir / L"crash";
    req.include_dumps = include_dumps;  // varsayilan kapali; yalniz acik onayla true
    req.app_version = std::move(app_version);
    req.os_build = std::move(os_build);
    return req;
}

contracts::Result<BundleResult> build_bundle(const BundleRequest& req, const logging::RedactionContext& ctx) {
    ZipWriter zip;
    BundleResult res;
    std::size_t budget = req.max_log_bytes;
    std::error_code ec;

    for (const auto& e : std::filesystem::directory_iterator(req.logs_dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != L".log") continue;
        const std::string raw = read_file(e.path(), budget);
        budget -= raw.size();
        // Dosya adi da redakte edilir.
        const std::string name = "logs/" + logging::redact(u8name(e.path()), ctx);
        if (zip.add(name, redact_lines(raw, ctx))) res.entries.push_back(name);
        if (budget == 0) break;
    }

    if (req.include_dumps) {
        for (const auto& e : std::filesystem::directory_iterator(req.crash_dir, ec)) {
            if (!e.is_regular_file(ec) || e.path().extension() != L".dmp") continue;
            const std::string name = "crash/" + u8name(e.path());
            if (zip.add(name, read_file(e.path(), 8u << 20))) res.entries.push_back(name);
        }
    }

    if (!req.diag_json.empty() && zip.add("diag.json", redact_lines(req.diag_json, ctx))) res.entries.push_back("diag.json");

    for (const auto& [name, content] : req.extra_files)
        if (!name.empty() && !content.empty() && zip.add(name, redact_lines(content, ctx))) res.entries.push_back(name);

    json::Value m = json::Value::object();
    m.set("schema_version", json::Value::integer(1));
    m.set("app_version", json::Value::string(req.app_version));
    m.set("os_build", json::Value::string(logging::redact(req.os_build, ctx)));
    m.set("includes_dumps", json::Value::boolean(req.include_dumps));
    m.set("redacted", json::Value::boolean(true));
    json::Value list = json::Value::array();
    for (const auto& n : res.entries) list.push(json::Value::string(n));
    m.set("entries", std::move(list));
    if (!zip.add("manifest.json", json::write(m))) return contracts::Result<BundleResult>::fail(contracts::ErrorCode::IoError);
    res.entries.push_back("manifest.json");
    res.zip_bytes = zip.finish();
    return res;
}

}  // namespace ef::core::support
