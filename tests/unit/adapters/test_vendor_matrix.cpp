// P9-B8: yetenek x uretici destek matrisi. Her ozellik x {NVIDIA, AMD, Intel} icin acik satir; AMD/Intel kimliginde
// uretici-surucu yazma yolu acilmaz (fail-closed); mock gercek cihaz sayilmaz; docs/20_SUPPORT_MATRIX.md tablodan uretilir.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "core/adapters/amd/amd3d.h"
#include "core/adapters/intel/igcl3d.h"
#include "core/adapters/windows/boot_options.h"
#include "core/adapters/windows/tweak_catalog.h"
#include "core/broker/admin.h"
#include "core/capabilities/registry.h"
#include "core/capabilities/vendor_matrix.h"
#include "core/serialization/json.h"
#include "core/vendors/dispatch.h"
#include "ef_test.h"

using namespace ef::core::capabilities;
using namespace ef::core::inventory;
using ef::contracts::EvidenceLevel;
using ef::contracts::Field;
using ef::contracts::Provenance;
using ef::contracts::SupportState;
using ef::core::vendors::Vendor;

namespace {

std::string read_text(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());  // git autocrlf bagimsiz
    return s;
}

GpuAdapter gpu(std::uint32_t idx, std::uint32_t vendor, GpuKind k) {
    GpuAdapter g;
    g.index = idx;
    g.vendor_id = Field<std::uint32_t>::known(vendor, Provenance::Api);
    g.kind = k;
    return g;
}

ef::core::vendors::RuntimeInfo loaded(Vendor v) { return {v, ef::core::vendors::RuntimeState::Loaded, "test", "", ""}; }

// Katalogtaki implementation_status degeri (research/setting_catalog.json, gomulu).
std::string catalog_status(std::string_view id) {
    auto doc = ef::core::json::parse(embedded_catalog_json(), [] {
        ef::core::json::Limits l;
        l.max_bytes = 8u << 20;
        l.max_items = 100000;
        return l;
    }());
    if (!doc.ok()) return {};
    for (const auto& it : doc.value().items()) {
        const auto* i = it.find("id");
        const auto* s = it.find("implementation_status");
        if (i && s && i->as_string() == id) return s->as_string();
    }
    return {};
}

}  // namespace

EF_TEST(vendor_matrix_has_explicit_row_for_every_feature_and_vendor) {
    const auto& features = embedded_features();
    EF_CHECK(features.size() == 64 && kVendorSpecs.size() == features.size());
    std::set<std::string> ids;
    for (const auto& s : kVendorSpecs) EF_CHECK(ids.insert(std::string(s.id)).second);  // tekrar yok
    for (const auto& f : features) {
        EF_CHECK(ids.count(f.id) == 1);  // her katalog ozelligi tabloda
        for (const auto v : kMatrixVendors) {
            const auto row = vendor_row(f.id, v);
            EF_CHECK(row.has_value());
            if (!row) continue;
            // Her satirin gerekce anahtari var ve metin tablosunda tanimli (bos/acik uclu satir yok).
            EF_CHECK(!row->cell.reason.empty() && find_reason(row->cell.reason) != nullptr);
        }
    }
    EF_CHECK(all_vendor_rows().size() == 64 * 3);
    EF_CHECK(!vendor_row("W01", Vendor::None).has_value());  // bilinmeyen kimlik satir almaz
    EF_CHECK(!vendor_row("Z99", Vendor::Nvidia).has_value());
}

EF_TEST(vendor_matrix_amd_and_intel_never_open_a_vendor_driver_write_path) {
    for (const auto& r : all_vendor_rows()) {
        if (r.vendor == Vendor::Nvidia) continue;
        EF_CHECK(!is_vendor_driver_write(r.cell));  // ADLX/IGCL yazma yolu yok
        if (r.cell.channel == WriteChannel::VendorDriver) EF_CHECK(r.cell.path != VendorPath::Write);
    }
    // NVAPI DRS yazma satirlari yalniz bilinen dort ayar; baska hicbir ozellik vendor_driver yazma almaz.
    std::set<std::string> nv_writes;
    for (const auto& r : all_vendor_rows())
        if (is_vendor_driver_write(r.cell)) {
            EF_CHECK(r.vendor == Vendor::Nvidia);
            nv_writes.insert(std::string(r.feature_id));
        }
    EF_CHECK((nv_writes == std::set<std::string>{"G01", "G02", "G05", "G06"}));
}

