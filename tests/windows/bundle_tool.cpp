// FOUND-05 Windows testi: gercek redaksiyon baglamiyla destek paketi uretir.
// Kullanim: ef_bundle_tool <logs_dir> <crash_dir> <out.zip>

#include <fstream>

#include "core/support/bundle.h"
#include "platform/windows/win_env.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) return 2;
    ef::core::support::BundleRequest req;
    req.logs_dir = argv[1];
    req.crash_dir = argv[2];
    req.app_version = "0.1.0";
    req.os_build = "test";
    auto r = ef::core::support::build_bundle(req, ef::platform::current_redaction_context());
    if (!r.ok()) return 3;
    std::ofstream f(argv[3], std::ios::binary);
    f.write(r.value().zip_bytes.data(), static_cast<std::streamsize>(r.value().zip_bytes.size()));
    return f ? 0 : 4;
}
