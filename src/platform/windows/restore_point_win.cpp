#include "platform/windows/restore_point_win.h"

#include <windows.h>
#include <srrestoreptapi.h>

#include <string>

#include "platform/windows/win_env.h"
#include "platform/windows/wmi.h"

namespace ef::platform {

std::optional<bool> system_restore_enabled() {
    DWORD v = 0;
    DWORD sz = sizeof(v);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows NT\\SystemRestore", L"DisableSR", RRF_RT_REG_DWORD, nullptr, &v, &sz) ==
            ERROR_SUCCESS &&
        v == 1)
        return false;
    return std::nullopt;
}

namespace core_rp = core::recovery;

namespace {

using SetFn = BOOL(WINAPI*)(PRESTOREPOINTINFOW, PSTATEMGRSTATUS);

// Calisma klasorunden DLL yuklenmez: yalniz System32.
SetFn resolve() {
    static const HMODULE mod = LoadLibraryExW(L"srclient.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return mod ? reinterpret_cast<SetFn>(GetProcAddress(mod, "SRSetRestorePointW")) : nullptr;
}

core_rp::RpCall call(DWORD event, DWORD type, std::int64_t seq, const std::wstring& description) {
    core_rp::RpCall out;
    const auto fn = resolve();
    if (!fn) {
        out.status = ERROR_MOD_NOT_FOUND;
        return out;
    }
    RESTOREPOINTINFOW info{};
    info.dwEventType = event;
    info.dwRestorePtType = type;
    info.llSequenceNumber = seq;
    wcsncpy_s(info.szDescription, description.c_str(), _TRUNCATE);
    STATEMGRSTATUS st{};
    const BOOL ok = fn(&info, &st);
    out.status = ok ? 0 : (st.nStatus != 0 ? static_cast<std::uint32_t>(st.nStatus) : static_cast<std::uint32_t>(GetLastError()));
    out.seq = st.llSequenceNumber;
    return out;
}

}  // namespace

core_rp::RpCall WinRestorePointApi::begin(const std::string& description) {
    return call(BEGIN_SYSTEM_CHANGE, APPLICATION_INSTALL, 0, to_wide(description));
}

std::uint32_t WinRestorePointApi::end(std::int64_t seq) {
    return call(END_SYSTEM_CHANGE, APPLICATION_INSTALL, seq, L"").status;
}

std::uint32_t WinRestorePointApi::cancel(std::int64_t seq) {
    return call(END_SYSTEM_CHANGE, CANCELLED_OPERATION, seq, L"").status;
}

std::optional<std::vector<core_rp::RpItem>> WinRestorePointApi::inventory() {
    auto rows = wmi::query(L"ROOT\\DEFAULT", L"SELECT SequenceNumber, Description FROM SystemRestore", {L"SequenceNumber", L"Description"});
    if (!rows.ok()) return std::nullopt;
    std::vector<core_rp::RpItem> out;
    for (const auto& row : rows.value()) {
        core_rp::RpItem it;
        if (const auto s = row.find("SequenceNumber"); s != row.end() && s->second.i) it.seq = static_cast<std::uint32_t>(*s->second.i);
        if (const auto d = row.find("Description"); d != row.end() && d->second.s) it.description = *d->second.s;
        out.push_back(std::move(it));
    }
    return out;
}

}  // namespace ef::platform
