// ADR-033 / spec 3 ve 8: kurulum penceresi (Setup rolu). Durum makinesi core::setup::reduce, cizim ui::SetupView.
// Bu dosya yalniz pencere, is parcaciklari ve isletim sistemi cagrilarini baglar:
//  - denetim: app::upd::check_for_update (Ui ile ayni kod; EF_UPDATE_CHECK=0 ve gomulu anahtarsiz derleme atlar);
//  - daha yeni surum: imzali manifestteki url bellege indirilir, boyut + SHA-256 imzali manifestle karsilastirilir,
//    %LOCALAPPDATA%\EcuFps\setup\EcuFps-Setup-<surum>.exe yazilir, yazma paylasimi kapali tutulurken geri okunup
//    yeniden dogrulanir ve o dosya --setup ile (yukseltilmemis, sabit arguman) baslatilir; bu surec cikar;
//  - Kur: Ui acik ise "Ecu Fps acik"; degilse uygulama ici Evet/Hayir -> tek UAC -> SetupWorker (journal + geri alma);
//    ilerleme cerceveleri satirlara islenir; yalniz result.ok yanitinda "Kurulum tamamlandi";
//  - basaridan sonra HKCU Run degeri ve tercih bu yukseltilmemis surecte yazilir (spec 2: UAC baska yonetici hesabiyla
//    verilse de Run kaydi gercek kullaniciya gider); "Ecu Fps'i ac" kurulu EXE'yi argumansiz baslatir.
#include "app/worker.h"
#include "app/setup_app.h"

#include <windows.h>
#include <dwmapi.h>
#include <windowsx.h>

#include "ui/shell/window_fit.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "app/update_support.h"
#include "core/product_info.h"
#include "core/recovery/sha256.h"
#include "core/settings/autostart.h"
#include "core/settings/preferences.h"
#include "core/setup/layout.h"
#include "core/setup/setup_flow.h"
#include "core/update/release_key.h"
#include "core/update/update_check.h"
#include "platform/windows/elevation.h"
#include "platform/windows/install/setup_worker_channel.h"
#include "platform/windows/journal_fs.h"
#include "platform/windows/known_roots.h"
#include "platform/windows/net/update_http.h"
#include "platform/windows/run_key_store.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/win_env.h"
#include "ui/accessibility/uia.h"
#include "ui/brand/brand_resources.h"
#include "ui/render/graphics.h"
#include "ui/setup/setup_view.h"
#include "ui/theme/theme.h"

