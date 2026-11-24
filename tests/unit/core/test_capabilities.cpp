#include "core/capabilities/registry.h"
#include "ef_test.h"

using namespace ef::core::capabilities;
using namespace ef::contracts;

namespace {
Capability cap(std::string id, SupportState s, std::string reason = {}, std::string provider = "test") {
    return Capability{std::move(id), "system", s, std::move(provider), "1", std::move(reason), Privilege::User, EvidenceLevel::WindowsIntegration};
}
}  // namespace

EF_TEST(embedded_catalog_loads_all_features) {
    const auto& f = embedded_features();
    EF_CHECK(f.size() == 64);  // ADR-040: W13-W16; B6: G13-G14
    Registry r(f);
    EF_CHECK(r.feature("W01") != nullptr);
    EF_CHECK(r.feature("X10")->group == FeatureGroup::Excluded);
    EF_CHECK(r.feature("B06")->group == FeatureGroup::Startup);
    for (const auto& x : f) EF_CHECK(!x.catalog_release_enabled);  // henuz hicbiri acik degil
}

EF_TEST(unsupported_and_denied_are_distinct) {
    Registry r(embedded_features());
    const std::string env = "env1";
    EF_CHECK(r.record(cap("W06", SupportState::Unsupported, "HAGS not supported by driver"), env).ok());
    Capability d = cap("W02", SupportState::Denied, "power scheme write needs admin");
    d.required = Privilege::Admin;
    EF_CHECK(r.record(d, env).ok());
    EF_CHECK(r.query("W06", "system", env).state == SupportState::Unsupported);
    EF_CHECK(r.query("W02", "system", env).state == SupportState::Denied);
    EF_CHECK(r.query("W02", "system", env).required == Privilege::Admin);
    EF_CHECK(r.query("W03", "system", env).state == SupportState::Unknown);  // sorgulanmadi
    EF_CHECK(r.query("W03", "system", env).reason == "not queried");
    EF_CHECK(!r.record(cap("W99", SupportState::Supported), env).ok());      // katalog disi
    EF_CHECK(!r.record(cap("W01", SupportState::Unknown), env).ok());        // gerekcesiz
}

EF_TEST(new_driver_makes_validation_stale) {
    Registry r(embedded_features());
    const auto env1 = fingerprint({"26300.9711", {"10DE:2C02=32.0.16.2101"}, "0.1.0"});
    const auto env2 = fingerprint({"26300.9711", {"10DE:2C02=32.0.16.2200"}, "0.1.0"});
    EF_CHECK(env1 != env2);
    EF_CHECK(fingerprint({"b", {"x", "y"}, "a"}) == fingerprint({"b", {"y", "x"}, "a"}));  // sira bagimsiz
    EF_CHECK(r.record(cap("G01", SupportState::Supported, "", "NVAPI"), env1).ok());
    EF_CHECK(r.query("G01", "system", env1).state == SupportState::Supported);
    const auto stale = r.query("G01", "system", env2);
    EF_CHECK(stale.state == SupportState::Unknown);
    EF_CHECK(stale.reason.find("stale") != std::string::npos);
    // Acik gecersiz kilma olayi da ayni ortamda bayatlatir
    EF_CHECK(r.record(cap("W01", SupportState::Supported, "", "PowerSetActiveOverlayScheme"), env1).ok());
    EF_CHECK(r.invalidate(InvalidationEvent::DriverChanged) == 1);  // yalniz GPU grubu
    EF_CHECK(r.query("G01", "system", env1).state == SupportState::Unknown);
    EF_CHECK(r.query("W01", "system", env1).state == SupportState::Supported);
    EF_CHECK(r.invalidate(InvalidationEvent::OsUpdated) == 1);
    EF_CHECK(r.query("W01", "system", env1).state == SupportState::Unknown);
}

EF_TEST(untested_feature_never_release_enabled) {
    Registry r(embedded_features());
    EF_CHECK(r.record(cap("W01", SupportState::Supported, "", "api"), "e").ok());
    EF_CHECK(!r.release_enabled("W01"));  // kanit yok
    r.add_evidence({"W01", "win11", SupportState::Supported, EvidenceLevel::WindowsIntegration, "abc", "ctest", "artifacts/evidence/WIN-01.md"});
    EF_CHECK(!r.release_enabled("W01"));  // integration yetmez
    r.add_evidence({"W01", "win11 rtx5080", SupportState::Supported, EvidenceLevel::Hardware, "abc", "manual", "artifacts/evidence/WIN-01.md"});
    EF_CHECK(r.release_enabled("W01"));
    r.add_evidence({"X04", "win11", SupportState::Supported, EvidenceLevel::Hardware, "abc", "manual", "x.md"});
    EF_CHECK(!r.release_enabled("X04"));  // hariç tutulan asla
    EF_CHECK(r.query("X04", "system", "e").state == SupportState::Unsupported);
}

EF_TEST(registry_persistence_round_trip_and_matrix) {
    Registry r(embedded_features());
    EF_CHECK(r.record(cap("W08", SupportState::Supported, "", "ChangeDisplaySettingsEx"), "e1").ok());
    EF_CHECK(r.record(cap("G03", SupportState::Unsupported, "no AMD discrete GPU"), "e1").ok());
    const auto saved = ef::core::json::write(r.save());
    Registry r2(embedded_features());
    auto parsed = ef::core::json::parse(saved);
    EF_CHECK(parsed.ok());
    EF_CHECK(r2.load(parsed.value()).ok());
    EF_CHECK(r2.query("W08", "system", "e1").state == SupportState::Supported);
    EF_CHECK(r2.query("G03", "system", "e1").reason == "no AMD discrete GPU");
    const auto m = r2.support_matrix("e1");
    const auto* feats = m.find("features");
    EF_CHECK(feats && feats->items().size() >= 57);
    for (const auto& row : feats->items()) EF_CHECK(!row.find("release_enabled")->as_bool());
    // Bozuk kayit reddedilir
    auto bad = ef::core::json::parse(R"({"schema_version":1,"entries":[{"feature":"W01","scope":"s","state":"maybe","provider":"","provider_version":"","reason":"x","privilege":"user","evidence":"mock","env":""}]})");
    EF_CHECK(!r2.load(bad.value()).ok());
}

// ---- P9-B8: kayit defteri ile uretici matrisi tutarliligi ----
#include "core/capabilities/vendor_matrix.h"

EF_TEST(vendor_matrix_covers_registry_features_and_never_enables_release) {
    Registry r(embedded_features());
    for (const auto& f : embedded_features()) {
        EF_CHECK(find_vendor_spec(f.id) != nullptr);
        EF_CHECK(!r.release_enabled(f.id));
        // Matris yolu "yazma" olsa bile kayit defteri kanitsiz kapali tutar; "Supported" durumu matristen uretilmez.
        for (const auto v : kMatrixVendors) {
            const auto row = vendor_row(f.id, v);
            EF_CHECK(row.has_value());
            if (row && row->cell.path == VendorPath::Write && f.group != FeatureGroup::Excluded)
                EF_CHECK(r.query(f.id, "gpu:0", "e").state == SupportState::Unknown);  // sorgulanmadi != destekleniyor
        }
    }
}
