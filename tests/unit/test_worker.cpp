// Denetim 4.1: spawn_worker govdeyi calistirir, yalniz-tasinabilir yakalamalari tasir ve normal donuste hicbir sey
// yapmaz (istisna yolu sureci sonlandirdigi icin birim testte calistirilmaz; dump yolu crash_handler'dadir).
#include <atomic>
#include <memory>

#include "app/worker.h"
#include "ef_test.h"

EF_TEST(spawn_worker_runs_body_and_moves_captures) {
    std::atomic<int> got{0};
    auto p = std::make_unique<int>(41);
    auto t = ef::app::spawn_worker([&got, p = std::move(p)] { got = *p + 1; });
    t.join();
    EF_CHECK(got == 42);
}

#include <windows.h>

#include <cstring>

namespace {
// SEH: fatal_worker_exception'in firlattigi kaydi yakalar (surec sonlanmaz). C++ nesnesi yok (__try kurali).
char g_text[128];
unsigned long g_code = 0;
int capture(EXCEPTION_POINTERS* ep) {
    const auto* r = ep->ExceptionRecord;
    g_code = r->ExceptionCode;
    const std::size_t n = r->NumberParameters ? static_cast<std::size_t>(r->ExceptionInformation[0]) : 0;
    if (n < sizeof g_text && r->NumberParameters >= 1 + (n + 7) / 8) std::memcpy(g_text, &r->ExceptionInformation[1], n);
    return EXCEPTION_EXECUTE_HANDLER;
}
void raise_fatal(const char* what) {
    __try {
        ef::platform::fatal_worker_exception(what);
    } __except (capture(GetExceptionInformation())) {
    }
}
}  // namespace

// Denetim 4.1 inceleme: istisna metni yigindaki gostericiye degil, istisna kaydinin kendisine kopyalanir (MiniDumpNormal
// yigini yazmaz); uzun metin kesilir.
EF_TEST(fatal_worker_exception_carries_text_in_record) {
    std::memset(g_text, 0, sizeof g_text);
    raise_fatal("disk dolu: journal yazilamadi");
    EF_CHECK(g_code == ef::platform::kWorkerExceptionCode);
    EF_CHECK(std::strcmp(g_text, "disk dolu: journal yazilamadi") == 0);
    std::memset(g_text, 0, sizeof g_text);
    char longer[300];
    std::memset(longer, 'x', sizeof longer - 1);
    longer[sizeof longer - 1] = 0;
    raise_fatal(longer);
    EF_CHECK(std::strlen(g_text) == (EXCEPTION_MAXIMUM_PARAMETERS - 1) * sizeof(ULONG_PTR));
}
