#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

#include "core/capabilities/vendor_matrix.h"
#include "core/games/vendor_guide.h"
#include "core/i18n/catalog.h"
#include "ef_test.h"

using namespace ef::core::games;
namespace vendors = ef::core::vendors;
namespace caps = ef::core::capabilities;
namespace i18n = ef::core::i18n;

EF_TEST(vendor_guide_has_nvidia_and_amd_sections) {
    const auto s = vendor_guide_sections();
    EF_CHECK(s.size() == 2);
    EF_CHECK(s[0].title_key == "vendor.nvidia.title");
    EF_CHECK(s[0].point_keys.size() == 3);
    EF_CHECK(s[1].title_key == "vendor.amd.title");
    EF_CHECK(s[1].point_keys.size() == 4);
}

EF_TEST(vendor_guide_keys_are_stable_and_prefixed) {
    for (const auto& sec : vendor_guide_sections()) {
        EF_CHECK(sec.title_key.rfind("vendor.", 0) == 0);
        EF_CHECK(!sec.point_keys.empty());
        for (const auto& k : sec.point_keys) EF_CHECK(k.rfind("vendor.", 0) == 0);
    }
}

namespace {
LowLatencyProbe probe(vendors::Vendor v, vendors::RuntimeState rt, std::optional<bool> feature) {
    LowLatencyProbe p;
    p.vendor = v;
    p.runtime = rt;
    p.feature_reported = feature;
    return p;
}
constexpr auto kVendors = {vendors::Vendor::Amd, vendors::Vendor::Intel};
constexpr auto kRuntimes = {vendors::RuntimeState::Loaded, vendors::RuntimeState::Missing, vendors::RuntimeState::Untrusted,
                            vendors::RuntimeState::LoadFailed};
bool key_in_catalog(const i18n::Catalog& c, const std::string& key) {
    const auto id = i18n::id_of(key);
    return id && !c.get(*id).empty();
}
}  // namespace

// B7: "Desteklenmiyor" (runtime yok / ureticinin kendisi yok diyor) ile "Veri yetersiz" (runtime var, durum okunamadi) AYRI.
EF_TEST(amd_guide_unsupported_vs_insufficient) {
    const auto none = low_latency_guide(probe(vendors::Vendor::Amd, vendors::RuntimeState::Missing, std::nullopt));
    const auto unknown = low_latency_guide(probe(vendors::Vendor::Amd, vendors::RuntimeState::Loaded, std::nullopt));
    EF_CHECK(none && unknown);
    if (!none || !unknown) return;
    EF_CHECK(none->status == LowLatencyStatus::Unsupported);
    EF_CHECK(unknown->status == LowLatencyStatus::InsufficientData);
    EF_CHECK(none->status_key == "vendor.ll.status.unsupported");
    EF_CHECK(unknown->status_key == "vendor.ll.status.insufficient");
    EF_CHECK(none->status_key != unknown->status_key);
    // Bilinmeyen surum/yetenek okunamaz: veri yetersiz; "Desteklenmiyor" uydurulmaz.
    EF_CHECK(unknown->matrix_reason_key.rfind("hw.matrix.", 0) == 0);
}

EF_TEST(guide_unsupported_when_runtime_not_usable_for_every_state) {
    for (const auto v : kVendors)
        for (const auto rt : {vendors::RuntimeState::Missing, vendors::RuntimeState::Untrusted, vendors::RuntimeState::LoadFailed})
            for (const auto f : {std::optional<bool>{}, std::optional<bool>{true}, std::optional<bool>{false}}) {
                const auto g = low_latency_guide(probe(v, rt, f));
                EF_CHECK(g.has_value());
                if (!g) continue;
                EF_CHECK(g->status == LowLatencyStatus::Unsupported);
                EF_CHECK(g->mechanisms.empty());  // yoksa rehber de yok
            }
}

EF_TEST(intel_guide_runtime_loaded_feature_false_is_unsupported_and_unknown_is_insufficient) {
    const auto no = low_latency_guide(probe(vendors::Vendor::Intel, vendors::RuntimeState::Loaded, false));
    const auto unk = low_latency_guide(probe(vendors::Vendor::Intel, vendors::RuntimeState::Loaded, std::nullopt));
    const auto yes = low_latency_guide(probe(vendors::Vendor::Intel, vendors::RuntimeState::Loaded, true));
    EF_CHECK(no && unk && yes);
    if (!no || !unk || !yes) return;
    EF_CHECK(no->status == LowLatencyStatus::Unsupported);
    EF_CHECK(unk->status == LowLatencyStatus::InsufficientData);
    EF_CHECK(yes->status == LowLatencyStatus::GuideOnly);
    EF_CHECK(no->status_key != unk->status_key && unk->status_key != yes->status_key && no->status_key != yes->status_key);
}

