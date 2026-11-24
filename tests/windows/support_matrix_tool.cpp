// INV-04 + P9-B8: gercek ortam parmak iziyle destek matrisini uretir; yetenek x uretici tablosunu JSON'a ekler.
// Kullanim: ef_support_matrix <out.json> [--markdown <out.md>] [--strings <out.json>]
//   --strings : gerekce metinleri ({"anahtar": ["tr","en"]}); design/strings/pending/p9-hardware-honesty.json
//   out.json : {"matrix": <Registry::support_matrix>, "vendor_matrix": <yetenek x uretici satirlari>}
//   --markdown: yalniz sabit tablodan uretilen docs/20_SUPPORT_MATRIX.md govdesi (ortam bilgisi YOK, deterministik).
// Hicbir ayar yazmaz, hicbir surucu runtime'i yuklemez; yalniz okur.

#include <fstream>
#include <string>

#include "core/capabilities/registry.h"
#include "core/capabilities/vendor_matrix.h"
#include "core/serialization/json.h"
#include "platform/windows/inventory/environment.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    const auto& features = ef::core::capabilities::embedded_features();
    if (features.empty()) return 3;
    const wchar_t* md_path = nullptr;
    const wchar_t* strings_path = nullptr;
    for (int i = 2; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--markdown" && i + 1 < argc) md_path = argv[++i];
        else if (a == L"--strings" && i + 1 < argc) strings_path = argv[++i];
        else return 2;
    }
    if (strings_path) {
        std::ofstream s(strings_path, std::ios::binary);
        s << ef::core::json::write(ef::core::capabilities::reason_texts_json()) << '\n';
        if (!s) return 6;
    }
    if (md_path) {
        std::ofstream m(md_path, std::ios::binary);
        m << ef::core::capabilities::vendor_matrix_markdown(features);
        if (!m) return 5;
    }
    ef::core::capabilities::Registry reg(features);
    const auto env = ef::core::capabilities::fingerprint(ef::platform::inventory::current_environment());
    auto root = ef::core::json::Value::object();
    root.set("schema_version", ef::core::json::Value::integer(2));
    root.set("matrix", reg.support_matrix(env));
    root.set("vendor_matrix", ef::core::capabilities::vendor_matrix_json());
    std::ofstream f(argv[1], std::ios::binary);
    f << ef::core::json::write(root) << '\n';
    return f ? 0 : 4;
}