EF_TEST(vendor_matrix_mock_is_never_real_device_evidence) {
    for (const auto& r : all_vendor_rows()) {
        // Yazma yolunun kanit tavani birim testidir; Hardware/ReleaseArtifact kaniti bu tabloda yazma icin iddia edilemez.
        if (r.cell.path == VendorPath::Write) EF_CHECK(!ef::contracts::is_real_device_evidence(r.cell.evidence));
        if (r.cell.path == VendorPath::Unsupported) EF_CHECK(r.cell.evidence == EvidenceLevel::Mock);
    }
    // Gercek cihaz okuma kaniti yalniz NVIDIA telemetri (GPU-02: nvidia-smi ile karsilastirildi); yazma degil.
    const auto h06 = vendor_row("H06", Vendor::Nvidia);
    EF_CHECK(h06 && h06->cell.path == VendorPath::ReadOnly && h06->cell.evidence == EvidenceLevel::Hardware);
    // Hicbir satir release kapisini acmaz: kayit defteri bos kanitla hicbir ozelligi acmaz.
    Registry reg(embedded_features());
    for (const auto& f : embedded_features()) EF_CHECK(!reg.release_enabled(f.id));
}

EF_TEST(vendor_matrix_follows_catalog_status_and_policy) {
    for (const auto& s : kVendorSpecs) {
        const auto status = catalog_status(s.id);
        EF_CHECK(!status.empty());
        for (const auto v : kMatrixVendors) {
            const auto& c = *vendor_cell(s, v);
            const bool writes = c.path == VendorPath::Write;
            // H05 (WUA surucu kurulumu, ADR-031) katalogda not_started kalmis ama kodda var: tek acik istisna.
            const bool allowed_status = status == "implemented" || status == "guided" || status == "partial" || s.id == "H05";
            if (writes) EF_CHECK(allowed_status);
            if (status == "excluded") EF_CHECK(c.path == VendorPath::Unsupported && c.reason == matrix_reason::kExcluded);
            if (status == "unsupported") EF_CHECK(c.path == VendorPath::Unsupported);
            if (status == "blocked_license" && v == Vendor::Amd) EF_CHECK(c.reason == matrix_reason::kAmdLicense);
        }
    }
    // Disarida tutulan grup kayit defteri sorgusuyla da tutarli.
    Registry reg(embedded_features());
    for (const auto& f : embedded_features())
        if (f.group == FeatureGroup::Excluded) {
            EF_CHECK(reg.query(f.id, "system", "e").state == SupportState::Unsupported);
            for (const auto v : kMatrixVendors) EF_CHECK(vendor_row(f.id, v)->cell.path == VendorPath::Unsupported);
        }
}

