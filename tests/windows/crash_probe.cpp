// FOUND-05 Windows testi: cokme isleyicisini kurar ve kasitli erisim ihlali uretir.
// Kullanim: ef_crash_probe <dump_dir> <max_dumps>

#include <cstdlib>
#include <string>

#include "platform/windows/crash_handler.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    ef::platform::CrashConfig cfg;
    cfg.dir = argv[1];
    cfg.max_dumps = static_cast<std::size_t>(std::wcstoul(argv[2], nullptr, 10));
    if (!ef::platform::install_crash_handler(cfg)) return 3;
    volatile int* p = nullptr;
    *p = 42;  // kasitli cokme
    return 0;
}
