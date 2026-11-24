// Uyumluluk B2 (bosluk 4, 5): ikinci baslatma, cikista tek ornek kilidi ve yonetici isi birakma kurallari.
#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/instance_policy.h"
#include "ef_test.h"
#include "platform/windows/admin_exit_gate.h"
#include "platform/windows/single_instance.h"

using namespace ef;

namespace {
struct FakeClock {
    std::uint64_t t = 0;
};

core::RelaunchOps fake_ops(FakeClock& clk, std::vector<core::ActivateTry> activations, int acquire_on_call, int& activate_calls,
                           int& wait_calls) {
    core::RelaunchOps ops;
    ops.activate = [activations, &activate_calls]() {
        const auto i = static_cast<std::size_t>(activate_calls++);
        return i < activations.size() ? activations[i] : activations.back();
    };
    ops.wait_lock = [&clk, acquire_on_call, &wait_calls](std::uint32_t ms) {
        clk.t += ms;
        return ++wait_calls == acquire_on_call;
    };
    ops.now_ms = [&clk] { return clk.t; };
    return ops;
}

std::wstring test_mutex_name(const wchar_t* tag) {
    return L"Local\\EcuFps.Test.B2." + std::wstring(tag) + L"." + std::to_wstring(GetCurrentProcessId());
}
}  // namespace

EF_TEST(b2_second_launch_activates_responsive_instance) {
    FakeClock clk;
    int a = 0, w = 0;
    const auto r = core::resolve_second_launch(fake_ops(clk, {core::ActivateTry::Activated}, -1, a, w));
    EF_CHECK(r == core::RelaunchOutcome::Activated);
    EF_CHECK(a == 1 && w == 0);  // kilit beklenmez
}

EF_TEST(b2_second_launch_waits_for_exiting_instance_then_starts) {
    // Cikmakta olan ornek: pencere yok / yanit yok; kilit 3. dilimde birakilir -> normal baslangic.
    FakeClock clk;
    int a = 0, w = 0;
    const auto r = core::resolve_second_launch(
        fake_ops(clk, {core::ActivateTry::NotResponding, core::ActivateTry::NoWindow}, 3, a, w), 15000, 500);
    EF_CHECK(r == core::RelaunchOutcome::Acquired);
    EF_CHECK(w == 3 && a == 3);
    EF_CHECK(clk.t == 1500);
}

EF_TEST(b2_second_launch_gives_up_after_budget) {
    // Kilit hic birakilmazsa ikinci ornek acilmaz (es zamanli iki yazici yok); bekleme butceyle sinirli.
    FakeClock clk;
    int a = 0, w = 0;
    const auto r = core::resolve_second_launch(fake_ops(clk, {core::ActivateTry::NotResponding}, -1, a, w), 15000, 500);
    EF_CHECK(r == core::RelaunchOutcome::GaveUp);
    EF_CHECK(clk.t >= 15000 && clk.t < 15000 + 500);
}

EF_TEST(b2_second_launch_activates_when_starting_instance_creates_window) {
    // Ilk ornek henuz pencere kurmadiysa (NoWindow) beklenir; pencere gelince one getirilir.
    FakeClock clk;
    int a = 0, w = 0;
    const auto r = core::resolve_second_launch(
        fake_ops(clk, {core::ActivateTry::NoWindow, core::ActivateTry::NoWindow, core::ActivateTry::Activated}, -1, a, w));
    EF_CHECK(r == core::RelaunchOutcome::Activated);
    EF_CHECK(a == 3 && w == 2);
}

EF_TEST(b2_second_launch_gives_up_at_once_when_blocked) {
    // UIPI reddi (yonetici ornek): beklemek sonucu degistirmez; 15 sn gorunmez bekleme olmaz.
    FakeClock clk;
    int a = 0, w = 0;
    const auto r = core::resolve_second_launch(fake_ops(clk, {core::ActivateTry::Blocked}, -1, a, w), 15000, 500);
    EF_CHECK(r == core::RelaunchOutcome::GaveUp);
    EF_CHECK(a == 1 && w == 0 && clk.t == 0);
}

EF_TEST(b2_activate_reply_only_known_value_counts) {
    EF_CHECK(core::classify_activate_reply(true, core::kActivateReply, false) == core::ActivateTry::Activated);
    EF_CHECK(core::classify_activate_reply(true, 0, false) == core::ActivateTry::NotResponding);  // DefWindowProc
    EF_CHECK(core::classify_activate_reply(true, 7, false) == core::ActivateTry::NotResponding);
    EF_CHECK(core::classify_activate_reply(false, 0, false) == core::ActivateTry::NotResponding);  // zaman asimi / olu
    EF_CHECK(core::classify_activate_reply(false, 0, true) == core::ActivateTry::Blocked);         // UIPI
}

namespace {
LRESULT CALLBACK reply_zero_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == platform::activate_message_id()) return 0;  // mesaji tanimayan pencere (cikmakta olan ornegin MainWindow'u)
    return DefWindowProcW(h, m, w, l);
}
LRESULT CALLBACK reply_known_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == platform::activate_message_id()) return static_cast<LRESULT>(core::kActivateReply);
    return DefWindowProcW(h, m, w, l);
}
HWND make_fake_window(const wchar_t* cls, WNDPROC proc) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    // Yalniz mesaj penceresi: ekranda gorunmez, FindWindow ile urun sinifi olarak bulunmaz.
    return CreateWindowExW(0, cls, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
}
}  // namespace

