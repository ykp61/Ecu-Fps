// Spec 2026-11-12 H: gercek Windows Update surucu aramasi (yukseltilmemis, SALT OKUMA; indirme / kurulum yok). Istege
// bagli konsol araci: ctest'e eklenmez (aramayi Windows Update hizmeti internet uzerinden yapar). Uygulamanin kullandigi
// ayni yol: core::drivers::DriverScanner + platform::wua::make_driver_search.
// Kullanim: ef_wua_search_probe [zaman_asimi_sn]
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "core/drivers/driver_updates.h"
#include "platform/windows/wua/wua_search.h"

int wmain(int argc, wchar_t** argv) {
    namespace cd = ef::core::drivers;
    const long secs = argc > 1 ? std::wcstol(argv[1], nullptr, 10) : 120;
    cd::DriverScanner scanner([] { return ef::platform::wua::make_driver_search(); }, std::chrono::seconds(secs > 0 ? secs : 120));
    std::mutex mu;
    std::condition_variable cv;
    bool finished = false;
    cd::ScanView view;
    const auto t0 = std::chrono::steady_clock::now();
    scanner.start([&](const cd::ScanView& v) {
        std::lock_guard lk(mu);
        view = v;
        finished = true;
        cv.notify_all();
    });
    {
        std::unique_lock lk(mu);
        cv.wait(lk, [&] { return finished; });
    }
    scanner.join();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("wua_search: state=%d failure=%d hresult=0x%08X found=%zu installable=%zu list_only=%zu elapsed_ms=%.0f\n",
                static_cast<int>(view.state), static_cast<int>(view.failure), static_cast<unsigned>(view.hresult), view.updates.size(),
                view.installable, view.list_only, ms);
    for (const auto& u : view.updates)
        std::printf("  - title=\"%s\" class=\"%s\" kind=%d firmware=%d version=\"%s\" date=%04d-%02d-%02d size=%llu hwid=\"%s\" provider=\"%s\"\n",
                    u.title.c_str(), u.driver_class.c_str(), static_cast<int>(cd::device_kind(u)), cd::is_firmware(u) ? 1 : 0, u.version.c_str(),
                    u.year, u.month, u.day, static_cast<unsigned long long>(u.size_bytes.value_or(0)), u.hardware_id.c_str(), u.provider.c_str());
    return view.state == cd::ScanState::Failed ? 1 : 0;
}
