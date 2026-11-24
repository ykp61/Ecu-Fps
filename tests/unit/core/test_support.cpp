#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/support/bundle.h"
#include "core/support/zip_writer.h"
#include "ef_test.h"

using namespace ef::core::support;

EF_TEST(crc32_known_vectors) {
    EF_CHECK(crc32("") == 0x00000000u);
    EF_CHECK(crc32("123456789") == 0xCBF43926u);
    EF_CHECK(crc32("The quick brown fox jumps over the lazy dog") == 0x414FA339u);
}

EF_TEST(zip_rejects_unsafe_names) {
    ZipWriter z;
    EF_CHECK(!z.add("../evil.txt", "x"));
    EF_CHECK(!z.add("/abs.txt", "x"));
    EF_CHECK(!z.add("C:/x.txt", "x"));
    EF_CHECK(!z.add("", "x"));
    EF_CHECK(z.add("logs\\a.log", "x"));
    EF_CHECK(!z.add("logs/a.log", "y"));  // ters bolu normalize edildi -> tekrar
    EF_CHECK(z.entries() == 1);
}

EF_TEST(zip_structure_is_valid) {
    ZipWriter z;
    EF_CHECK(z.add("a.txt", "hello"));
    EF_CHECK(z.add("dir/b.txt", ""));
    const std::string zip = z.finish();
    EF_CHECK(zip.compare(0, 4, "PK\x03\x04") == 0);
    const auto eocd = zip.rfind(std::string("PK\x05\x06", 4));
    EF_CHECK(eocd != std::string::npos);
    EF_CHECK(eocd + 22 == zip.size());
    EF_CHECK(static_cast<unsigned char>(zip[eocd + 10]) == 2);  // toplam kayit
}

EF_TEST(make_bundle_request_maps_app_data_dirs_and_keeps_dumps_opt_in) {
    // R21: istek yollari uygulama veri dizininden turetilir; dump'lar yalniz acik onayla (varsayilan kapali).
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
                      ("ef-bundle-req-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto req = make_bundle_request(root, false, "1.2.3", "10.0.26300.9711");
    EF_CHECK(req.logs_dir == root / "logs");
    EF_CHECK(req.crash_dir == root / "crash");
    EF_CHECK(!req.include_dumps);  // varsayilan: bellek iceren dump yok
    EF_CHECK(req.app_version == "1.2.3");
    EF_CHECK(req.os_build == "10.0.26300.9711");
    const auto opted = make_bundle_request(root, true, "1.2.3", "10.0.26300.9711");
    EF_CHECK(opted.include_dumps);
    EF_CHECK(opted.logs_dir == req.logs_dir && opted.crash_dir == req.crash_dir);
    EF_CHECK(!fs::exists(root));  // yalniz yol eslemesi: gercek kullanici verisi okunmaz/yazilmaz
}

EF_TEST(bundle_redacts_logs_and_skips_dumps_by_default) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
                      ("ef-bundle-unit-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "logs");
    fs::create_directories(root / "crash");
    {
        std::ofstream(root / "logs" / "app.log") << "user testuser opened C:\\Users\\testuser\\a.txt token=abc123\n";
        std::ofstream(root / "logs" / "notes.txt") << "ignored\n";
        std::ofstream(root / "crash" / "crash-1.dmp") << "MDMPsecretmemory";
    }
    ef::core::logging::RedactionContext ctx;
    ctx.user_name = "testuser";
    BundleRequest req;
    req.logs_dir = root / "logs";
    req.crash_dir = root / "crash";
    req.app_version = "0.1.0";
    req.os_build = "10.0.26300";
    auto r = build_bundle(req, ctx);
    EF_CHECK(r.ok());
    const auto& zip = r.value().zip_bytes;  // stored ZIP: icerik duz metin aranabilir
    EF_CHECK(zip.find("testuser") == std::string::npos);
    EF_CHECK(zip.find("abc123") == std::string::npos);
    EF_CHECK(zip.find("secretmemory") == std::string::npos);
    EF_CHECK(zip.find("ignored") == std::string::npos);
    EF_CHECK(r.value().entries.size() == 2);  // logs/app.log + manifest.json

    req.include_dumps = true;
    auto r2 = build_bundle(req, ctx);
    EF_CHECK(r2.value().zip_bytes.find("secretmemory") != std::string::npos);  // yalniz acik onayla
    fs::remove_all(root);
}

// P3: Denge raporu ayni paket yolunu kullanir; ek dosya (Denge gecmis kaydi) de redakte edilir, balance.log pakete girer.
EF_TEST(bundle_extra_files_are_redacted_and_balance_log_included) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
                      ("ef-bundle-extra-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "logs");
    {
        std::ofstream(root / "logs" / "balance.log") << "run step=cpu status=invalid reason=cpu_step_invalid\n";
    }
    ef::core::logging::RedactionContext ctx;
    ctx.user_name = "testuser";
    BundleRequest req;
    req.logs_dir = root / "logs";
    req.crash_dir = root / "crash";
    req.app_version = "0.1.0";
    req.os_build = "10.0.26300";
    req.extra_files.emplace_back("balance_history.json", "{\"title\":\"C:\\Users\\testuser\\x\",\"source\":\"balance\"}\n");
    auto r = build_bundle(req, ctx);
    EF_CHECK(r.ok());
    const auto& zip = r.value().zip_bytes;
    EF_CHECK(zip.find("testuser") == std::string::npos);
    EF_CHECK(zip.find("cpu_step_invalid") != std::string::npos);
    bool has_log = false, has_hist = false;
    for (const auto& e : r.value().entries) {
        has_log = has_log || e == "logs/balance.log";
        has_hist = has_hist || e == "balance_history.json";
    }
    EF_CHECK(has_log && has_hist);
    fs::remove_all(root);
}