// Intel icin yazilabilir API yok (IGCL bagli degil, cihaz yok): matris Unsupported; rehber yazma sunmaz.
EF_TEST(vendor_guide_never_offers_write) {
    for (const auto v : kVendors)
        for (const auto rt : kRuntimes)
            for (const auto f : {std::optional<bool>{}, std::optional<bool>{true}, std::optional<bool>{false}}) {
                const auto g = low_latency_guide(probe(v, rt, f));
                EF_CHECK(g.has_value());
                if (!g) continue;
                EF_CHECK(g->vendor == v);
                EF_CHECK(!g->offers_write);  // islem/Duzelt yok
                // Matris tek kaynak: bu uretici x surucu dusuk gecikme ozelligi yazma yolu degil.
                const auto row = caps::vendor_row(v == vendors::Vendor::Amd ? "G03" : "G04", v);
                EF_CHECK(row.has_value());
                if (row) {
                    EF_CHECK(row->cell.path != caps::VendorPath::Write);
                    EF_CHECK(row->cell.channel != caps::WriteChannel::VendorDriver);
                    EF_CHECK(g->matrix_reason_key == row->cell.reason);
                }
            }
}

// Farkli mekanizmalar tek ad altinda birlestirilmez: surucu duzeyi ve oyun ici SDK ayri girdi, ayri anahtar.
EF_TEST(guide_keeps_driver_level_and_in_game_sdk_separate) {
    for (const auto v : kVendors) {
        const auto g = low_latency_guide(probe(v, vendors::RuntimeState::Loaded, true));
        EF_CHECK(g.has_value());
        if (!g) continue;
        EF_CHECK(g->mechanisms.size() == 2);
        if (g->mechanisms.size() != 2) continue;
        EF_CHECK(g->mechanisms[0].kind == LowLatencyKind::DriverPanel);
        EF_CHECK(g->mechanisms[1].kind == LowLatencyKind::InGameSdk);
        EF_CHECK(g->mechanisms[0].text_key != g->mechanisms[1].text_key);
        const std::string pre = v == vendors::Vendor::Amd ? "vendor.ll.amd." : "vendor.ll.intel.";
        EF_CHECK(g->mechanisms[0].text_key.rfind(pre, 0) == 0);
        EF_CHECK(g->mechanisms[1].text_key.rfind(pre, 0) == 0);
    }
}

// NVIDIA bu dalda dokunulmaz: yeni rehber NVIDIA/bilinmeyen kimlik icin girdi uretmez; NVIDIA bolumu aynen kalir.
EF_TEST(guide_does_not_touch_nvidia_or_unknown_vendor) {
    for (const auto rt : kRuntimes) {
        EF_CHECK(!low_latency_guide(probe(vendors::Vendor::Nvidia, rt, true)).has_value());
        EF_CHECK(!low_latency_guide(probe(vendors::Vendor::None, rt, std::nullopt)).has_value());
    }
    const auto s = vendor_guide_sections();
    EF_CHECK(s.size() == 2 && s[0].title_key == "vendor.nvidia.title");
    EF_CHECK(s[0].point_keys.size() == 3);
    EF_CHECK(s[0].point_keys[0] == "vendor.nvidia.sysmem");
    EF_CHECK(s[0].point_keys[1] == "vendor.nvidia.clean_install");
    EF_CHECK(s[0].point_keys[2] == "vendor.nvidia.overlay");
}

