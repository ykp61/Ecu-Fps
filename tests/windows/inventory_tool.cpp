// INV Windows testi: gercek envanteri JSON olarak stdout'a yazar (salt okuma).
// Kullanim: ef_inventory_tool [--raw]   (varsayilan: kimlikler redakte)

#include <cstdio>
#include <cwchar>
#include <string>

#include "core/logging/redact.h"
#include "core/serialization/json.h"
#include "platform/windows/inventory/inventory.h"
#include "platform/windows/win_env.h"

int wmain(int argc, wchar_t** argv) {
    const bool raw = argc > 1 && std::wcscmp(argv[1], L"--raw") == 0;
    const auto doc = ef::platform::inventory::collect_all_json();
    std::string out = ef::core::json::write(doc);
    if (!raw) out = ef::core::logging::redact(out, ef::platform::current_redaction_context());
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fputc('\n', stdout);
    return 0;
}
