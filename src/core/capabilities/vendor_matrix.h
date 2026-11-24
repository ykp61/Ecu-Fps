#pragma once
// P9-B8: yetenek x uretici destek matrisi. Her katalog ozelligi icin NVIDIA / AMD / Intel satiri ACIK yazilir:
//   Write       : kodda bu uretici kimliginde calisan bir yazma yolu var (birim/mock testli; gercek cihaz yazma kaniti
//                 AYRI bir SupportEvidence kaydidir ve burada iddia edilmez)
//   ReadOnly    : yalniz okuma/tespit/rehber; yazma yolu yok veya kapali
//   Unsupported : bu uretici kimliginde yol yok (gerekce anahtari zorunlu)
// Degismezler (AGENTS.md): AMD/Intel kimliginde uretici-surucu (ADLX/IGCL) yazma yolu ACIK DEGILDIR (fail-closed);
// mock gercek cihaz sayilmaz (Write satirinin kanit seviyesi Unit'tir, Hardware olamaz); "yonetici hakki" API yoklugunu
// gidermez. Tablo derleme zamaninda sabittir; kod yeni bir yolu acarsa bu tablo ve testi birlikte degisir.
// Header-only: src/core/CMakeLists.txt degismez.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/support.h"
#include "core/capabilities/registry.h"
#include "core/serialization/json.h"
#include "core/vendors/dispatch.h"