// Metinlerde yuzde, FPS, rakam (surum/kazanc) iddiasi yok; her anahtar TR+EN katalogda var.
EF_TEST(vendor_guide_strings_have_no_digits_claims) {
    for (auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        const auto cat = i18n::Catalog::embedded(loc);
        EF_CHECK(cat.ok());
        if (!cat.ok()) continue;
        for (const auto v : kVendors)
            for (const auto f : {std::optional<bool>{}, std::optional<bool>{true}, std::optional<bool>{false}})
                for (const auto rt : kRuntimes) {
                    const auto g = low_latency_guide(probe(v, rt, f));
                    EF_CHECK(g.has_value());
                    if (!g) continue;
                    std::vector<std::string> keys{g->status_key, g->matrix_reason_key};
                    if (!g->mechanisms.empty()) keys.push_back(g->footnote_key);
                    for (const auto& m : g->mechanisms) keys.push_back(m.text_key);
                    for (const auto& k : keys) {
                        EF_CHECK(!k.empty());
                        EF_CHECK(key_in_catalog(cat.value(), k));
                        const auto id = i18n::id_of(k);
                        if (!id) continue;
                        const std::string t(cat.value().get(*id));
                        EF_CHECK(t.find('%') == std::string::npos);
                        std::string low = t;
                        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        EF_CHECK(low.find("fps") == std::string::npos);
                        // Tek istisna: urun adi "Anti-Lag 2" (surum/kazanc degil); onun disinda rakam yok.
                        std::string rest = t;
                        for (std::size_t p; (p = rest.find("Anti-Lag 2")) != std::string::npos;) rest.erase(p, 10);
                        EF_CHECK(std::none_of(rest.begin(), rest.end(), [](unsigned char c) { return std::isdigit(c) != 0; }));
                    }
                }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// B-012: rehberin arayuze baglanmasi (envanter -> girdi -> kart satirlari).
namespace {
namespace inv = ef::core::inventory;
inv::GpuAdapter adapter(std::uint32_t index, std::uint32_t pci, inv::GpuKind kind) {
    inv::GpuAdapter a;
    a.index = index;
    a.vendor_id.value = pci;
    a.kind = kind;
    return a;
}
vendors::RuntimeInfo runtime(vendors::Vendor v, vendors::RuntimeState s) {
    vendors::RuntimeInfo r;
    r.vendor = v;
    r.state = s;
    return r;
}
}  // namespace

EF_TEST(ll_probes_skip_nvidia_software_and_dedupe_vendor) {
    const std::vector<inv::GpuAdapter> gpus{adapter(0, inv::kVendorNvidia, inv::GpuKind::Discrete),
                                            adapter(1, inv::kVendorAmd, inv::GpuKind::Integrated),
                                            adapter(2, inv::kVendorAmd, inv::GpuKind::Discrete),
                                            adapter(3, inv::kVendorIntel, inv::GpuKind::Software),
                                            adapter(4, 0x1234, inv::GpuKind::Discrete)};
    const auto p = low_latency_probes(gpus, {runtime(vendors::Vendor::Amd, vendors::RuntimeState::Loaded)});
    EF_CHECK(p.size() == 1);  // NVIDIA yok, ikinci AMD tekrar edilmez, yazilim Intel ve bilinmeyen uretici atlanir
    if (p.size() != 1) return;
    EF_CHECK(p[0].vendor == vendors::Vendor::Amd);
    EF_CHECK(p[0].runtime == vendors::RuntimeState::Loaded);
    EF_CHECK(!p[0].feature_reported.has_value());  // ozellik durumu uydurulmaz
}

EF_TEST(ll_probes_missing_runtime_when_not_observed) {
    const std::vector<inv::GpuAdapter> gpus{adapter(0, inv::kVendorIntel, inv::GpuKind::Integrated)};
    const auto p = low_latency_probes(gpus, {});
    EF_CHECK(p.size() == 1 && p[0].vendor == vendors::Vendor::Intel && p[0].runtime == vendors::RuntimeState::Missing);
}

EF_TEST(ll_card_lines_empty_for_nvidia_only_system) {
    const std::vector<inv::GpuAdapter> gpus{adapter(0, inv::kVendorNvidia, inv::GpuKind::Discrete)};
    EF_CHECK(low_latency_card_lines(low_latency_probes(gpus, {})).empty());
    EF_CHECK(low_latency_card_lines({probe(vendors::Vendor::Nvidia, vendors::RuntimeState::Loaded, true)}).empty());
}

EF_TEST(ll_card_lines_separate_unsupported_and_insufficient_and_resolve_in_catalog) {
    const auto lines = low_latency_card_lines({probe(vendors::Vendor::Amd, vendors::RuntimeState::Loaded, std::nullopt),
                                               probe(vendors::Vendor::Intel, vendors::RuntimeState::Missing, std::nullopt)});
    const auto has = [&](const std::string& k) {
        return std::any_of(lines.begin(), lines.end(), [&](const LowLatencyLine& l) { return l.key == k; });
    };
    EF_CHECK(has("vendor.ll.amd.title") && has("vendor.ll.intel.title"));
    EF_CHECK(has("vendor.ll.status.insufficient"));  // AMD: runtime yuklu, durum okunamadi
    EF_CHECK(has("vendor.ll.status.unsupported"));   // Intel: runtime yok
    EF_CHECK(has("vendor.ll.amd.driver") && has("vendor.ll.amd.ingame"));
    EF_CHECK(!has("vendor.ll.intel.driver") && !has("vendor.ll.intel.ingame"));  // desteklenmeyende rehber maddesi yok
    std::size_t headings = 0;
    for (const auto& l : lines) {
        headings += l.heading ? 1 : 0;
        EF_CHECK(l.key.rfind("vendor.", 0) == 0 || l.key.rfind("hw.matrix.", 0) == 0);  // islem/dugme anahtari yok
        EF_CHECK(!(l.heading && l.secondary));
    }
    EF_CHECK(headings == 2);
    for (auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        const auto cat = i18n::Catalog::embedded(loc);
        EF_CHECK(cat.ok());
        if (!cat.ok()) continue;
        for (const auto& l : lines) EF_CHECK(key_in_catalog(cat.value(), l.key));
    }
}