EF_TEST(vendor_matrix_intel_and_amd_gpu_rows_state_why) {
    // AMD: lisans bekliyor (B-006); Intel: baglayici + cihaz yok (B-004). Gerekce anahtari sabit.
    EF_CHECK(vendor_row("G03", Vendor::Amd)->cell.reason == matrix_reason::kAmdLicense);
    EF_CHECK(vendor_row("G08", Vendor::Amd)->cell.reason == matrix_reason::kAmdLicense);
    EF_CHECK(vendor_row("G09", Vendor::Amd)->cell.reason == matrix_reason::kAmdLicense);
    EF_CHECK(vendor_row("G04", Vendor::Intel)->cell.reason == matrix_reason::kIntelNoDevice);
    EF_CHECK(vendor_row("H06", Vendor::Amd)->cell.path == VendorPath::Unsupported);
    EF_CHECK(vendor_row("H06", Vendor::Intel)->cell.path == VendorPath::Unsupported);
    // NVIDIA'ya ozgu ayar baska uretici kimliginde "baska uretici" gerekcesiyle desteklenmiyor.
    EF_CHECK(vendor_row("G01", Vendor::Amd)->cell.reason == matrix_reason::kOtherVendor);
    EF_CHECK(vendor_row("G02", Vendor::Intel)->cell.reason == matrix_reason::kOtherVendor);
    // Shader onbellegi (G07) NVIDIA'da yazma yolu kodda ama kapali: "yalniz okuma".
    const auto g07 = vendor_row("G07", Vendor::Nvidia);
    EF_CHECK(g07->cell.path == VendorPath::ReadOnly && g07->cell.reason == matrix_reason::kWriteGatedOff);
    // CPU/RAM/BIOS: voltaj/fan/OC yazimi hicbir uretici kimliginde yok.
    for (const auto v : kMatrixVendors) {
        EF_CHECK(vendor_row("H01", v)->cell.path == VendorPath::Unsupported);
        EF_CHECK(vendor_row("G11", v)->cell.path == VendorPath::Unsupported);
        EF_CHECK(vendor_row("H02", v)->cell.path == VendorPath::ReadOnly);
        EF_CHECK(vendor_row("G12", v)->cell.path == VendorPath::ReadOnly);
    }
}

EF_TEST(vendor_matrix_mock_adapters_do_not_open_amd_or_intel_write_paths) {
    using namespace ef::core::adapters;
    // AMD mock Supported + Mock kanit: kayit defteri "Supported" gorur ama release kapisi kapali, matris AMD satiri Yok.
    amd::FakeAmd3d fa;
    fa.supported[static_cast<int>(amd::Amd3dFeature::AntiLag)] = true;
    amd::Amd3dAdapter aa(fa);
    const auto acap = aa.capability({0, amd::Amd3dFeature::AntiLag}, "mock");
    EF_CHECK(acap.state == SupportState::Supported && acap.evidence == EvidenceLevel::Mock);
    intel::FakeIgcl3d fi;
    fi.supported[static_cast<int>(intel::Igcl3dFeature::LowLatency)] = true;
    intel::Igcl3dAdapter ia(fi);
    const auto icap = ia.capability({0, intel::Igcl3dFeature::LowLatency}, "mock");
    EF_CHECK(icap.state == SupportState::Supported && icap.evidence == EvidenceLevel::Mock);

    Registry reg(embedded_features());
    EF_CHECK(reg.record(acap, "e").ok());
    EF_CHECK(!reg.release_enabled("G03"));
    reg.add_evidence({"G03", "mock", SupportState::Supported, EvidenceLevel::Mock, "x", "unit", "x.md"});
    EF_CHECK(!reg.release_enabled("G03"));  // mock kanit gercek cihaz sayilmaz

    // Runtime yuklu olsa da kapi kapali (AMD ve Intel, tum ozellikler).
    const std::vector<GpuAdapter> gpus{gpu(0, kVendorAmd, GpuKind::Discrete), gpu(1, kVendorIntel, GpuKind::Integrated)};
    const auto routes = ef::core::vendors::route_adapters(gpus, {loaded(Vendor::Amd), loaded(Vendor::Intel)});
    EF_CHECK(routes.size() == 2 && routes[0].support == SupportState::Supported && routes[1].support == SupportState::Supported);
    for (const auto& rt : routes)
        for (const auto& f : embedded_features()) EF_CHECK(!ef::core::vendors::vendor_driver_write_allowed(rt, f.id));
}

EF_TEST(vendor_matrix_amd_intel_sources_stay_unwired_from_product_code) {
    // Urun kodunda ADLX/IGCL adaptorleri baglanmadi: baglanirsa bu test bilincli olarak guncellenmeli (matris + kapi ile birlikte).
    namespace fs = std::filesystem;
    const fs::path root = fs::path(EF_SOURCE_ROOT) / "src";
    std::size_t scanned = 0;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file()) continue;
        const auto ext = e.path().extension().string();
        if (ext != ".cpp" && ext != ".h") continue;
        const auto rel = fs::relative(e.path(), root).generic_string();
        if (rel.rfind("core/adapters/amd/", 0) == 0 || rel.rfind("core/adapters/intel/", 0) == 0) continue;
        const std::string text = read_text(e.path());
        ++scanned;
        // Satir bazli: baslik ekleme (#include) tarar; yorum satirlari sayilmaz.
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) {
            if (line.rfind("#include", 0) != 0) continue;
            EF_CHECK(line.find("adapters/amd/amd3d.h") == std::string::npos);
            EF_CHECK(line.find("adapters/intel/igcl3d.h") == std::string::npos);
            EF_CHECK(line.find("ADLXHelper") == std::string::npos);
            EF_CHECK(line.find("ctl_api.h") == std::string::npos);
        }
    }
    EF_CHECK(scanned > 100);
}