namespace ef::core::capabilities {

enum class VendorPath { Write, ReadOnly, Unsupported };
inline std::string_view to_string(VendorPath p) noexcept {
    switch (p) {
        case VendorPath::Write: return "write";
        case VendorPath::ReadOnly: return "read_only";
        case VendorPath::Unsupported: return "unsupported";
    }
    return "?";
}

// Yazma/okuma yolunun kanali. VendorDriver = ureticinin surucu runtime'i (NVAPI/ADLX/IGCL); fail-closed kapisi bu kanala bakar.
enum class WriteChannel { None, Windows, VendorDriver };
inline std::string_view to_string(WriteChannel c) noexcept {
    switch (c) {
        case WriteChannel::None: return "none";
        case WriteChannel::Windows: return "windows";
        case WriteChannel::VendorDriver: return "vendor_driver";
    }
    return "?";
}

// Gerekce anahtarlari (UI/metin kataloguna design/strings/pending/p9-hardware-honesty.json ile eklenir).
namespace matrix_reason {
inline constexpr std::string_view kWriteUnitOnly = "hw.matrix.write_unit_only";
inline constexpr std::string_view kWriteGatedOff = "hw.matrix.write_gated_off";
inline constexpr std::string_view kReadBiosGuide = "hw.matrix.read_bios_guide";
inline constexpr std::string_view kReadDetection = "hw.matrix.read_detection_only";
inline constexpr std::string_view kReadGameConfig = "hw.matrix.read_game_config_only";
inline constexpr std::string_view kReadTelemetryDevice = "hw.matrix.read_telemetry_device_read";
inline constexpr std::string_view kAmdLicense = "hw.matrix.amd_sdk_license_pending";
inline constexpr std::string_view kIntelNoDevice = "hw.matrix.igcl_no_binding_no_device";
inline constexpr std::string_view kOtherVendor = "hw.matrix.other_vendor_feature";
inline constexpr std::string_view kExcluded = "hw.matrix.excluded_policy";
inline constexpr std::string_view kPolicyNoOc = "hw.matrix.policy_no_oc_voltage_fan_write";
inline constexpr std::string_view kNotImplemented = "hw.matrix.not_implemented";
inline constexpr std::string_view kOsNoApi = "hw.matrix.os_no_documented_api";
}  // namespace matrix_reason

struct ReasonText {
    std::string_view key;
    std::string_view code;  // docs tablosunda kisa gonderme
    std::string_view tr;
    std::string_view en;
};
inline constexpr std::array<ReasonText, 13> kReasonTexts{{
    {matrix_reason::kWriteUnitOnly, "A1",
     "Kodda yazma yolu var; yalnız birim/sahte arka uç testi. Gerçek cihazda yazma kanıtı yok; sürüm kapısı kapalı.",
     "A write path exists in code; unit/mock tests only. No real-device write proof; release gate closed."},
    {matrix_reason::kWriteGatedOff, "A2",
     "Yazma yolu kodda var ama kapalı: resmî belgede birim/anlam doğrulanana kadar plana girmez.",
     "A write path exists in code but is switched off until the unit and meaning are confirmed in official documentation."},
    {matrix_reason::kReadBiosGuide, "B1",
     "Yalnız okuma tahmini ve BIOS rehberi; Windows'tan resmî yazma yolu yok.",
     "Read-only estimate and BIOS guide; no official write path from Windows."},
    {matrix_reason::kReadDetection, "B2",
     "Yalnız tespit/okuma; yazma yolu yok.", "Detection/read only; no write path."},
    {matrix_reason::kReadGameConfig, "B3",
     "Oyun ayar dosyası yalnız okunur (şu an yalnız CS2); oyun ayarı yazımı kapalı.",
     "The game config file is only read (currently CS2 only); writing game settings is closed."},
    {matrix_reason::kReadTelemetryDevice, "B4",
     "Sürücü telemetrisi okunur; bu PC'de gerçek cihazda okuma doğrulandı (yazma yok).",
     "Driver telemetry is read; the read was confirmed on a real device on the dev PC (no writes)."},
    {matrix_reason::kAmdLicense, "C1",
     "AMD SDK lisansı (ADLX / Ryzen Master) ürün sahibince kabul edilmedi; başlık ve bağlayıcı yok.",
     "AMD SDK licence (ADLX / Ryzen Master) not accepted by the product owner; no headers or binding."},
    {matrix_reason::kIntelNoDevice, "C2",
     "Intel IGCL bağlayıcısı yok ve Intel GPU'lu test cihazı yok; yalnız sözleşme + mock testi.",
     "No Intel IGCL binding and no Intel GPU test device; contract and mock tests only."},
    {matrix_reason::kOtherVendor, "D1",
     "Bu ayar başka üreticinin sürücü özelliğidir; bu üretici kimliğinde karşılığı yok.",
     "This setting is another vendor's driver feature; it has no counterpart for this vendor identity."},
    {matrix_reason::kExcluded, "D2", "Ürün politikasıyla dışlandı; hiçbir üreticide açılmaz.",
     "Excluded by product policy; never enabled for any vendor."},
    {matrix_reason::kPolicyNoOc, "D3",
     "Ürün değişmezi: genel voltaj / fan / aşırı hız yazımı yok; yönetici hakkı bunu açmaz.",
     "Product invariant: no generic voltage / fan / overclock writes; administrator rights do not enable it."},
    {matrix_reason::kNotImplemented, "E1", "Henüz uygulanmadı; kodda yol yok.", "Not implemented yet; no code path."},
    {matrix_reason::kOsNoApi, "E2", "Windows bu işlem için belgeli, güvenilir bir API sunmuyor.",
     "Windows offers no documented, dependable API for this operation."},
}};

inline const ReasonText* find_reason(std::string_view key) noexcept {
    for (const auto& r : kReasonTexts)
        if (r.key == key) return &r;
    return nullptr;
}

struct Cell {
    VendorPath path = VendorPath::Unsupported;
    WriteChannel channel = WriteChannel::None;
    std::string_view reason;
    contracts::EvidenceLevel evidence = contracts::EvidenceLevel::Mock;  // yolun KENDI kanit tavani (yazma icin <= Unit)
};

struct VendorSpec {
    std::string_view id;
    Cell nvidia, amd, intel;
};

struct VendorRow {
    std::string_view feature_id;
    vendors::Vendor vendor = vendors::Vendor::None;
    Cell cell;
};

namespace matrix_detail {
using contracts::EvidenceLevel;
namespace R = matrix_reason;
inline constexpr Cell win_write{VendorPath::Write, WriteChannel::Windows, R::kWriteUnitOnly, EvidenceLevel::Unit};
inline constexpr Cell drs_write{VendorPath::Write, WriteChannel::VendorDriver, R::kWriteUnitOnly, EvidenceLevel::Unit};
inline constexpr Cell drs_gated{VendorPath::ReadOnly, WriteChannel::VendorDriver, R::kWriteGatedOff, EvidenceLevel::Unit};
inline constexpr Cell read_bios{VendorPath::ReadOnly, WriteChannel::None, R::kReadBiosGuide, EvidenceLevel::Unit};
inline constexpr Cell read_detect{VendorPath::ReadOnly, WriteChannel::None, R::kReadDetection, EvidenceLevel::Unit};
inline constexpr Cell read_game{VendorPath::ReadOnly, WriteChannel::None, R::kReadGameConfig, EvidenceLevel::Unit};
inline constexpr Cell read_nv_telemetry{VendorPath::ReadOnly, WriteChannel::VendorDriver, R::kReadTelemetryDevice, EvidenceLevel::Hardware};
inline constexpr Cell amd_license{VendorPath::Unsupported, WriteChannel::None, R::kAmdLicense, EvidenceLevel::Mock};
inline constexpr Cell intel_nodev{VendorPath::Unsupported, WriteChannel::None, R::kIntelNoDevice, EvidenceLevel::Mock};
inline constexpr Cell other_vendor{VendorPath::Unsupported, WriteChannel::None, R::kOtherVendor, EvidenceLevel::Mock};
inline constexpr Cell excluded{VendorPath::Unsupported, WriteChannel::None, R::kExcluded, EvidenceLevel::Mock};
inline constexpr Cell policy_no_oc{VendorPath::Unsupported, WriteChannel::None, R::kPolicyNoOc, EvidenceLevel::Mock};
// Kodda yazma yolu var ama bayrakla KAPALI ve plana girmiyor (okuma da urunde baglanmadi): "Desteklenmiyor [A2]" (yalniz okuma degil).
inline constexpr Cell win_gated_off{VendorPath::Unsupported, WriteChannel::None, R::kWriteGatedOff, EvidenceLevel::Mock};
inline constexpr Cell not_impl{VendorPath::Unsupported, WriteChannel::None, R::kNotImplemented, EvidenceLevel::Mock};
inline constexpr Cell os_no_api{VendorPath::Unsupported, WriteChannel::None, R::kOsNoApi, EvidenceLevel::Mock};

constexpr VendorSpec same(std::string_view id, Cell c) { return {id, c, c, c}; }
// Uretici-bagimsiz ayar, yalniz NVAPI DRS yolu var: AMD/Intel API'leri baglanmadi (lisans / cihaz yok).
constexpr VendorSpec nv_only(std::string_view id, Cell nv) { return {id, nv, amd_license, intel_nodev}; }
// Adi NVIDIA'ya bagli ayar (G01/G02): AMD/Intel'de ayni ayar yok.
constexpr VendorSpec nv_named(std::string_view id, Cell nv) { return {id, nv, other_vendor, other_vendor}; }
}  // namespace matrix_detail

// Tek kaynak: 64 katalog ozelligi (research/setting_catalog.json ile ayni kimlikler; testi kapsami denetler).
inline constexpr std::array<VendorSpec, 64> kVendorSpecs = [] {
    using namespace matrix_detail;
    return std::array<VendorSpec, 64>{{
        // Windows (uretici bagimsiz Windows yazma yolu; gercek cihaz kaniti ayri)
        same("W01", win_write), same("W02", win_write), same("W03", win_write), same("W04", win_write),
        same("W05", win_write), same("W06", win_write), same("W07", win_write), same("W08", win_write),
        same("W09", win_write), same("W10", win_write), same("W11", win_write), same("W12", win_write),
        // ADR-040 (B9 ek not): W13 USB kodda yazma yolu var (varsayilan Hayir, bayrak yok); W14 ve W16 kodda var ama bayrakla
        // KAPALI (W16'da BCD okuma yolu da dogrulanmadi); W15 icin resmi kaynak yok, katalog girdisi yok.
        // Bir bayrak acilirsa satir ve katalog durumu AYNI degisiklikte guncellenir (vendor_matrix_adr040_rows_follow_real_state).
        same("W13", win_write), same("W14", win_gated_off), same("W15", not_impl), same("W16", win_gated_off),
        // GPU
        nv_named("G01", drs_write), nv_named("G02", drs_write),
        VendorSpec{"G03", other_vendor, amd_license, other_vendor},
        VendorSpec{"G04", other_vendor, other_vendor, intel_nodev},
        nv_only("G05", drs_write), nv_only("G06", drs_write), nv_only("G07", drs_gated),
        VendorSpec{"G08", other_vendor, amd_license, other_vendor},
        VendorSpec{"G09", other_vendor, amd_license, other_vendor},
        same("G10", policy_no_oc), same("G11", policy_no_oc), same("G12", read_bios),
        // B6 (ADR-040 m.8): NVAPI DRS yazma yolu kodda ama ayar basina bayrakla KAPALI -> NVIDIA "yalniz okuma (kapali)".
        // Bayrak acildiginda (gercek GPU turu sonrasi) satir drs_write olur; test bayrak ile satiri birlikte denetler.
        nv_named("G13", drs_gated), nv_named("G14", drs_gated),
        // Oyun (ayar dosyasi yazimi kapali; yalniz CS2 okunur / tespit)
        same("O01", read_game), same("O02", not_impl), same("O03", read_game), same("O04", read_game),
        same("O05", read_detect), same("O06", not_impl), same("O07", read_game), same("O08", read_detect),
        // Donanim (CPU/RAM/BIOS: uretici-surucu yazma yok)
        same("H01", policy_no_oc), same("H02", read_bios), same("H03", not_impl), same("H04", read_detect),
        same("H05", win_write),
        VendorSpec{"H06", read_nv_telemetry, amd_license, intel_nodev},
        // Disarida tutulanlar
        same("X01", excluded), same("X02", excluded), same("X03", excluded), same("X04", excluded),
        same("X05", excluded), same("X06", excluded), same("X07", excluded), same("X08", excluded),
        same("X09", excluded), same("X10", excluded), same("X11", excluded), same("X12", excluded),
        same("X13", excluded),
        // Baslangic
        same("B01", win_write), same("B02", win_write), same("B03", win_write), same("B04", win_write),
        same("B05", win_write), same("B06", os_no_api), same("B07", win_write),
    }};
}();

inline const VendorSpec* find_vendor_spec(std::string_view id) noexcept {
    for (const auto& s : kVendorSpecs)
        if (s.id == id) return &s;
    return nullptr;
}

inline const Cell* vendor_cell(const VendorSpec& s, vendors::Vendor v) noexcept {
    switch (v) {
        case vendors::Vendor::Nvidia: return &s.nvidia;
        case vendors::Vendor::Amd: return &s.amd;
        case vendors::Vendor::Intel: return &s.intel;
        case vendors::Vendor::None: break;
    }
    return nullptr;
}

// Vendor::None (bilinmeyen/okunamayan kimlik) icin satir YOKTUR: cagiran fail-closed davranir.
inline std::optional<VendorRow> vendor_row(std::string_view feature_id, vendors::Vendor v) noexcept {
    const auto* s = find_vendor_spec(feature_id);
    if (!s) return std::nullopt;
    const auto* c = vendor_cell(*s, v);
    if (!c) return std::nullopt;
    return VendorRow{s->id, v, *c};
}

inline constexpr std::array<vendors::Vendor, 3> kMatrixVendors{vendors::Vendor::Nvidia, vendors::Vendor::Amd, vendors::Vendor::Intel};

inline std::vector<VendorRow> all_vendor_rows() {
    std::vector<VendorRow> out;
    out.reserve(kVendorSpecs.size() * kMatrixVendors.size());
    for (const auto& s : kVendorSpecs)
        for (const auto v : kMatrixVendors) out.push_back({s.id, v, *vendor_cell(s, v)});
    return out;
}

// Uretici-surucu yazma satiri mi (fail-closed kapisi bunu sorar)?
inline bool is_vendor_driver_write(const Cell& c) noexcept {
    return c.path == VendorPath::Write && c.channel == WriteChannel::VendorDriver;
}

// JSON cikti: ortam bagimsiz, deterministik. release_enabled bu tabloda YOKTUR (registry'den gelir); "supported" kelimesi yoktur.
inline json::Value vendor_matrix_json() {
    auto arr = json::Value::array();
    for (const auto& r : all_vendor_rows()) {
        auto o = json::Value::object();
        o.set("feature", json::Value::string(std::string(r.feature_id)));
        o.set("vendor", json::Value::string(std::string(vendors::to_string(r.vendor))));
        o.set("path", json::Value::string(std::string(to_string(r.cell.path))));
        o.set("channel", json::Value::string(std::string(to_string(r.cell.channel))));
        o.set("reason_key", json::Value::string(std::string(r.cell.reason)));
        o.set("evidence", json::Value::string(std::string(contracts::to_string(r.cell.evidence))));
        arr.push(std::move(o));
    }
    auto root = json::Value::object();
    root.set("schema_version", json::Value::integer(1));
    root.set("rows", std::move(arr));
    return root;
}

// design/strings/pending/p9-hardware-honesty.json govdesi: {"anahtar": ["tr", "en"]} (scripts/add_strings.py bicimi).
inline json::Value reason_texts_json() {
    auto root = json::Value::object();
    for (const auto& r : kReasonTexts) {
        auto pair = json::Value::array();
        pair.push(json::Value::string(std::string(r.tr)));
        pair.push(json::Value::string(std::string(r.en)));
        root.set(std::string(r.key), std::move(pair));
    }
    return root;
}

inline std::string_view path_label_tr(VendorPath p) noexcept {
    switch (p) {
        case VendorPath::Write: return "Yazma";
        case VendorPath::ReadOnly: return "Yalnız okuma";
        case VendorPath::Unsupported: return "Desteklenmiyor";
    }
    return "?";
}

// docs/20_SUPPORT_MATRIX.md govdesi. Deterministik (zaman/ortam yok); testi dosyayla birebir karsilastirir.
inline std::string vendor_matrix_markdown(const std::vector<FeatureDef>& features) {
    std::string md;
    md += "# Donanım destek matrisi (yetenek x üretici)\n\n";
    md += "Bu dosya `ef_support_matrix --markdown` çıktısıdır; elle düzenlenmez (`test_vendor_matrix` dosyayı üretilenle karşılaştırır).\n";
    md += "Kaynak tablo: `src/core/capabilities/vendor_matrix.h`.\n\n";
    md += "## Nasıl okunur\n\n";
    md += "- **Yazma**: kodda bu üretici kimliğinde çalışan bir yazma yolu var. Bu, cihazda desteklendiği veya sürümde açık olduğu anlamına GELMEZ: ";
    md += "kanıt tavanı birim/sahte arka uç testidir; gerçek cihaz yazma kanıtı ayrı kayıttır ve sürüm kapısı (`release_enabled`) hepsinde kapalıdır.\n";
    md += "- **Yalnız okuma**: tespit, okuma veya rehber; yazma yolu yok ya da kapalı.\n";
    md += "- **Desteklenmiyor**: bu üretici kimliğinde yol yok. Gerekçe kodu aşağıdaki tablodadır. Yönetici hakkı bu satırları değiştirmez.\n";
    md += "- Üretici kimliği PCI üretici numarasından gelir (NVIDIA 10DE, AMD 1002, Intel 8086). Bilinmeyen veya okunamayan kimlik satır almaz ve hiçbir yazma yolu açmaz.\n";
    md += "- **AMD ve Intel kimliğinde üretici sürücüsü (ADLX / IGCL) yazma yolu açık değildir.** AMD/Intel sütunlarındaki \"Yazma\" satırları yalnız üreticiden bağımsız Windows yollarıdır.\n";
    md += "- Hibrit sistemde (ör. AMD iGPU + NVIDIA ayrık kart) satır, oyunun gerçekten çalıştığı bağdaştırıcının üreticisine göre seçilir; DXGI sırası veya \"ilk kart\" kimlik değildir.\n\n";
    md += "## Matris\n\n";
    md += "| Kimlik | Ayar | NVIDIA | AMD | Intel |\n|---|---|---|---|---|\n";
    for (const auto& f : features) {
        const auto* s = find_vendor_spec(f.id);
        md += "| " + f.id + " | " + f.name + " |";
        for (const auto v : kMatrixVendors) {
            md += " ";
            if (!s) {
                md += "TABLO YOK";
            } else {
                const auto& c = *vendor_cell(*s, v);
                const auto* rt = find_reason(c.reason);
                md += std::string(path_label_tr(c.path)) + " [" + std::string(rt ? rt->code : "?") + "]";
            }
            md += " |";
        }
        md += "\n";
    }
    md += "\n## Gerekçe kodları\n\n| Kod | Anahtar | Açıklama |\n|---|---|---|\n";
    for (const auto& r : kReasonTexts)
        md += "| " + std::string(r.code) + " | `" + std::string(r.key) + "` | " + std::string(r.tr) + " |\n";
    md += "\nAdlandırma notu: AMD ve Intel için \"Yazma\" satırı olan özellikler yalnız `Windows` kanalındadır; "
          "`vendor_driver` kanalında yalnız NVIDIA yazma satırı vardır (NVAPI DRS: G01, G02, G05, G06).\n";
    return md;
}

}  // namespace ef::core::capabilities
