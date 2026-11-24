#include <crtdbg.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <windows.h>

#include "ef_test.h"

int main() {
    // Debug CRT assert/abort pencereleri testi sonsuza kadar bekletmesin: stderr'e yaz, hemen bitir.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    // EF_TEST_FILTER=alt_dize: yalniz adi alt dizeyi iceren testler kosulur (zamanlama/yuk kaynakli flake tanisinda
    // 2000+ testin tamamini beklemeden tekrar kosusu icin). Bos/yok: hepsi.
    char* filter = nullptr;
    std::size_t filter_len = 0;
    if (_dupenv_s(&filter, &filter_len, "EF_TEST_FILTER") == 0 && filter && *filter) {
        auto& reg = ::ef::test::registry();
        reg.erase(std::remove_if(reg.begin(), reg.end(), [filter](const ::ef::test::Case& c) { return !std::strstr(c.name, filter); }),
                  reg.end());
    }
    std::free(filter);
    return ::ef::test::run_all();
}
