// P9/B5: salt okuma ornekleyiciler (onceden app_measure.cpp icinde adsiz ad alanindaydi). Davranis degismedi.
#include "app/measure_sampling.h"

#include <windows.h>

#include <tlhelp32.h>

#include <cstdint>

#include "core/inventory/power_security.h"
#include "platform/windows/adapters/win_power.h"
#include "platform/windows/inventory/power_security_win.h"

namespace ef::app {

namespace {

// Donanim kesmesi + DPC suresi (tum islemciler toplami, 100 ns). Hicbir surece yazilmaz; GetSystemTimes cekirdek
// zamaninin icindedir. Okunamazsa nullopt (cikarilmaz: olcum eski kurala duser, daha temkinli); cagiran bunu
// CpuTimes::exempt_parts maskesine yazar, boylece gecici okuma hatasi sahte muaf fark uretmez (B3 inceleme).
std::optional<std::uint64_t> interrupt_dpc_time() noexcept {
    struct ProcPerf {  // SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION (winternl: Reserved1[0] = DpcTime, [1] = InterruptTime)
        LARGE_INTEGER idle, kernel, user, dpc, interrupt;
        ULONG interrupt_count;
    };
    using QueryFn = LONG(WINAPI*)(int, void*, ULONG, ULONG*);
    static const auto query = reinterpret_cast<QueryFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation")));
    if (!query) return std::nullopt;
    ProcPerf buf[256];
    ULONG got = 0;
    if (query(8 /* SystemProcessorPerformanceInformation */, buf, sizeof(buf), &got) != 0 || got < sizeof(ProcPerf)) return std::nullopt;
    std::uint64_t sum = 0;
    for (ULONG i = 0; i < got / sizeof(ProcPerf); ++i)
        sum += static_cast<std::uint64_t>(buf[i].dpc.QuadPart) + static_cast<std::uint64_t>(buf[i].interrupt.QuadPart);
    return sum;
}

}  // namespace

BalanceEnvironment read_balance_environment() {
    BalanceEnvironment e;
    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps)) {
        e.on_battery = ps.ACLineStatus == 0;
        e.battery_saver = ps.SystemStatusFlag == 1;
    }
    platform::adapters::WinPowerApi power;
    if (const auto scheme = power.get_active_scheme(); scheme.ok())
        e.power_saver_plan = core::inventory::plan_from_guid(scheme.value()) == core::inventory::PowerPlan::PowerSaver;
    if (const auto overlay = platform::inventory::read_effective_overlay_guid())
        e.efficiency_overlay = core::inventory::overlay_from_guid(*overlay) == core::inventory::PowerOverlay::BestEfficiency;
    return e;
}

// GetSystemTimes + GetProcessTimes: ikisi de CPU SURESI (100 ns). PDH "% Processor Utility" burada kullanilmaz: frekansla
// olceklenir ve sure tabanli surec zamaniyla cikarildiginda turbo altinda sahte "baska surec" yuku uretir; ayrica GPU
// motoru sayaclarini toplamak olcum sirasinda CPU harcar.
// denge_v2: kendi oturumumuzdaki masaustu bilesimcisi (dwm.exe) surecleri. "Baska program" sayilmaz: testin
// penceresini / canli gorunumu ekrana o cizer; kullanicinin kapatabilecegi bir program degildir.
std::vector<std::uint32_t> session_compositor_pids() {
    std::vector<std::uint32_t> out;
    DWORD own_session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &own_session)) return out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) {
        DWORD session = 0;
        if (_wcsicmp(e.szExeFile, L"dwm.exe") == 0 && ProcessIdToSessionId(e.th32ProcessID, &session) && session == own_session)
            out.push_back(e.th32ProcessID);
    }
    CloseHandle(snap);
    return out;
}

// `own`: payi "baska program" sayilmayan surec (denge testinde bu uygulama, oyun ici testte oyun). Okunamazsa nullopt.
std::optional<core::metrics::CpuTimes> read_cpu_times(HANDLE own, const std::vector<HANDLE>& exempt_procs) noexcept {
    const auto u64 = [](const FILETIME& f) { return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    FILETIME idle{}, kernel{}, user{};  // sistem cekirdek zamani bosta zamani ICERIR
    if (!GetSystemTimes(&idle, &kernel, &user)) return std::nullopt;
    FILETIME created{}, exited{}, own_kernel{}, own_user{};
    if (!own || !GetProcessTimes(own, &created, &exited, &own_kernel, &own_user)) return std::nullopt;
    core::metrics::CpuTimes t;
    t.total = u64(kernel) + u64(user);
    if (t.total < u64(idle)) return std::nullopt;
    t.busy = t.total - u64(idle);
    t.own = u64(own_kernel) + u64(own_user);
    // B3 inceleme: hangi muaf parcanin gercekten okundugu maskeye yazilir; maskesi farkli okumalar karsilastirilmaz.
    if (const auto dpc = interrupt_dpc_time()) {
        t.exempt = *dpc;
        t.exempt_parts |= 1u;
    }
    for (std::size_t i = 0; i < exempt_procs.size() && i < 31; ++i) {
        FILETIME c{}, x{}, k{}, u{};
        if (GetProcessTimes(exempt_procs[i], &c, &x, &k, &u)) {
            t.exempt += u64(k) + u64(u);
            t.exempt_parts |= 1u << (i + 1);
        }
    }
    return t;
}

}  // namespace ef::app