EF_TEST(vendor_matrix_reason_texts_are_honest_and_complete) {
    for (const auto& r : kReasonTexts) {
        EF_CHECK(!r.tr.empty() && !r.en.empty() && !r.code.empty());
        EF_CHECK(r.key.rfind("hw.matrix.", 0) == 0);
        // Kanitsiz "destekleniyor" iddiasi yok.
        EF_CHECK(r.tr.find("destekleniyor") == std::string_view::npos);
        EF_CHECK(r.en.find("is supported") == std::string_view::npos && r.en.find("fully supported") == std::string_view::npos);
    }
    std::set<std::string_view> keys, codes;
    for (const auto& r : kReasonTexts) {
        EF_CHECK(keys.insert(r.key).second);
        EF_CHECK(codes.insert(r.code).second);
    }
    // Her tablo hucresi bu listeden bir anahtar kullanir; listedeki her anahtar bir yerde kullanilir (olu anahtar yok).
    std::set<std::string_view> used;
    for (const auto& r : all_vendor_rows()) used.insert(r.cell.reason);
    for (const auto& r : kReasonTexts) EF_CHECK(used.count(r.key) == 1);
}

EF_TEST(vendor_matrix_pending_strings_match_reason_table_when_present) {
    // B7 bekleyen dosyayi isleyince dosya tuketilir; bulunuyorsa tablo ile birebir olmali.
    const auto p = std::filesystem::path(EF_SOURCE_ROOT) / "design" / "strings" / "pending" / "p9-hardware-honesty.json";
    if (!std::filesystem::exists(p)) return;
    auto doc = ef::core::json::parse(read_text(p));
    EF_CHECK(doc.ok());
    if (!doc.ok()) return;
    EF_CHECK(doc.value().keys().size() == kReasonTexts.size());
    for (const auto& r : kReasonTexts) {
        const auto* v = doc.value().find(r.key);
        EF_CHECK(v && v->items().size() == 2);
        if (!v || v->items().size() != 2) continue;
        EF_CHECK(v->items()[0].as_string() == r.tr);
        EF_CHECK(v->items()[1].as_string() == r.en);
    }
}

EF_TEST(vendor_matrix_markdown_doc_is_generated_from_the_table) {
    const auto md = vendor_matrix_markdown(embedded_features());
    EF_CHECK(md.find("| W01 |") != std::string::npos && md.find("| X13 |") != std::string::npos);
    EF_CHECK(md.find("TABLO YOK") == std::string::npos);
    // 64 ozellik satiri (ADR-040: W13-W16; B6: G13-G14)
    std::size_t rows = 0;
    for (std::size_t p = md.find("\n| "); p != std::string::npos; p = md.find("\n| ", p + 1)) ++rows;
    EF_CHECK(rows >= 64);
    const auto doc = std::filesystem::path(EF_SOURCE_ROOT) / "docs" / "20_SUPPORT_MATRIX.md";
    EF_CHECK(std::filesystem::exists(doc));
    EF_CHECK(read_text(doc) == md);  // elle duzenlenmis/bayat belge kirmizi
}

