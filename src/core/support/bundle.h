#pragma once
// Kullanici kontrollu destek paketi: redakte edilmis loglar + manifest. Dump'lar bellek icerdigi icin
// yalniz kullanici acikca isterse eklenir. Paket yalniz yerel dosyaya yazilir; gonderim yok.

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "contracts/result.h"
#include "core/logging/redact.h"

namespace ef::core::support {

struct BundleRequest {
    std::filesystem::path logs_dir;
    std::filesystem::path crash_dir;
    bool include_dumps = false;        // kullanici onayi gerekir
    std::string app_version;           // "0.1.0"
    std::string os_build;              // "10.0.26300" (isim/kimlik yok)
    std::size_t max_log_bytes = 4u << 20;
    // Uyumluluk B8 (bosluk 8): diag.json metni (core::support::diag_json). Bos = eklenmez. Paketlenmeden once redakte edilir.
    std::string diag_json;
    // Denge raporu (P3): pakete eklenecek ek metin dosyalari {paket ici ad, icerik}. Ad sabit/guvenli olmalidir (cagiran
    // verir); icerik paketlenmeden once satir satir redakte edilir. Bos = eklenmez.
    std::vector<std::pair<std::string, std::string>> extra_files;
};

struct BundleResult {
    std::string zip_bytes;
    std::vector<std::string> entries;
};

// Uygulama veri dizininden istek: <app_data>/logs ve <app_data>/crash. Dump'lar yalniz include_dumps=true
// (kullanicinin acik onayi) ile eklenir. Boylece cagri yerleri ayni yollari tekrar kurmaz.
BundleRequest make_bundle_request(const std::filesystem::path& app_data_dir, bool include_dumps, std::string app_version,
                                  std::string os_build);

// Loglar paketlenmeden once yeniden redakte edilir (eski surum loglarina karsi savunma).
contracts::Result<BundleResult> build_bundle(const BundleRequest& req, const logging::RedactionContext& ctx);

}  // namespace ef::core::support