namespace ef::app {

namespace st = core::setup;
namespace ev = core::setup::ev;
using ui::Hit;

namespace {

constexpr wchar_t kSetupWindowClass[] = L"EcuFps.SetupWindow";
constexpr char kPrefsFile[] = "preferences.json";
constexpr UINT kMsgCall = WM_APP + 1;         // lParam: new std::function<void()> (is parcacigindan UI'ya)
constexpr UINT kMsgUiaActivate = WM_APP + 2;  // wParam: Hit + 1
constexpr UINT_PTR kAnimTimer = 1;
constexpr UINT kAnimIntervalMs = 16;

std::int64_t now_ms() { return static_cast<std::int64_t>(GetTickCount64()); }

std::string version_string() {
    const auto v = core::product_version();
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

// Indirilen dosya adinda yalniz rakam ve nokta (imzali manifest surumu; yine de yol parcasi olarak suzulur).
std::string safe_version(const std::string& v) {
    std::string out;
    for (char c : v)
        if ((c >= '0' && c <= '9') || c == '.') out += c;
    if (out.empty() || out.size() > 32 || out.find("..") != std::string::npos) return "new";
    return out;
}

// Yukseltilmemis, sabit argumanli baslatma. cwd bos olabilir.
bool start_process(const std::wstring& exe, const std::wstring& args, const std::wstring& cwd) {
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!args.empty()) cmd += L" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

class SetupWindow {
public:
    SetupWindow(HINSTANCE inst, const core::i18n::Catalog& cat, st::InstallLayout layout, HANDLE setup_mutex)
        : inst_(inst), cat_(cat), theme_(ui::default_theme()), view_(theme_, cat), layout_(std::move(layout)), mutex_(setup_mutex) {
        vm_ = st::initial_vm(version_string(), st::launched_from_portable(platform::normalized_self_exe(), layout_));
        install_dir_ = layout_.dir.wstring();
        state_.reduced_motion = false;
        state_.strip_live = false;  // sabit gradyan (serit zamanlayicisi yok)
        load_prefs();
    }
    ~SetupWindow() { stop_worker(); }

    bool create();
    int run();

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    float scale() const { return static_cast<float>(dpi_) / 96.f; }
    void invalidate() {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void paint();
    void post(std::function<void()> fn);
    void post_event(st::SetupEvent e) {
        post([this, e = std::move(e)] { dispatch(e); });
    }
    void dispatch(const st::SetupEvent& e);
    void after_change(const st::SetupVm& before);
    void activate(Hit h);
    void modal_input(ui::ModalKey k);
    void modal_finished();
    void set_focus(Hit h, bool visible);
    void key_down(WPARAM vk, bool repeat);
    void close_request();

    void stop_worker();
    void join_worker();
    void start_check();
    void start_download();
    void start_install();
    void handoff();
    void write_autostart();
    void open_installed();

    void load_prefs();
    bool save_prefs(bool with_autostart);  // with_autostart: bu kayit Windows ile baslatma alanlarini da yazar

    HINSTANCE inst_;
    const core::i18n::Catalog& cat_;
    const ui::Theme& theme_;
    ui::SetupView view_;
    st::InstallLayout layout_;
    HANDLE mutex_;
    std::wstring install_dir_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    std::unique_ptr<ui::Graphics> gfx_;
    std::shared_ptr<ui::UiaModel> uia_model_ = std::make_shared<ui::UiaModel>();
    std::unique_ptr<ui::UiaHost> uia_;
    ui::UiState state_;
    ui::HitMap hits_;
    ui::ModalState modal_;
    ui::ModalKeyGuard modal_guard_;
    Hit pressed_ = Hit::None;
    bool tracking_ = false;
    bool anim_timer_ = false;
    st::SetupVm vm_;
    core::settings::Preferences prefs_;
    std::optional<core::catalog::ReleaseManifest> manifest_;
    std::wstring downloaded_;
    HANDLE download_lock_ = INVALID_HANDLE_VALUE;  // indirilen dosyaya yazma/silme paylasimi kapali (devre kadar)
    std::thread worker_;
    std::atomic<bool> cancel_{false};
};

void SetupWindow::load_prefs() {
    if (const auto dir = platform::app_data_dir()) {
        platform::WinJournalFs fs(*dir);
        if (auto txt = fs.read(kPrefsFile); txt.ok())
            if (auto p = core::settings::Preferences::parse(txt.value()); p.ok()) prefs_ = std::move(p).value();
    }
}

bool SetupWindow::save_prefs(bool with_autostart) {
    const auto dir = platform::app_data_dir();
    if (!dir) return false;
    std::error_code ec;
    std::filesystem::create_directories(*dir, ec);
    platform::WinJournalFs fs(*dir);
    // Uyumluluk #26: kurulum acikken tepsi uygulamasi dili veya baska tercihleri degistirmis olabilir. Kayittan hemen
    // once diskteki guncel dosya okunur ve yalniz kurulumun alanlari yazilir (bayat kopya ile uzerine yazma yok).
    // Dosya yoksa ya da okunamiyorsa kurulumun kopyasi esas alinir (onceki davranis).
    if (auto txt = fs.read(kPrefsFile); txt.ok())
        if (auto p = core::settings::Preferences::parse(txt.value()); p.ok())
            prefs_ = core::settings::merge_setup_fields(std::move(p).value(), prefs_, with_autostart);
    const std::string text = prefs_.serialize();
    if (!fs.write_atomic(kPrefsFile, text).ok()) return false;
    const auto back = fs.read(kPrefsFile);
    return back.ok() && back.value() == text;
}

void SetupWindow::post(std::function<void()> fn) {
    auto* p = new std::function<void()>(std::move(fn));
    if (!hwnd_ || !PostMessageW(hwnd_, kMsgCall, 0, reinterpret_cast<LPARAM>(p))) delete p;
}

void SetupWindow::join_worker() {
    if (worker_.joinable()) worker_.join();
    cancel_ = false;
}

void SetupWindow::stop_worker() {
    cancel_ = true;
    platform::net::abort_update_requests();
    if (worker_.joinable()) worker_.join();
    if (download_lock_ != INVALID_HANDLE_VALUE) {
        CloseHandle(download_lock_);
        download_lock_ = INVALID_HANDLE_VALUE;
    }
}

void SetupWindow::dispatch(const st::SetupEvent& e) {
    const st::SetupVm before = vm_;
    vm_ = st::reduce(vm_, e);
    after_change(before);
    invalidate();
}

void SetupWindow::after_change(const st::SetupVm& before) {
    const bool phase_changed = before.phase != vm_.phase;
    if (phase_changed && before.phase == st::SetupPhase::Confirm) {
        state_.modal = nullptr;
        set_focus(Hit::None, state_.focus_visible);
    }
    if (phase_changed) {
        switch (vm_.phase) {
            case st::SetupPhase::Checking: start_check(); break;
            case st::SetupPhase::Downloading: start_download(); break;
            case st::SetupPhase::Confirm:
                modal_ = ui::modal_open(view_.confirm_spec());
                state_.modal = &modal_;
                state_.focus = ui::modal_hit(modal_.focus);
                view_.screen().open_modal_motion(state_, now_ms());
                break;
            case st::SetupPhase::Installing: start_install(); break;
            case st::SetupPhase::Done: write_autostart(); break;
            default: break;
        }
        if (uia_) {
            uia_->notify_structure_changed();
            if (state_.modal) uia_->notify_window_opened(Hit::Modal);
            uia_->announce(view_.announcement(vm_, install_dir_), vm_.phase == st::SetupPhase::Done || vm_.phase == st::SetupPhase::Failed);
        }
    }
    if (vm_.phase == st::SetupPhase::Downloading && vm_.handoff && !before.handoff) handoff();
    if (vm_.license_open != before.license_open) {
        state_.scroll = 0;
        set_focus(vm_.license_open ? Hit::SetupLicenseClose : Hit::SetupLicense, state_.focus_visible);
    }
}

void SetupWindow::start_check() {
    join_worker();
    manifest_.reset();
    if (upd::update_check_disabled_by_env() || !core::update::has_release_key()) {
        post_event(ev::CheckSkipped{});  // dogrulanamaz: ag kullanilmaz, "en guncel" denmez
        return;
    }
    const std::string version = vm_.version;
    const std::uint64_t floor = prefs_.release_manifest_seen;
    worker_ = spawn_worker([this, version, floor] {
        // Setup indirmeyi kendi kullanici klasorune yazar: "klasor yazilamaz" ayrimi yoktur.
        auto r = upd::check_for_update(version, floor, true, cancel_);
        if (!r) return;
        post([this, r = std::move(*r)]() mutable {
            if (const auto next = core::update::next_highest_seen(prefs_.release_manifest_seen, r.decision); next != prefs_.release_manifest_seen) {
                prefs_.release_manifest_seen = next;  // yalniz dogrulanmis manifestten sonra yukselir
                (void)save_prefs(false);
            }
            const auto outcome = r.decision.outcome;
            std::string newer;
            if (outcome == core::update::UpdateOutcome::Mandatory || outcome == core::update::UpdateOutcome::NotWritable) {
                newer = r.decision.manifest.version;
                manifest_ = std::move(r.decision.manifest);
            }
            dispatch(ev::CheckDone{outcome, newer});
        });
    });
}

void SetupWindow::start_download() {
    join_worker();
    if (!manifest_) {
        post_event(ev::DownloadDone{ev::DownloadResult::NetworkFailed});
        return;
    }
    const auto m = *manifest_;
    const auto dir = layout_.user_setup_dir;
    worker_ = spawn_worker([this, m, dir] {
        auto finish = [this](ev::DownloadResult r) { post_event(ev::DownloadDone{r}); };
        auto got = platform::net::update_get(m.url, core::update::kExeMaxBytes, 0, cancel_, [this, size = m.size](std::uint64_t done, std::uint64_t total) {
            const std::uint64_t all = total ? total : size;
            post_event(ev::DownloadProgress{all ? static_cast<double>(done) / static_cast<double>(all) : 0.0});
        });
        if (cancel_.load() || got.fail == platform::net::FetchFail::Cancelled) return finish(ev::DownloadResult::Cancelled);
        if (!got.ok()) return finish(ev::DownloadResult::NetworkFailed);
        // Imzali manifestle boyut + SHA-256; uyusmazsa hicbir dosya yazilmaz.
        if (!core::update::download_matches(got.body.size(), core::recovery::sha256_hex(got.body), m)) return finish(ev::DownloadResult::VerifyFailed);
        if (dir.empty()) return finish(ev::DownloadResult::NetworkFailed);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path file = dir / platform::to_wide("EcuFps-Setup-" + safe_version(m.version) + ".exe");
        const std::string file_u8 = platform::to_utf8(file.wstring());
        if (!upd::write_file(file_u8, got.body)) return finish(ev::DownloadResult::NetworkFailed);
        got.body.clear();
        got.body.shrink_to_fit();
        // Geri okumadan baslatmaya kadar dosyaya yazma / silme paylasimi kapali tutulur (okuma ve calistirma acik).
        HANDLE lock = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const auto back = lock != INVALID_HANDLE_VALUE ? upd::read_file(file_u8, core::update::kExeMaxBytes) : std::nullopt;
        if (!back || !core::update::download_matches(back->size(), core::recovery::sha256_hex(*back), m)) {
            if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
            DeleteFileW(file.c_str());  // dogrulanmayan dosya birakilmaz
            return finish(ev::DownloadResult::VerifyFailed);
        }
        post([this, lock, file] {
            if (download_lock_ != INVALID_HANDLE_VALUE) CloseHandle(download_lock_);
            download_lock_ = lock;
            downloaded_ = file.wstring();
            dispatch(ev::DownloadDone{ev::DownloadResult::Verified});
        });
    });
}

void SetupWindow::handoff() {
    // Yeni surumun penceresi kendi tek ornek kilidini alabilsin: kilit birakilir; baslatilamazsa geri alinir.
    if (mutex_) {
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
    const bool ok = !downloaded_.empty() && start_process(downloaded_, L"--setup", layout_.user_setup_dir.wstring());
    if (download_lock_ != INVALID_HANDLE_VALUE) {
        CloseHandle(download_lock_);
        download_lock_ = INVALID_HANDLE_VALUE;
    }
    if (ok) {
        DestroyWindow(hwnd_);
        return;
    }
    mutex_ = CreateMutexW(nullptr, TRUE, std::wstring(st::kSetupMutex).c_str());
    post_event(ev::LaunchFailed{});
}

void SetupWindow::start_install() {
    join_worker();
    worker_ = spawn_worker([this] {
        // Tek UAC (ShellElevationLauncher, --role=setup); isci sabit Install istegini journal + geri alma ile yurutur.
        auto run = platform::install::run_setup_worker_request(st::SetupKind::Install,
                                                               [this](const st::SetupProgress& p) { post_event(ev::WorkerProgress{p}); });
        // request_sent/worker_exit/detail: istek gitmeden biten hata NotStarted + gercek neden olarak gosterilir.
        post_event(ev::WorkerFinished{run.elevation, std::move(run.reply), run.request_sent, run.worker_exit, std::move(run.detail)});
    });
}

void SetupWindow::write_autostart() {
    // Spec 2 / ADR-030: HKCU Run "EcuFps" = "\"<kurulu exe>\" --tray"; yaz/sil + geri oku. Eski tasinabilir kaydin
    // ayni adli degeri kurulu yola yeniden yazilir (veya kapaliysa silinir). Tercih dosyasi ilk acilisin anahtari geri
    // acmamasi icin kaydedilir.
    platform::WinRunKeyStore store = platform::WinRunKeyStore::production();
    const auto cmd = core::settings::autostart_command(layout_.exe.wstring());
    const auto s = core::settings::autostart_set(store, prefs_, vm_.autostart, cmd);
    const bool prefs_ok = save_prefs(true);
    platform::append_autostart_log(core::settings::autostart_set_trace("setup done", vm_.autostart, s) +
                                   (prefs_ok ? "" : " prefs_save=failed"));
    const bool ok = prefs_ok && s.failure == core::settings::AutostartFailure::None && s.on == vm_.autostart;
    dispatch(ev::AutostartResult{ok});
}

void SetupWindow::open_installed() {
    // Kurulu EXE argumansiz, yukseltilmemis baslatilir (spec 3); bu pencere kapanir.
    if (start_process(layout_.exe.wstring(), L"", layout_.dir.wstring())) DestroyWindow(hwnd_);
}

void SetupWindow::close_request() {
    if (!st::setup_can_close(vm_)) return;  // kurulum surerken kapatma yok: sonuc isciden gelir
    if (state_.modal) {
        modal_input(ui::ModalKey::Escape);
        return;
    }
    DestroyWindow(hwnd_);
}

void SetupWindow::set_focus(Hit h, bool visible) {
    state_.focus = h;
    state_.focus_visible = visible;
    uia_model_->focus = h;
    if (uia_ && h != Hit::None) uia_->notify_focus(h);
}

void SetupWindow::modal_finished() {
    if (!modal_.done) return;
    const bool yes = modal_.result.answer == ui::ModalAnswer::Yes;
    dispatch(ev::ConfirmAnswer{yes});
}

void SetupWindow::modal_input(ui::ModalKey k) {
    ui::modal_key(modal_, k);
    state_.focus = ui::modal_hit(modal_.focus);
    state_.focus_visible = true;
    invalidate();
    modal_finished();
}

void SetupWindow::activate(Hit h) {
    if (state_.modal) {
        const auto f = ui::modal_focus_of(h);
        if (f.item == ui::ModalItem::None) return;
        ui::modal_activate(modal_, f);
        invalidate();
        modal_finished();
        return;
    }
    switch (h) {
        case Hit::SetupCancel:
        case Hit::SetupClose: close_request(); break;
        case Hit::SetupInstall: dispatch(ev::InstallClicked{platform::ui_instance_running()}); break;
        case Hit::SetupRetry: dispatch(ev::Retry{platform::ui_instance_running()}); break;
        case Hit::SetupInstallAnyway: dispatch(ev::InstallAnyway{platform::ui_instance_running()}); break;
        case Hit::SetupStop:
            cancel_ = true;
            platform::net::abort_update_requests();  // bekleyen ag cagrisi hemen doner
            dispatch(ev::DownloadStop{});
            break;
        case Hit::SetupOpen: open_installed(); break;
        case Hit::SetupAutostart: dispatch(ev::ToggleAutostart{}); break;
        case Hit::SetupLicense:
        case Hit::SetupLicenseClose: dispatch(ev::ToggleLicense{}); break;
        default: break;
    }
}

void SetupWindow::key_down(WPARAM vk, bool repeat) {
    if (modal_guard_.swallow(static_cast<unsigned>(vk), repeat)) return;
    if (state_.modal) {
        switch (vk) {
            case VK_TAB: modal_input(GetKeyState(VK_SHIFT) < 0 ? ui::ModalKey::ShiftTab : ui::ModalKey::Tab); break;
            case VK_ESCAPE: modal_input(ui::ModalKey::Escape); break;
            case VK_RETURN:
                if (!repeat) modal_input(ui::ModalKey::Enter);
                break;
            case VK_SPACE:
                if (!repeat) modal_input(ui::ModalKey::Space);
                break;
            case VK_LEFT: modal_input(ui::ModalKey::Left); break;
            case VK_RIGHT: modal_input(ui::ModalKey::Right); break;
            default: return;
        }
        if (!state_.modal) modal_guard_.closed_by(static_cast<unsigned>(vk));
        return;
    }
    switch (vk) {
        case VK_TAB: {
            const auto intent = ui::setup_focus_order(vm_, state_);
            const auto real = ui::setup_focus_order(vm_, state_, hits_);
            set_focus(ui::focus_step(intent, real, state_.focus, GetKeyState(VK_SHIFT) < 0 ? -1 : 1), true);
            invalidate();
            break;
        }
        case VK_RETURN:
        case VK_SPACE:
            if (!repeat && hits_.activatable(state_.focus)) activate(state_.focus);
            break;
        case VK_ESCAPE:
            if (vm_.license_open) dispatch(ev::ToggleLicense{});
            else close_request();
            break;
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
            if (vm_.license_open) {
                const float step = (vk == VK_UP || vk == VK_DOWN) ? 48.f : 240.f;
                state_.scroll += (vk == VK_UP || vk == VK_PRIOR) ? -step : step;
                invalidate();
            }
            break;
        default: break;
    }
}

void SetupWindow::paint() {
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (!gfx_ || !gfx_->bind_dc(ps.hdc, rc.right, rc.bottom, dpi_) || !gfx_->begin()) {
        EndPaint(hwnd_, &ps);
        return;
    }
    const float w = static_cast<float>(rc.right) / scale();
    const float h = static_cast<float>(rc.bottom) / scale();
    const auto now = now_ms();
    hits_ = view_.paint(*gfx_, w, h, state_, vm_, install_dir_, now);
    uia_model_->hits = hits_;
    uia_model_->scale = scale();
    uia_model_->focus = state_.focus;
    const bool ok = gfx_->end();
    EndPaint(hwnd_, &ps);
    if (!ok) invalidate();
    const bool anim = view_.screen().animating(state_, now, false);
    if (anim && !anim_timer_) anim_timer_ = SetTimer(hwnd_, kAnimTimer, kAnimIntervalMs, nullptr) != 0;
}

bool SetupWindow::create() {
    gfx_ = ui::Graphics::create(theme_, cat_.locale() == core::i18n::Locale::TrTR ? L"tr-TR" : L"en-US");
    if (!gfx_) return false;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    static const HBRUSH bg = CreateSolidBrush(RGB(static_cast<int>(theme_.dark.background.r * 255.f + 0.5f),
                                                  static_cast<int>(theme_.dark.background.g * 255.f + 0.5f),
                                                  static_cast<int>(theme_.dark.background.b * 255.f + 0.5f)));
    wc.hbrBackground = bg;
    wc.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(IDI_ECUFPS_APP));
    wc.lpszClassName = kSetupWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const UINT dpi = GetDpiForSystem();
    RECT r{0, 0, MulDiv(static_cast<int>(theme_.setup.window_w), static_cast<int>(dpi), 96),
           MulDiv(static_cast<int>(theme_.setup.window_h), static_cast<int>(dpi), 96)};
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
    const int ww = r.right - r.left, wh = r.bottom - r.top;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + std::max(0, static_cast<int>((work.right - work.left - ww) / 2));
    const int y = work.top + std::max(0, static_cast<int>((work.bottom - work.top - wh) / 2));
    hwnd_ = CreateWindowExW(0, kSetupWindowClass, view_.window_title().c_str(), style, x, y, ww, wh, nullptr, nullptr, inst_, this);
    return hwnd_ != nullptr;
}

int SetupWindow::run() {
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    UpdateWindow(hwnd_);
    start_check();
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    stop_worker();
    // Pencere kapandiktan sonra kuyrukta kalan cagrilar serbest birakilir (calistirilmaz).
    while (PeekMessageW(&msg, nullptr, kMsgCall, kMsgCall, PM_REMOVE)) delete reinterpret_cast<std::function<void()>*>(msg.lParam);
    if (mutex_) {
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
    return core::kExitOk;
}

LRESULT CALLBACK SetupWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<SetupWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<SetupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->handle(msg, wp, lp);
}

LRESULT SetupWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            dpi_ = GetDpiForWindow(hwnd_);
            const HWND hw = hwnd_;  // Uyumluluk B6 (bosluk 27): 20 reddedilirse eski Windows 10 icin 19
            (void)ui::apply_dark_caption([hw](unsigned attr) {
                const BOOL dark = TRUE;
                return static_cast<long>(DwmSetWindowAttribute(hw, attr, &dark, sizeof(dark)));
            });
            uia_model_->hwnd = hwnd_;
            uia_model_->window_name = view_.window_title();
            const HWND self = hwnd_;
            uia_model_->activate = [self](Hit h) { PostMessageW(self, kMsgUiaActivate, static_cast<WPARAM>(static_cast<int>(h) + 1), 0); };
            uia_ = std::make_unique<ui::UiaHost>(uia_model_);
            return 0;
        }
        case WM_GETOBJECT:
            if (uia_)
                if (const LRESULT r = uia_->on_get_object(wp, lp)) return r;
            break;
        case kMsgCall: {
            std::unique_ptr<std::function<void()>> fn(reinterpret_cast<std::function<void()>*>(lp));
            if (fn && *fn) (*fn)();
            return 0;
        }
        case kMsgUiaActivate: {
            const Hit h = static_cast<Hit>(static_cast<int>(wp) - 1);
            const bool modal_item = h == Hit::Modal || ui::modal_focus_of(h).item != ui::ModalItem::None;
            if (modal_item != (state_.modal != nullptr) || h == Hit::Modal) return 0;
            if (!hits_.activatable(h)) return 0;
            set_focus(h, true);
            activate(h);
            invalidate();
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint(); return 0;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            const auto* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            invalidate();
            return 0;
        }
        case WM_TIMER:
            if (wp == kAnimTimer) {
                if (!view_.screen().animating(state_, now_ms(), false)) {
                    KillTimer(hwnd_, kAnimTimer);
                    anim_timer_ = false;
                }
                invalidate();
            }
            return 0;
        case WM_MOUSEMOVE: {
            if (!tracking_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                tracking_ = TrackMouseEvent(&tme) != FALSE;
            }
            const Hit h = hits_.at(static_cast<float>(GET_X_LPARAM(lp)) / scale(), static_cast<float>(GET_Y_LPARAM(lp)) / scale());
            if (h != state_.hover) {
                view_.screen().set_hover(state_, h, now_ms());
                invalidate();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            tracking_ = false;
            view_.screen().set_hover(state_, Hit::None, now_ms());
            invalidate();
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, state_.hover != Hit::None ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_LBUTTONDOWN:
            pressed_ = hits_.at(static_cast<float>(GET_X_LPARAM(lp)) / scale(), static_cast<float>(GET_Y_LPARAM(lp)) / scale());
            if (pressed_ != Hit::None) SetCapture(hwnd_);
            view_.screen().set_pressed(state_, pressed_, now_ms());
            invalidate();
            return 0;
        case WM_LBUTTONUP: {
            const Hit h = hits_.at(static_cast<float>(GET_X_LPARAM(lp)) / scale(), static_cast<float>(GET_Y_LPARAM(lp)) / scale());
            const Hit down = pressed_;
            pressed_ = Hit::None;
            view_.screen().set_pressed(state_, Hit::None, now_ms());
            if (GetCapture() == hwnd_) ReleaseCapture();
            if (h != Hit::None && h == down) {
                if (!state_.modal) set_focus(h, false);
                activate(h);
            }
            invalidate();
            return 0;
        }
        case WM_CAPTURECHANGED:
            pressed_ = Hit::None;
            view_.screen().set_pressed(state_, Hit::None, now_ms());
            invalidate();
            return 0;
        case WM_MOUSEWHEEL:
            if (vm_.license_open && !state_.modal) {
                state_.scroll -= static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * 48.f;
                invalidate();
            }
            return 0;
        case WM_KEYDOWN:
            key_down(wp, (lp & 0x40000000) != 0);
            return 0;
        case WM_CLOSE: close_request(); return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, kAnimTimer);
            if (uia_) uia_->disconnect();
            if (gfx_) gfx_->release_target();
            cancel_ = true;
            platform::net::abort_update_requests();
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

bool exe_outside_install_dir() {
    return !st::is_installed_location(platform::normalized_self_exe(), st::make_layout(platform::known_roots()));
}

}  // namespace

bool setup_role_disabled_by_env() {
    wchar_t v[8]{};
    return GetEnvironmentVariableW(L"EF_SETUP_ROLE", v, 8) == 1 && v[0] == L'0';
}

bool install_offer_applies() { return st::kSetupRoleEnabled && !setup_role_disabled_by_env() && exe_outside_install_dir(); }

bool launch_self_setup() {
    const auto self = platform::self_exe_path();
    return !self.empty() && start_process(self.wstring(), core::build_launch_args(core::LaunchArgs{core::Role::Setup}), self.parent_path().wstring());
}

int run_setup(HINSTANCE instance, const core::LaunchArgs&, const core::i18n::Catalog& catalog) {
    // Denetim 4.1: kurulum penceresi ve isci is parcaciklari da yerel dump yazar (yukseltilmemis, kullanici klasoru).
    if (const auto dir = platform::app_data_dir()) platform::install_crash_handler({*dir / L"crash", 3});
    platform::guard_current_thread();
    // Ayri tek ornek kilidi (spec 3): ikinci kurulum penceresi mevcut olani one getirir.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, std::wstring(st::kSetupMutex).c_str());
    if (!mutex) return core::kExitBadArgs;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        if (HWND other = FindWindowW(kSetupWindowClass, nullptr)) {
            ShowWindow(other, IsIconic(other) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(other);
        }
        return core::kExitAlreadyRunning;
    }
    auto win = std::make_unique<SetupWindow>(instance, catalog, st::make_layout(platform::known_roots()), mutex);
    if (!win->create()) return 6;
    return win->run();
}

}  // namespace ef::app
