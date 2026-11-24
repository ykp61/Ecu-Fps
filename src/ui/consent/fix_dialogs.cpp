#include "ui/consent/fix_dialogs.h"

#include <iterator>

#include "core/adapters/windows/dns.h"
#include "core/net/server_ping.h"
#include "core/service/uac_copy.h"
#include "ui/shell/main_window.h"

namespace ef::ui {

namespace {
std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Eski TaskDialog pencere basligi ("Ecu Fps — Oyun hazirligi"): ekranda yoktur, kip penceresinin UIA HelpText'inde okunur.
std::wstring fix_caption(const core::i18n::Catalog& cat) {
    using core::i18n::StringId;
    return widen(cat.get(StringId::AppTitle)) + L" — " + widen(cat.get(StringId::NavReadiness));
}
}  // namespace

ModalSpec fix_modal_spec(const core::i18n::Catalog& cat, const std::wstring& question, const std::wstring& detail, FixDialogIcon icon) {
    using core::i18n::StringId;
    ModalSpec spec;
    spec.caption = fix_caption(cat);
    spec.title = question;
    if (!detail.empty()) spec.body.push_back(detail);
    spec.yes = widen(cat.get(StringId::ActionConfirm));
    spec.no = widen(cat.get(StringId::ActionCancel));
    spec.destructive = icon == FixDialogIcon::Warning;  // RDY-05: geri donussuz islem -> ana dugme hata renginde
    return spec;
}

ModalSpec fix_apply_modal_spec(const core::i18n::Catalog& cat, const std::wstring& change, const std::wstring& finding,
                               const std::wstring& detail) {
    using core::i18n::StringId;
    ModalSpec spec;
    spec.caption = fix_caption(cat);
    if (!finding.empty()) spec.caption += L" — " + finding;  // bulgu cumlesi: ekranda degisiklik satiri var, tamami UIA'da
    spec.title = widen(cat.get(StringId::ChkApplyQuestion));
    if (const auto& first = change.empty() ? finding : change; !first.empty()) spec.body.push_back(first);
    std::wstring note = widen(cat.get(StringId::ChkApplyReversible));
    if (!detail.empty()) note += L" " + detail;
    spec.body.push_back(std::move(note));
    spec.yes = widen(cat.get(StringId::ActionConfirm));
    spec.no = widen(cat.get(StringId::ActionCancel));
    return spec;
}

ModalSpec fix_all_modal_spec(const core::i18n::Catalog& cat, const std::vector<std::wstring>& changes, const std::wstring& detail) {
    using core::i18n::StringId;
    ModalSpec spec;
    spec.caption = fix_caption(cat);
    spec.title = widen(cat.get(StringId::ChkApplyQuestion));
    spec.list = changes;
    std::wstring note = widen(cat.get(StringId::ChkApplyReversible));
    if (!detail.empty()) note += L" " + detail;
    spec.body.push_back(std::move(note));
    spec.yes = widen(cat.get(StringId::ActionConfirm));
    spec.no = widen(cat.get(StringId::ActionCancel));
    return spec;
}

std::wstring fix_change_line(const core::i18n::Catalog& cat, const core::readiness::Check& c) {
    using core::i18n::StringId;
    if (c.action != core::readiness::ActionKind::AutoFix) return {};
    const auto get = [&](StringId id) { return widen(cat.get(id)); };
    const std::wstring on = get(StringId::HistoryValueOn), off = get(StringId::HistoryValueOff);
    std::wstring name, from, to;
    const auto toggle = [&](StringId id, bool turns_on) { name = get(id), from = turns_on ? off : on, to = turns_on ? on : off; };
    const auto& k = c.action_arg;
    if (k == "fix.refresh_rate") {
        const auto hz = c.args.find("hz"), mx = c.args.find("max");
        if (hz == c.args.end() || mx == c.args.end() || hz->second.empty() || mx->second.empty()) return {};
        name = get(StringId::HistoryOpRefreshRateSet), from = widen(hz->second) + L" Hz", to = widen(mx->second) + L" Hz";
    } else if (k == "fix.game_mode") toggle(StringId::HistoryOpGameModeSet, true);
    else if (k == "fix.game_dvr") toggle(StringId::FixNameGameDvr, false);
    else if (k == "fix.windowed_opt") toggle(StringId::FixNameWindowedOpt, true);
    else if (k == "fix.windows_vrr") toggle(StringId::FixNameWindowsVrr, true);
    else if (k == "fix.hags") toggle(StringId::HistoryOpHagsSet, true);
    else if (k == "fix.fast_startup") toggle(StringId::HistoryOpFastStartupSet, false);
    else return {};
    std::wstring line = get(StringId::ChkApplyChange);
    const auto put = [&line](std::wstring_view key, const std::wstring& val) {
        if (const auto q = line.find(key); q != std::wstring::npos) line.replace(q, key.size(), val);
    };
    put(L"{name}", name), put(L"{old}", from), put(L"{new}", to);
    return line;
}

ModalSpec dns_modal_spec(const core::i18n::Catalog& cat, const std::wstring& finding, const std::wstring& detail, std::string_view preselect) {
    namespace aw = core::adapters::windows;
    ModalSpec spec = fix_apply_modal_spec(cat, {}, finding, detail);
    for (const auto& preset : aw::kDnsPresets) spec.choices.push_back(widen(aw::dns_preset_label(preset)));
    for (std::size_t i = 0; i < std::size(aw::kDnsPresets); ++i)
        if (!preselect.empty() && aw::kDnsPresets[i].id == preselect) spec.initial_choice = static_cast<int>(i);
    return spec;
}

ModalSpec display_keep_modal_spec(const core::i18n::Catalog& cat, int seconds) {
    using core::i18n::StringId;
    ModalSpec spec;
    spec.caption = widen(cat.get(StringId::AppTitle));
    spec.title = widen(cat.get(StringId::ChkDisplayKeepQuestion));
    spec.body.push_back(widen(cat.get(StringId::ChkDisplayKeepCountdown)));  // "{s}" kalan saniye
    spec.yes = widen(cat.get(StringId::ChkDisplayKeep));
    spec.no = widen(cat.get(StringId::ChkDisplayRevert));
    spec.countdown_s = seconds;
    return spec;
}

ModalSpec cleanup_modal_spec(const core::i18n::Catalog& cat, std::size_t categories, const std::wstring& size, const std::wstring& root) {
    using core::i18n::StringId;
    ModalSpec spec;
    spec.caption = widen(cat.get(StringId::AppTitle)) + L" — " + widen(cat.get(StringId::NavSystemTools));
    spec.title = widen(cat.get(StringId::CleanupRunQuestion));
    std::wstring body = widen(cat.get(StringId::CleanupConfirmBody));
    const auto put = [&body](std::wstring_view key, const std::wstring& val) {
        if (const auto q = body.find(key); q != std::wstring::npos) body.replace(q, key.size(), val);
    };
    put(L"{n}", std::to_wstring(categories)), put(L"{size}", size), put(L"{root}", root);
    spec.body.push_back(std::move(body));
    spec.body.push_back(widen(cat.get(StringId::CleanupIrreversibleChip)));  // zorunlu uyari
    spec.yes = widen(cat.get(StringId::CleanupConfirmDelete));
    spec.no = widen(cat.get(StringId::ActionCancel));
    spec.destructive = true;
    return spec;
}

ModalSpec startup_modal_spec(const core::i18n::Catalog& cat) {
    using core::i18n::StringId;
    ModalSpec spec;
    spec.caption = widen(cat.get(StringId::AppTitle)) + L" — " + widen(cat.get(StringId::NavSystemTools));
    spec.title = widen(cat.get(StringId::StartupQuestion));
    spec.body.push_back(widen(cat.get(StringId::StartupQuestionDesc)));
    spec.yes = widen(cat.get(StringId::ActionConfirm));
    spec.no = widen(cat.get(StringId::ActionCancel));
    return spec;
}

ModalSpec support_modal_spec(const core::i18n::Catalog& cat) {
    using core::i18n::StringId;
    ModalSpec spec;
    spec.caption = widen(cat.get(StringId::AppTitle)) + L" — " + widen(cat.get(StringId::NavSettings));
    spec.title = widen(cat.get(StringId::SettingsSupport));
    spec.body = {widen(cat.get(StringId::SettingsSupportDesc)), widen(cat.get(StringId::SettingsSupportDumpsDesc))};
    spec.checkbox = widen(cat.get(StringId::SupportBundleIncludeDumps));
    spec.checkbox_checked = false;  // varsayilan kapali
    spec.yes = widen(cat.get(StringId::SupportBundleCreate));
    spec.no = widen(cat.get(StringId::ActionCancel));
    return spec;
}

namespace {
// Yazi alani yalniz rakam ve nokta kabul eder; baska karakter girmez (gecerlilik yine core'dadir).
std::string narrow_digits(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (const wchar_t c : w) s.push_back(c > 0 && c < 0x80 ? static_cast<char>(c) : '?');
    return s;
}
bool server_text_valid(const std::wstring& t) { return core::net::normalize_server_ipv4(narrow_digits(t)).has_value(); }
}  // namespace

ModalSpec server_ping_modal_spec(const core::i18n::Catalog& cat, const std::string& initial) {
    using core::i18n::StringId;
    ModalSpec spec;
    spec.caption = widen(cat.get(StringId::AppTitle)) + L" — " + widen(cat.get(StringId::NavMeasurements));
    spec.title = widen(cat.get(StringId::ServerPingTitle));
    spec.body = {widen(cat.get(StringId::ServerPingBody1)), widen(cat.get(StringId::ServerPingBody2))};
    spec.input_label = widen(cat.get(StringId::ServerPingLabel));
    spec.input_hint = widen(cat.get(StringId::ServerPingHint));
    spec.input_text = std::wstring(initial.begin(), initial.end());  // kayitli adres yalniz rakam ve nokta (ASCII)
    spec.input_allowed = L"0123456789.";
    spec.input_max = core::net::kServerAddrMaxLen;
    spec.input_valid = &server_text_valid;
    spec.yes = widen(cat.get(StringId::ServerPingYes));
    spec.no = widen(cat.get(StringId::ServerPingNo));
    return spec;
}

std::optional<std::string> ask_server_address(HWND owner, const core::i18n::Catalog& cat, const std::string& initial) {
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return std::nullopt;  // ana pencere yok / tepside: sorulamaz -> paket gonderilmez
    const ModalResult r = w->run_modal(server_ping_modal_spec(cat, initial));
    if (r.answer != ModalAnswer::Yes) return std::nullopt;
    return core::net::normalize_server_ipv4(narrow_digits(r.text));
}

std::optional<bool> ask_support_bundle(HWND owner, const core::i18n::Catalog& cat) {
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return std::nullopt;
    const ModalResult r = w->run_modal(support_modal_spec(cat));
    if (r.answer != ModalAnswer::Yes) return std::nullopt;
    return r.checked;
}

ModalSpec uninstall_modal_spec(const core::i18n::Catalog& cat) {
    using core::i18n::StringId;
    ModalSpec spec;
    spec.caption = widen(cat.get(StringId::AppTitle)) + L" — " + widen(cat.get(StringId::NavSettings));
    spec.title = widen(cat.get(StringId::SettingsUninstallQuestion));
    spec.body = {widen(cat.get(StringId::SettingsUninstallConfirm)), widen(cat.get(StringId::SettingsUninstallIrreversible))};
    spec.yes = widen(cat.get(StringId::SettingsUninstallYes));
    spec.no = widen(cat.get(StringId::SettingsUninstallNo));
    spec.destructive = true;
    return spec;
}

bool ask_uninstall(HWND owner, const core::i18n::Catalog& cat) {
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return false;  // ana pencere yok (tepside): soru sorulmaz, islem baslatilmaz
    return w->run_modal(uninstall_modal_spec(cat)).answer == ModalAnswer::Yes;
}

ModalSpec driver_install_modal_spec(const core::i18n::Catalog& cat, const std::wstring& what, bool admin_without_uac) {
    using core::i18n::StringId;
    ModalSpec spec;
    spec.caption = widen(cat.get(StringId::AppTitle)) + L" — " + widen(cat.get(StringId::NavUpdates));
    spec.title = widen(cat.get(StringId::UpdatesInstallQuestion));
    if (!what.empty()) spec.body.push_back(what);
    spec.body.push_back(widen(cat.get(core::service::admin_copy(StringId::UpdatesInstallAdminNote, admin_without_uac))));
    spec.yes = widen(cat.get(StringId::UpdatesInstallYes));
    spec.no = widen(cat.get(StringId::ActionCancel));
    return spec;
}

bool ask_driver_install(HWND owner, const core::i18n::Catalog& cat, const std::wstring& what, bool admin_without_uac) {
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return false;  // ana pencere yok (tepside): soru sorulmaz, kurulum baslatilmaz
    return w->run_modal(driver_install_modal_spec(cat, what, admin_without_uac)).answer == ModalAnswer::Yes;
}

ModalSpec vendor_install_modal_spec(const core::i18n::Catalog& cat, const std::wstring& what) {
    using core::i18n::StringId;
    ModalSpec spec = driver_install_modal_spec(cat, what, false);  // VendorInstall: hizmet yurutmez, her zaman UAC
    spec.title = widen(cat.get(StringId::UpdatesLocalQuestion));
    return spec;
}

bool ask_vendor_install(HWND owner, const core::i18n::Catalog& cat, const std::wstring& what) {
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return false;  // ana pencere yok (tepside): soru sorulmaz, kurulum baslatilmaz
    return w->run_modal(vendor_install_modal_spec(cat, what)).answer == ModalAnswer::Yes;
}

bool ask_cleanup(HWND owner, const core::i18n::Catalog& cat, std::size_t categories, const std::wstring& size, const std::wstring& root) {
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return false;  // ana pencere yok (tepside): soru sorulmaz, islem baslatilmaz
    return w->run_modal(cleanup_modal_spec(cat, categories, size, root)).answer == ModalAnswer::Yes;
}

bool ask_startup(HWND owner, const core::i18n::Catalog& cat) {
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return false;
    return w->run_modal(startup_modal_spec(cat)).answer == ModalAnswer::Yes;
}

bool ask_fix(HWND owner, const core::i18n::Catalog& cat, const std::wstring& question, const std::wstring& detail, FixDialogIcon icon) {
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return false;  // ana pencere yok (tepside): soru sorulmaz, islem baslatilmaz
    return w->run_modal(fix_modal_spec(cat, question, detail, icon)).answer == ModalAnswer::Yes;
}

bool ask_fix_apply(HWND owner, const core::i18n::Catalog& cat, const std::wstring& change, const std::wstring& finding,
                   const std::wstring& detail) {
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return false;  // ana pencere yok (tepside): soru sorulmaz, islem baslatilmaz
    return w->run_modal(fix_apply_modal_spec(cat, change, finding, detail)).answer == ModalAnswer::Yes;
}

bool ask_fix_all(HWND owner, const core::i18n::Catalog& cat, const std::vector<std::wstring>& changes, const std::wstring& detail) {
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return false;  // ana pencere yok (tepside): soru sorulmaz, islem baslatilmaz
    return w->run_modal(fix_all_modal_spec(cat, changes, detail)).answer == ModalAnswer::Yes;
}

std::optional<std::string_view> ask_dns_preset(HWND owner, const core::i18n::Catalog& cat, const std::wstring& finding,
                                               const std::wstring& detail, std::string_view preselect) {
    namespace aw = core::adapters::windows;
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return std::nullopt;
    const ModalResult r = w->run_modal(dns_modal_spec(cat, finding, detail, preselect));
    if (r.answer != ModalAnswer::Yes || r.choice < 0 || r.choice >= static_cast<int>(std::size(aw::kDnsPresets))) return std::nullopt;
    return aw::kDnsPresets[static_cast<std::size_t>(r.choice)].id;
}

bool confirm_display_keep(HWND owner, const core::i18n::Catalog& cat, int seconds) {
    MainWindow* w = MainWindow::from_hwnd(owner);
    if (!w) return false;  // gosterilemiyor: eski ayara donulur
    return w->run_modal(display_keep_modal_spec(cat, seconds)).answer == ModalAnswer::Yes;
}

}  // namespace ef::ui
