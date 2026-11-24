#include "platform/windows/metrics/raw_mouse.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstddef>

namespace ef::platform::metrics {

namespace {

constexpr wchar_t kClass[] = L"EcuFpsRawMouseProbe";

struct Ctx {
    std::vector<std::int64_t> ticks;
};

inline std::int64_t qpc_now() {
    LARGE_INTEGER v{};
    QueryPerformanceCounter(&v);
    return v.QuadPart;
}

inline bool is_move(const RAWINPUT& ri) {
    if (ri.header.dwType != RIM_TYPEMOUSE) return false;
    const RAWMOUSE& m = ri.data.mouse;
    return (m.usFlags & MOUSE_MOVE_ABSOLUTE) == 0 && (m.lLastX != 0 || m.lLastY != 0);
}

// NEXTRAWINPUTBLOCK (QWORD tanimi LEAN_AND_MEAN'de yok): kayit boyutu 8 bayta hizalanir.
inline RAWINPUT* next_block(RAWINPUT* p) {
    return reinterpret_cast<RAWINPUT*>((reinterpret_cast<ULONG_PTR>(p) + p->header.dwSize + 7) & ~static_cast<ULONG_PTR>(7));
}

LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUT) {
        auto* ctx = reinterpret_cast<Ctx*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (ctx) {
            const std::int64_t t = qpc_now();  // toplu okumadaki tum kayitlar bu damgayi paylasir
            alignas(8) BYTE one[128];
            UINT sz = sizeof one;
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, one, &sz, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
                is_move(*reinterpret_cast<RAWINPUT*>(one)))
                ctx->ticks.push_back(t);
            // Birikmis kayitlar (yuksek hizda WM_INPUT birlesir): tampon bos kalana dek oku.
            alignas(8) BYTE buf[4096];
            for (;;) {
                UINT bsz = sizeof buf;
                const UINT n = GetRawInputBuffer(reinterpret_cast<RAWINPUT*>(buf), &bsz, sizeof(RAWINPUTHEADER));
                if (n == 0 || n == static_cast<UINT>(-1)) break;
                RAWINPUT* p = reinterpret_cast<RAWINPUT*>(buf);
                for (UINT i = 0; i < n; ++i, p = next_block(p))
                    if (is_move(*p)) ctx->ticks.push_back(t);
            }
        }
        return DefWindowProcW(h, msg, wp, lp);  // Raw Input belgesi: islem sonrasi DefWindowProc
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

std::optional<RawMouseCapture> capture_raw_mouse(int duration_ms, const std::atomic<bool>& cancel) {
    LARGE_INTEGER f{};
    if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) return std::nullopt;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = kClass;
    const bool registered = RegisterClassW(&wc) != 0;
    if (!registered && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return std::nullopt;
    HWND h = CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
    if (!h) {
        if (registered) UnregisterClassW(kClass, inst);
        return std::nullopt;
    }
    Ctx ctx;
    ctx.ticks.reserve(16384);
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;  // genel masaustu
    rid.usUsage = 0x02;      // fare
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = h;
    std::optional<RawMouseCapture> out;
    if (RegisterRawInputDevices(&rid, 1, sizeof rid)) {
        const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(duration_ms < 0 ? 0 : duration_ms);
        while (!cancel.load()) {
            const ULONGLONG now = GetTickCount64();
            if (now >= end) break;
            const DWORD wait = static_cast<DWORD>(end - now < 50 ? end - now : 50);
            MsgWaitForMultipleObjects(0, nullptr, FALSE, wait, QS_RAWINPUT | QS_ALLINPUT);
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        rid.dwFlags = RIDEV_REMOVE;
        rid.hwndTarget = nullptr;
        RegisterRawInputDevices(&rid, 1, sizeof rid);  // kaydi kaldir
        if (!cancel.load()) {
            RawMouseCapture cap;
            cap.qpc_ticks = std::move(ctx.ticks);
            cap.qpc_freq = f.QuadPart;
            out = std::move(cap);
        }
    }
    SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    DestroyWindow(h);
    if (registered) UnregisterClassW(kClass, inst);
    return out;
}

}  // namespace ef::platform::metrics