EF_TEST(vendor_matrix_json_is_deterministic_and_never_says_supported) {
    const auto a = ef::core::json::write(vendor_matrix_json());
    const auto b = ef::core::json::write(vendor_matrix_json());
    EF_CHECK(a == b);
    EF_CHECK(a.find("\"supported\"") == std::string::npos);  // durum degeri degil yol: write/read_only/unsupported
    EF_CHECK(a.find("release_enabled") == std::string::npos);
    auto parsed = ef::core::json::parse(a, [] {
        ef::core::json::Limits l;
        l.max_bytes = 4u << 20;
        l.max_items = 100000;
        return l;
    }());
    EF_CHECK(parsed.ok());
    if (parsed.ok()) EF_CHECK(parsed.value().find("rows")->items().size() == 64 * 3);
}

EF_TEST(vendor_matrix_adr040_rows_follow_real_state) {
    // B9 (ADR-040 ek not): W13-W16 satirlari gercek durumu soyler; mock/birim kaniti gercek cihaz kaniti degildir.
    // W13 USB: kodda yazma yolu var, bayrak yok (varsayilan Hayir / Gelismis) -> Yazma [A1] (yalniz birim kaniti).
    EF_CHECK(catalog_status("W13") == "implemented");
    // W14 (bayrakla kapali), W16 (bayrakla kapali + BCD okuma yolu dogrulanmadi): plana girmez -> Desteklenmiyor [A2].
    EF_CHECK(catalog_status("W14") == "implemented" && catalog_status("W16") == "partial");
    // W15: resmi kaynak yok, katalog girdisi YOK -> Desteklenmiyor [E1].
    EF_CHECK(catalog_status("W15") == "not_started");
    for (const auto v : kMatrixVendors) {
        const auto w13 = vendor_row("W13", v);
        EF_CHECK(w13 && w13->cell.path == VendorPath::Write && w13->cell.channel == WriteChannel::Windows);
        EF_CHECK(w13 && !ef::contracts::is_real_device_evidence(w13->cell.evidence));
        for (const auto* id : {"W14", "W16"}) {
            const auto r = vendor_row(id, v);
            EF_CHECK(r && r->cell.path == VendorPath::Unsupported && r->cell.reason == matrix_reason::kWriteGatedOff);
        }
        const auto w15 = vendor_row("W15", v);
        EF_CHECK(w15 && w15->cell.path == VendorPath::Unsupported && w15->cell.reason == matrix_reason::kNotImplemented);
    }
    // Bayrak ile satir birlikte degisir: W14/W16 bayragi acilirsa bu test bilincli olarak guncellenmeli.
    static_assert(!ef::core::adapters::windows::kPrioritySeparationItemEnabled && !ef::core::adapters::windows::kBootOptionItemEnabled);
}

EF_TEST(vendor_matrix_rows_complete) {
    // B6 (ADR-040 m.8): G13 is parcacikli iyilestirme, G14 FXAA. NVIDIA: DRS yazma yolu kodda ama bayrakla kapali ->
    // "yalniz okuma (kapali)" [A2]; bayrak acilirsa satir ayni degisiklikte drs_write olmali. AMD/Intel: desteklenmiyor.
    const std::pair<const char*, bool> rows[] = {{"G13", ef::core::broker::kThreadedOptimizationItemEnabled},
                                                 {"G14", ef::core::broker::kFxaaOffItemEnabled}};
    for (const auto& [id, enabled] : rows) {
        EF_CHECK(catalog_status(id) == "implemented");
        const auto nv = vendor_row(id, Vendor::Nvidia);
        EF_CHECK(nv.has_value());
        if (!nv) continue;
        EF_CHECK(nv->cell.channel == WriteChannel::VendorDriver);
        if (enabled) EF_CHECK(nv->cell.path == VendorPath::Write);
        else EF_CHECK(nv->cell.path == VendorPath::ReadOnly && nv->cell.reason == matrix_reason::kWriteGatedOff);
        for (const auto v : {Vendor::Amd, Vendor::Intel}) {
            const auto r = vendor_row(id, v);
            EF_CHECK(r && r->cell.path == VendorPath::Unsupported && r->cell.reason == matrix_reason::kOtherVendor);
        }
    }
    // Uretilen belge bayat degil ve yeni satirlari icerir.
    const auto md = vendor_matrix_markdown(embedded_features());
    EF_CHECK(md.find("| G13 |") != std::string::npos && md.find("| G14 |") != std::string::npos);
}