EF_TEST(b2_activate_window_requires_known_reply) {
    // Inceleme: cikmakta olan ornegin gizli penceresi mesaji DefWindowProc ile 0 yanitlarsa "Activated" sayilmamali
    // (ikinci ornek kapanir ve hicbir pencere acilmazdi).
    const HWND zero = make_fake_window(L"EcuFps.Test.B2.ReplyZero", reply_zero_proc);
    const HWND known = make_fake_window(L"EcuFps.Test.B2.ReplyKnown", reply_known_proc);
    EF_CHECK(zero != nullptr && known != nullptr);
    EF_CHECK(platform::activate_window(zero, 500) == core::ActivateTry::NotResponding);
    EF_CHECK(platform::activate_window(known, 500) == core::ActivateTry::Activated);
    EF_CHECK(platform::activate_window(nullptr, 500) == core::ActivateTry::NoWindow);
    if (zero) DestroyWindow(zero);
    if (known) DestroyWindow(known);
}

EF_TEST(b2_exit_lock_release_and_admin_decision) {
    EF_CHECK(core::release_instance_early(false));
    EF_CHECK(!core::release_instance_early(true));
    EF_CHECK(core::admin_exit_decision(true, 0) == core::AdminExitWait::Joined);
    EF_CHECK(core::admin_exit_decision(true, 1) == core::AdminExitWait::Joined);
    EF_CHECK(core::admin_exit_decision(false, 1) == core::AdminExitWait::LeaveToBroker);
    EF_CHECK(core::admin_exit_decision(false, 0) == core::AdminExitWait::KeepWaiting);  // yerel yazma: beklenir
    static_assert(core::kAdminExitWaitMs > 0 && core::kAdminExitWaitMs <= 30000);
}

EF_TEST(b2_admin_exit_gate_counts_calls_and_guards_decision) {
    platform::AdminExitGate g;  // surec geneli kapiya dokunulmaz
    EF_CHECK(!g.abandoned() && g.active_calls() == 0);
    int ran = 0;
    EF_CHECK(!g.run_if_in_call([&] { ++ran; }));
    {
        platform::AdminExitGate::Call c1(g);
        {
            platform::AdminExitGate::Call c2(g);  // ic ice (hizmet -> broker dususu)
            EF_CHECK(g.active_calls() == 2);
        }
        EF_CHECK(g.active_calls() == 1);
        EF_CHECK(g.run_if_in_call([&] { ++ran; }));
    }
    EF_CHECK(ran == 1 && g.active_calls() == 0);
    g.abandon();
    EF_CHECK(g.abandoned());
}

EF_TEST(b2_admin_exit_gate_call_cannot_end_during_decision) {
    // Karar kilit altindayken cagri bitemez: sonlandirma karari ile "cagridan cikip yerel yazmaya gecme" yarismaz.
    platform::AdminExitGate g;
    auto call = std::make_unique<platform::AdminExitGate::Call>(g);
    std::atomic<bool> ended{false};
    bool ended_inside = true;
    std::thread t;
    EF_CHECK(g.run_if_in_call([&] {
        t = std::thread([&] {
            call.reset();
            ended = true;
        });
        Sleep(100);
        ended_inside = ended.load();
    }));
    t.join();
    EF_CHECK(!ended_inside);
    EF_CHECK(ended.load() && g.active_calls() == 0);
}

EF_TEST(b2_single_instance_wait_acquire_after_release) {
    const auto name = test_mutex_name(L"rel");
    std::atomic<bool> held{false}, release{false};
    std::thread owner([&] {
        bool already = true;
        auto inst = platform::SingleInstance::acquire(already, name);
        held = inst != nullptr && !already;
        while (!release) Sleep(5);
        inst.reset();  // ReleaseMutex sahibi is parcaciginda
    });
    for (int i = 0; i < 200 && !held; ++i) Sleep(5);
    EF_CHECK(held.load());
    bool already = false;
    EF_CHECK(platform::SingleInstance::acquire(already, name) == nullptr);
    EF_CHECK(already);
    EF_CHECK(platform::SingleInstance::wait_acquire(50, name) == nullptr);  // hala tutuluyor: sinirli bekleme doner
    release = true;
    auto mine = platform::SingleInstance::wait_acquire(5000, name);
    EF_CHECK(mine != nullptr);
    owner.join();
}

EF_TEST(b2_single_instance_wait_acquire_abandoned_lock) {
    // Onceki surec kilidi birakmadan sonlandiysa (cikista TerminateProcess) yeni ornek kilidi alir.
    const auto name = test_mutex_name(L"abn");
    HANDLE raw = nullptr;
    std::thread owner([&] { raw = CreateMutexW(nullptr, TRUE, name.c_str()); });  // birakmadan biter
    owner.join();
    EF_CHECK(raw != nullptr);
    auto mine = platform::SingleInstance::wait_acquire(2000, name);
    EF_CHECK(mine != nullptr);
    mine.reset();
    if (raw) CloseHandle(raw);
}
