#pragma once
// P9/B5: Olcum kosularinin paylasilan, App'ten bagimsiz yardimcilari (onceden app_internal.h icindeydi). Oyun sureci
// probe'lari, ortam degiskeni, Ar-Ge kalibrasyon kancasi ve zaman yardimcilari. Davranis degismedi.
#include <windows.h>

#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark/runner/runner.h"
#include "contracts/measure.h"
#include "core/diagnosis/calibration_export.h"  // DIAG-10: diagcal_v1 ornek uretimi
#include "core/diagnosis/components.h"          // DIAG-10: MEAS-03 ornekleme aralik kapisi
#include "core/insights/history.h"
#include "core/measure/view.h"
#include "platform/windows/inventory/gpu_storage.h"  // F-63: adaptor ayrik VRAM toplami
#include "platform/windows/win_env.h"

namespace ef::app::detail {

constexpr UINT kMsgLiveFrame = WM_APP + 31;   // A4: cizim is parcacigi yeni onizleme karesi birakti
constexpr UINT kLiveViewPeriodMs = 250;      // canli sayilar (FrameTap kovalari ~4 Hz kapanir)

inline std::int64_t now_unix_ms() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const auto t = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<std::int64_t>((t - 116444736000000000ULL) / 10000ULL);
}

// Spec 2026-11-12 C: ekrandaki test listesi de kalici gecmis kadar (en yeni uc kayit).
inline void cap_measure_history(std::vector<core::measure::ResultView>& h) {
    if (h.size() > core::insights::kMaxEntries) h.erase(h.begin() + static_cast<std::ptrdiff_t>(core::insights::kMaxEntries), h.end());
}

// Sistem geneli QPC (ms). ETW kare ekseniyle ayni koken; yardimcinin capture_origin_ms degeri cikarilir.
inline double qpc_ms() noexcept {
    LARGE_INTEGER c{}, f{};
    if (!QueryPerformanceCounter(&c) || !QueryPerformanceFrequency(&f) || f.QuadPart <= 0) return 0.0;
    return static_cast<double>(c.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

// Oyunda Test: dogrulanmis kare yakalama suresi (kanal siniri 1..120 sn). min_seconds < seconds:
// gecerli sure duvar saatinden biraz kisadir; esit secilirse her kosu "too_short" olurdu.
constexpr double kIngameCaptureSeconds = 60.0;
constexpr double kIngameMinSeconds = 30.0;

// DIAG-08: drop penceresi tipik olarak 50-300 ms. MEAS-03 coznurluk kurali (ornek araligi <= olay/2)
// geregi 50 ms ornekleme yalniz >=100 ms pencereleri kok neden sayar; daha kisa pencereler durustce
// "baglam" kalir. 20 Hz yalniz test calisirken; normal UI orneklemesi 1 Hz'dir.
constexpr DWORD kIngameSampleIntervalMs = 50;

// Hedef oyunun CPU kullanimini GetProcessTimes ile olcer (yonetici gerekmez; korunan surecte
// PROCESS_QUERY_LIMITED_INFORMATION reddedilirse deger eksik kalir, tahmin edilmez).
struct GameCpuProbe {
    HANDLE process = nullptr;
    ULONGLONG last_kernel = 0, last_user = 0;
    double last_ms = 0;
    bool primed = false;

    explicit GameCpuProbe(DWORD pid) : process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {}
    ~GameCpuProbe() {
        if (process) CloseHandle(process);
    }
    GameCpuProbe(const GameCpuProbe&) = delete;
    GameCpuProbe& operator=(const GameCpuProbe&) = delete;

    // Donus: makine toplamina gore yuzde (0..100). Ilk ornekte delta yok -> nullopt.
    std::optional<double> sample(double now_ms, unsigned logical_processors) {
        if (!process || logical_processors == 0) return std::nullopt;
        FILETIME creation{}, exit_t{}, kernel{}, user{};
        if (!GetProcessTimes(process, &creation, &exit_t, &kernel, &user)) return std::nullopt;
        const ULONGLONG k = (static_cast<ULONGLONG>(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
        const ULONGLONG u = (static_cast<ULONGLONG>(user.dwHighDateTime) << 32) | user.dwLowDateTime;
        if (!primed) {
            last_kernel = k;
            last_user = u;
            last_ms = now_ms;
            primed = true;
            return std::nullopt;
        }
        const double elapsed = now_ms - last_ms;
        if (!(elapsed > 0)) return std::nullopt;
        const ULONGLONG delta = (k - last_kernel) + (u - last_user);
        last_kernel = k;
        last_user = u;
        last_ms = now_ms;
        const double pct = 100.0 * (static_cast<double>(delta) / 10000.0) /
                           (elapsed * static_cast<double>(logical_processors));
        if (!std::isfinite(pct)) return std::nullopt;
        return std::clamp(pct, 0.0, 100.0);
    }
};

// F-67: hedef oyunun ozel bellek kullanimi (MiB). Korunan surecte erisim reddedilirse deger eksik
// kalir; tahmin edilmez. Kosu boyunca artis, olasi surec-ici birikimi isaret eder (teshis degil).
struct GameMemoryProbe {
    HANDLE process = nullptr;

    explicit GameMemoryProbe(DWORD pid) : process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {}
    ~GameMemoryProbe() {
        if (process) CloseHandle(process);
    }
    GameMemoryProbe(const GameMemoryProbe&) = delete;
    GameMemoryProbe& operator=(const GameMemoryProbe&) = delete;

    std::optional<double> sample() const {
        if (!process) return std::nullopt;
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (!K32GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
            return std::nullopt;
        const double mib = static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
        if (!std::isfinite(mib) || mib <= 0.0) return std::nullopt;
        return mib;
    }
};

// F-54: hedef oyun surecinin yukledigi modul adlari (salt okuma). Erisim reddedilirse bos doner;
// API iddiası uretilmez. Tam ekran/pencereli sunum modu bu yolla okunamaz.
struct GameModuleProbe {
    static std::vector<std::string> names(DWORD pid) {
        std::vector<std::string> out;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE) return out;
        MODULEENTRY32W me{};
        me.dwSize = sizeof(me);
        if (Module32FirstW(snap, &me)) {
            do {
                char name[MAX_MODULE_NAME32 + 1]{};
                if (WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, name, sizeof(name), nullptr, nullptr) > 0)
                    out.emplace_back(name);
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
        return out;
    }
};

// Spec 2026-11-12 B: oyunun bulundugu ekranin yenileme hizi (Hz), kare siniri suphesi icin. Surecin gorunen,
// simge durumunda olmayan en buyuk ust pencere -> monitor -> gecerli ekran modu. Yalniz okuma (pencereye mesaj
// gonderilmez, odak degismez). Pencere / mod bulunamazsa nullopt: birincil ekran VARSAYILMAZ.
struct GameWindowPick {
    DWORD pid = 0;
    HWND hwnd = nullptr;
    long long area = 0;
};
inline BOOL CALLBACK pick_game_window(HWND h, LPARAM lp) {
    auto* p = reinterpret_cast<GameWindowPick*>(lp);
    DWORD owner = 0;
    GetWindowThreadProcessId(h, &owner);
    if (owner != p->pid || !IsWindowVisible(h) || IsIconic(h)) return TRUE;
    RECT rc{};
    if (!GetWindowRect(h, &rc)) return TRUE;
    const long long area = static_cast<long long>(rc.right - rc.left) * static_cast<long long>(rc.bottom - rc.top);
    if (area > p->area) {
        p->area = area;
        p->hwnd = h;
    }
    return TRUE;
}
inline std::optional<double> game_display_refresh_hz(std::uint32_t pid) {
    GameWindowPick pick;
    pick.pid = pid;
    EnumWindows(pick_game_window, reinterpret_cast<LPARAM>(&pick));
    if (!pick.hwnd) return std::nullopt;
    const HMONITOR mon = MonitorFromWindow(pick.hwnd, MONITOR_DEFAULTTONULL);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return std::nullopt;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) return std::nullopt;
    if (!(dm.dmFields & DM_DISPLAYFREQUENCY) || dm.dmDisplayFrequency <= 1) return std::nullopt;  // 0/1 = "varsayilan"
    return static_cast<double>(dm.dmDisplayFrequency);
}

// F-63: en buyuk ayrik VRAM'e sahip adaptorun toplami (MiB). Oyunun gercekten hangi adaptorde
// calistigini burada kanitlayamayiz; deger yalniz ust sinir baglamidir ve bilinmiyorsa nullopt kalir.
inline std::optional<double> largest_adapter_vram_mib() {
    double best = 0;
    for (const auto& g : platform::inventory::read_gpus()) {
        if (!g.dedicated_vram_mib.has()) continue;
        best = std::max(best, static_cast<double>(*g.dedicated_vram_mib.value));
    }
    if (!(best > 0)) return std::nullopt;
    return best;
}

// DIAG-10: hedef oyunun ANA is parcacigi (en erken olusturulan thread) CPU yuzdesi. GetThreadTimes
// olusturma zamanini verir; ana thread ayrimi tahmin edilmez. Surecun herhangi bir thread'i acilamaz/
// okunamazsa ana thread oldugu KANITLANAMAZ -> deger uretilmez (eksik kalir). Yalniz tek cekirdege gore
// yuzde (100 = bir cekirdek tam dolu); makine toplamina gore normalize edilmez.
struct GameMainThreadProbe {
    HANDLE thread = nullptr;
    DWORD tid = 0;  // F-41: yerlesim/kume sorgusu icin secilen ana thread kimligi (0 = yok)
    ULONGLONG last_kernel = 0, last_user = 0;
    double last_ms = 0;
    bool primed = false;

    explicit GameMainThreadProbe(DWORD pid) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        bool all_readable = true;
        bool found = false;
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID != pid) continue;
                found = true;
                HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
                FILETIME c{}, e{}, k{}, u{};
                if (!h || !GetThreadTimes(h, &c, &e, &k, &u)) {
                    if (h) CloseHandle(h);
                    all_readable = false;  // sirasi bilinmeyen bir thread atlandi: ana thread kanitlanamaz
                    continue;
                }
                const ULONGLONG created = (static_cast<ULONGLONG>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
                if (!thread || created < last_kernel) {  // last_kernel: secilen en erken olusturma zamani
                    if (thread) CloseHandle(thread);
                    thread = h;
                    tid = te.th32ThreadID;
                    last_kernel = created;
                } else {
                    CloseHandle(h);
                }
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
        if (!found || !all_readable) {
            if (thread) CloseHandle(thread);
            thread = nullptr;
            tid = 0;
        }
    }
    ~GameMainThreadProbe() {
        if (thread) CloseHandle(thread);
    }
    GameMainThreadProbe(const GameMainThreadProbe&) = delete;
    GameMainThreadProbe& operator=(const GameMainThreadProbe&) = delete;

    // Ilk ornekte delta yok -> nullopt. Yuzde tek cekirdek normalize (0..100).
    std::optional<double> sample(double now_ms) {
        if (!thread) return std::nullopt;
        FILETIME c{}, e{}, k{}, u{};
        if (!GetThreadTimes(thread, &c, &e, &k, &u)) return std::nullopt;
        const ULONGLONG kk = (static_cast<ULONGLONG>(k.dwHighDateTime) << 32) | k.dwLowDateTime;
        const ULONGLONG uu = (static_cast<ULONGLONG>(u.dwHighDateTime) << 32) | u.dwLowDateTime;
        if (!primed) {
            last_kernel = kk;
            last_user = uu;
            last_ms = now_ms;
            primed = true;
            return std::nullopt;
        }
        const double elapsed = now_ms - last_ms;
        if (!(elapsed > 0)) return std::nullopt;
        const ULONGLONG delta = (kk - last_kernel) + (uu - last_user);
        last_kernel = kk;
        last_user = uu;
        last_ms = now_ms;
        const double pct = 100.0 * (static_cast<double>(delta) / 10000.0) / elapsed;
        if (!std::isfinite(pct)) return std::nullopt;
        return std::clamp(pct, 0.0, 100.0);
    }
};

// DIAG-10: ortam degiskeni (sinirli; bos/hata -> bos).
inline std::wstring env_wide(const wchar_t* name, DWORD max_chars = 1024) {
    std::wstring buf(max_chars, L'\0');
    const DWORD n = GetEnvironmentVariableW(name, buf.data(), max_chars);
    if (n == 0 || n >= max_chars) return {};
    buf.resize(n);
    return buf;
}

// DIAG-10 (Ar-Ge veri toplama kancasi): uygulama kosusundan diagcal_v1 ornegi. Etiket DISARIDAN gelir
// (EF_CALIB_LABEL); siniflandirici ciktisindan turetilmez. Yalniz EF_CALIB_EXPORT yolu verildiginde
// yazar; yerel dosya, PII yok. Mevcut dosya once dogrulanir; bozuksa sessizce ezilmez.
inline void maybe_export_calibration(const benchmark::BenchmarkReport& rep, const contracts::Measurement& scene_gpu,
                              const contracts::Measurement& scene_cpu_core, const std::optional<double>& scene_interval_ms) {
    if (!core::diagnosis::calibration_export_eligible({rep.scene_measured, rep.cancelled})) return;
    const std::wstring path = env_wide(L"EF_CALIB_EXPORT");
    const std::wstring label_w = env_wide(L"EF_CALIB_LABEL", 64);
    if (path.empty() || label_w.empty()) return;  // kanca kapali
    const std::string label_s = platform::to_utf8(label_w);
    core::diagnosis::ComputeClass label{};
    bool label_ok = false;
    for (const auto c : {core::diagnosis::ComputeClass::GpuBound, core::diagnosis::ComputeClass::CpuOrEngineBound,
                         core::diagnosis::ComputeClass::FrameCapOrPresentWait, core::diagnosis::ComputeClass::Mixed,
                         core::diagnosis::ComputeClass::Healthy, core::diagnosis::ComputeClass::Insufficient})
        if (label_s == core::diagnosis::to_string(c)) {
            label = c;
            label_ok = true;
        }
    if (!label_ok) return;

    std::map<std::string, double> m;
    for (const auto& s : rep.subtests)
        if (s.id == "scene")
            for (const auto& [k, v] : s.metrics) m[k] = v;
    const auto get = [&](const char* k) -> std::optional<double> {
        const auto it = m.find(k);
        if (it == m.end() || !std::isfinite(it->second)) return std::nullopt;
        return it->second;
    };
    core::diagnosis::SceneCalibrationSample sample;
    sample.label = label;
    // EF-X20: v2 alanlari yalniz operator acikca verdiginde (etiket yontemi zorunlu; rule_derived yok).
    if (const auto lm = platform::to_utf8(env_wide(L"EF_CALIB_LABEL_METHOD", 32)); !lm.empty()) {
        if (lm == "controlled_experiment") sample.label_method = core::diagnosis::LabelMethod::ControlledExperiment;
        else if (lm == "expert_review") sample.label_method = core::diagnosis::LabelMethod::ExpertReview;
        else return;  // gecersiz/rule_derived: yazma yok
        sample.machine_group = platform::to_utf8(env_wide(L"EF_CALIB_MACHINE_GROUP", 65));
        sample.run_group = platform::to_utf8(env_wide(L"EF_CALIB_RUN_GROUP", 65));
        // EF-X20 dogrulama bulgusu 3: istege bagli, operatorun verdigi opak ozet. Otomatik donanim parmak izi
        // toplanmaz; yoksa ornek yazilir ama kapiya uygun sayilmaz. Gecersiz bicim -> yazma yok.
        sample.machine_digest = platform::to_utf8(env_wide(L"EF_CALIB_MACHINE_DIGEST", 65));
        if (const auto fl = platform::to_utf8(env_wide(L"EF_CALIB_FINE_LABEL", 33)); !fl.empty()) {
            const auto parsed = core::diagnosis::calibration_label_from(fl);
            if (!parsed) return;  // bilinmeyen ince etiket: yazma yok
            sample.fine_label = *parsed;
        }
    }
    if (env_wide(L"EF_CALIB_PROVENANCE", 16) == L"synthetic")
        sample.provenance = core::diagnosis::Provenance::SyntheticFixture;
    static std::atomic<std::uint64_t> seq{0};
    sample.id = "app-" + std::to_string(now_unix_ms()) + "-" + std::to_string(seq.fetch_add(1));
    sample.avg_fps = get("scene_avg_fps");
    sample.p99_ms = get("scene_p99_ms");
    sample.frames = get("scene_frames");
    sample.gpu_busy_ms = get("scene_gpu_busy_ms");  // yalniz zaman damgasi olculduyse (runner yazar)
    // MEAS-03: yalniz coznurlugu yeterli ortalama aralikta kullanim degerleri kanittir.
    if (scene_interval_ms && *scene_interval_ms > 0 && *scene_interval_ms <= core::diagnosis::kMaxBenchmarkSampleIntervalMs) {
        if (scene_gpu.has_value()) sample.gpu_engine_3d_pct = *scene_gpu.value();
        if (scene_cpu_core.has_value()) sample.cpu_max_core_pct = *scene_cpu_core.value();
    }

    const auto p = std::filesystem::path(path);
    std::string existing;
    {
        std::ifstream in(p, std::ios::binary);
        if (in) existing.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    auto next = core::diagnosis::append_sample_json(existing, sample);
    if (!next.ok()) return;  // bozuk mevcut dosya veya gecersiz ornek: yazma yok
    // Gecici dosya + atomik degistirme: yarim yazilmis veri seti birakilmaz.
    const auto tmp = p.wstring() + L".tmp";
    {
        std::ofstream out(std::filesystem::path(tmp), std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(next.value().data(), static_cast<std::streamsize>(next.value().size()));
        out.flush();
        if (!out.good()) {
            out.close();
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(tmp), ec);
            return;
        }
    }
    MoveFileExW(tmp.c_str(), p.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

inline std::string capture_app_id(std::string_view game_id) {
    std::string s = "game";
    if (game_id.empty() || game_id.size() > 64) return s;
    for (const char c : game_id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return s;
    s += "_";
    s += game_id;
    return s;
}

}  // namespace ef::app::detail
