// Spec 2026-11-12 H (tespit; ADR-031): Guncellemeler sayfasi. Kaynak yalniz Windows Update; uretici sitelerine otomatik
// sorgu yok, yalniz resmi sayfa dugmesi (kullanici tiklayinca varsayilan tarayicida). "En yeni" denmez: "Guncel" =
// Windows Update'e gore guncel. Basarisiz / zaman asimina ugramis / durdurulmus arama "Denetlenemedi" + neden + "Yeniden
// dene"dir; hicbir zaman bos bir "Guncel" degildir. Firmware sinifi oge "Elle denetlenir" (uygulama BIOS / firmware
// yazmaz). Kurulum (H2): kimligi okunan, firmware olmayan oneride "Kur" (uygulama ici Evet/Hayir -> UAC -> yardimci);
// kurulum surerken satir cipi "Kuruluyor…", bitince sonuc ust kartin altinda tek kartta (yeniden baslatma notu ile).
// Ekrandan kalkan aciklamalar (kaynak ilkesi, firmware kurali, tarayici notu) UIA HelpText'te okunur.
// ADR-032: ekran karti / yonga seti satiri disari yonlendirmez. Windows Update onerisi varsa satirda "Guncelleme var
// <surum>" + "Indir ve kur" (ayni H2 yolu); yoksa durum cipi ("Guncel" = Windows Update'e gore) ve ekran kartinda
// "Dosyadan kur" (kullanicinin indirdigi resmi paket: kopya -> imza + yayinci -> Evet/Hayir -> yardimci). BIOS satiri
// yalniz bilgidir ("BIOS guncellemesi bu uygulamadan yapilmaz"; eski metin UIA HelpText'te).
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "core/drivers/driver_install.h"
#include "core/drivers/vendor_install.h"
#include "core/inventory/problem_devices.h"
#include "core/service/uac_copy.h"
#include "ui/render/icons.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
namespace cd = core::drivers;

namespace {

std::wstring widen_u(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::wstring rep_u(std::wstring s, std::wstring_view k, const std::wstring& v) {
    for (std::size_t p = s.find(k); p != std::wstring::npos; p = s.find(k, p + v.size())) s.replace(p, k.size(), v);
    return s;
}
std::wstring two(int v) { return (v < 10 ? L"0" : L"") + std::to_wstring(v); }

constexpr float kRowIconBox = 28.f;
constexpr float kRowIconInset = 5.f;

std::pair<StringId, Icon> kind_of(cd::DeviceKind k) {
    switch (k) {
        case cd::DeviceKind::Display: return {StringId::UpdatesKindDisplay, Icon::Gpu};
        case cd::DeviceKind::Audio: return {StringId::UpdatesKindAudio, Icon::Windows};
        case cd::DeviceKind::Network: return {StringId::UpdatesKindNetwork, Icon::Net};
        case cd::DeviceKind::Chipset: return {StringId::UpdatesKindChipset, Icon::Cpu};
        case cd::DeviceKind::Storage: return {StringId::UpdatesKindStorage, Icon::Disk};
        case cd::DeviceKind::Firmware: return {StringId::UpdatesKindFirmware, Icon::Cpu};
        case cd::DeviceKind::Other: break;
    }
    return {StringId::UpdatesKindOther, Icon::Windows};
}

std::wstring join_dot(std::initializer_list<std::wstring> parts) {
    std::wstring m;
    for (const auto& x : parts)
        if (!x.empty()) m += (m.empty() ? L"" : L" · ") + x;
    return m;
}

}  // namespace

std::wstring Screen::updates_install_what(const ViewModel& vm, std::size_t k) const {
    const auto& scan = vm.updates.scan;
    if (scan.state != cd::ScanState::Found || k >= scan.updates.size()) return {};
    const auto& u = scan.updates[k];
    std::wstring kind = t(kind_of(cd::device_kind(u)).first);
    if (!u.provider.empty()) kind += L" · " + widen_u(u.provider);
    std::wstring date;
    if (u.year > 0 && u.month > 0 && u.day > 0)
        date = rep_u(rep_u(rep_u(t(StringId::UpdatesDate), L"{y}", std::to_wstring(u.year)), L"{m}", two(u.month)), L"{d}", two(u.day));
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    return join_dot({widen_u(cd::display_name(u)), kind, widen_u(u.version), date, u.size_bytes ? format_bytes(*u.size_bytes, tr) : std::wstring()});
}

namespace {
// Sonuc satiri: {code} Windows Update yolunda HRESULT (0x...), resmi paket yolunda ondalik cikis kodu; {version} geri
// okunan surum (yoksa "—").
std::wstring install_line(std::wstring text, const ViewModel::UpdatesView::InstallView& in) {
    wchar_t code[16];
    if (in.target.rfind(cd::kVendorTargetPrefix, 0) == 0) swprintf_s(code, L"%u", static_cast<unsigned>(in.exit_code));
    else swprintf_s(code, L"0x%08X", static_cast<unsigned>(in.hresult));
    text = rep_u(std::move(text), L"{code}", code);
    return rep_u(std::move(text), L"{version}", in.version.empty() ? std::wstring(L"—") : widen_u(in.version));
}
}  // namespace

std::wstring Screen::updates_install_text(const ViewModel& vm) const {
    const auto& in = vm.updates.install;
    if (in.notice_key.empty()) return {};
    std::wstring s = install_line(tk(in.notice_key), in);
    if (!in.name.empty()) s = widen_u(in.name) + L": " + s;
    if (in.reboot) s += L" " + t(StringId::UpdatesInstallReboot);
    if (in.no_restore_point) s += L" " + t(StringId::UpdatesInstallNoRestorePoint);
    if (in.previous_interrupted) s += L" " + t(StringId::UpdatesInstallPrevInterrupted);
    return s;
}

namespace {
// Windows Update onerisinin donanim kimligindeki PCI uretici kimligi ("PCI\VEN_10DE&DEV_..." -> 0x10DE); yoksa 0.
std::uint32_t pci_vendor_of(std::string_view hwid) {
    for (std::size_t p = 0; p + 8 <= hwid.size(); ++p) {
        const auto c = [&](std::size_t i) { return static_cast<char>(hwid[p + i] >= 'a' && hwid[p + i] <= 'z' ? hwid[p + i] - 32 : hwid[p + i]); };
        if (c(0) != 'V' || c(1) != 'E' || c(2) != 'N' || c(3) != '_') continue;
        std::uint32_t v = 0;
        for (std::size_t i = 4; i < 8; ++i) {
            const char x = c(i);
            if (x >= '0' && x <= '9') v = v * 16 + static_cast<std::uint32_t>(x - '0');
            else if (x >= 'A' && x <= 'F') v = v * 16 + static_cast<std::uint32_t>(x - 'A' + 10);
            else return 0;
        }
        return v;
    }
    return 0;
}
// Satira eslenen, kurulabilir (firmware degil, kimligi okunan) ilk oneri.
std::optional<std::size_t> match_update(const cd::ScanView& scan, cd::DeviceKind kind, std::uint32_t pci_vendor, std::vector<bool>& used) {
    if (scan.state != cd::ScanState::Found) return std::nullopt;
    for (std::size_t i = 0; i < scan.updates.size() && i < used.size(); ++i) {
        const auto& u = scan.updates[i];
        if (used[i] || cd::is_firmware(u) || cd::device_kind(u) != kind) continue;
        if (cd::install_target(u.update_id, u.revision).empty()) continue;
        if (pci_vendor && pci_vendor_of(u.hardware_id) != pci_vendor) continue;
        used[i] = true;
        return i;
    }
    return std::nullopt;
}
}  // namespace

std::vector<UpdatesVendorRow> updates_vendor_rows(const ViewModel& vm) {
    std::vector<UpdatesVendorRow> out;
    if (vm.system_loading) return out;
    const auto& uv = vm.updates;
    std::vector<bool> used(uv.scan.state == cd::ScanState::Found ? uv.scan.updates.size() : 0, false);
    const auto push = [&](UpdatesVendorRow r) { out.push_back(std::move(r)); };  // ADR-032: link yok
    for (std::size_t gi = 0; gi < uv.gpus.size(); ++gi) {
        const auto& gpu = uv.gpus[gi];
        UpdatesVendorRow r;
        r.kind = UpdatesVendorKind::Gpu;
        r.name = gpu.name;
        r.version = gpu.driver_version;
        r.page = cd::gpu_vendor_page(gpu.vendor_id);
        r.wu = match_update(uv.scan, cd::DeviceKind::Display, gpu.vendor_id, used);
        if (!r.wu && cd::gpu_vendor_from_pci(gpu.vendor_id) && gi < static_cast<std::size_t>(kMaxUpdatesLinks)) r.local = gi;
        push(std::move(r));
    }
    {
        UpdatesVendorRow r;
        r.kind = UpdatesVendorKind::Chipset;
        r.page = cd::chipset_vendor_page(uv.cpu_vendor);
        r.name = std::string(r.page.name);
        r.wu = match_update(uv.scan, cd::DeviceKind::Chipset, 0, used);
        push(std::move(r));
    }
    {
        UpdatesVendorRow r;
        r.kind = UpdatesVendorKind::Bios;
        r.name = uv.board_vendor;
        if (!uv.board_model.empty()) r.name += (r.name.empty() ? "" : " ") + uv.board_model;
        r.version = uv.bios_version;
        const auto& d = uv.bios_date;  // SMBIOS "MM/DD/YYYY"
        if (d.size() == 10 && d[2] == '/' && d[5] == '/') {
            r.date_m = std::atoi(d.substr(0, 2).c_str()), r.date_d = std::atoi(d.substr(3, 2).c_str()), r.date_y = std::atoi(d.substr(6, 4).c_str());
            if (r.date_m < 1 || r.date_m > 12 || r.date_d < 1 || r.date_d > 31 || r.date_y < 1990) r.date_y = r.date_m = r.date_d = 0;
        }
        r.page = cd::board_vendor_page(uv.board_vendor);
        push(std::move(r));
    }
    return out;
}

Screen::UpdatesStatus Screen::updates_status(const ViewModel& vm) const {
    const auto& uv = vm.updates;
    const auto& scan = uv.scan;
    UpdatesStatus out;
    switch (scan.state) {
        case cd::ScanState::NeverChecked: out.big = t(StringId::UpdatesStateNever); break;
        case cd::ScanState::Checking: out.big = t(StringId::UpdatesStateChecking); break;
        case cd::ScanState::UpToDate: out.big = t(StringId::UpdatesStateUpToDate); break;
        case cd::ScanState::Found:
            out.big = scan.updates.size() == 1 ? t(StringId::UpdatesStateFoundOne)
                                               : rep_u(t(StringId::UpdatesStateFound), L"{n}", std::to_wstring(scan.updates.size()));
            break;
        case cd::ScanState::Failed: out.big = t(StringId::UpdatesStateFailed); break;
    }
    const bool checking = scan.state == cd::ScanState::Checking;
    if (scan.state == cd::ScanState::Failed) {
        switch (scan.failure) {
            case cd::ScanFailure::Offline: out.sub = t(StringId::UpdatesFailOffline); break;
            case cd::ScanFailure::ServiceDisabled: out.sub = t(StringId::UpdatesFailService); break;
            case cd::ScanFailure::Policy: out.sub = t(StringId::UpdatesFailPolicy); break;
            case cd::ScanFailure::Timeout: out.sub = t(StringId::UpdatesFailTimeout); break;
            case cd::ScanFailure::Cancelled: out.sub = t(StringId::UpdatesFailCancelled); break;
            case cd::ScanFailure::None:
            case cd::ScanFailure::Error: {
                wchar_t code[16];
                swprintf_s(code, L"0x%08X", static_cast<unsigned>(scan.hresult));
                out.sub = rep_u(t(StringId::UpdatesFailError), L"{code}", code);
                break;
            }
        }
        out.warn = true;
    } else if (scan.state != cd::ScanState::NeverChecked) {
        out.sub = t(StringId::UpdatesSource);
        if (!uv.last_check.empty() && !checking) out.sub += L" · " + rep_u(t(StringId::UpdatesLastCheck), L"{t}", widen_u(uv.last_check));
        // P12-B2a: "BIOS ve firmware uygulamadan yazilmaz" satir notu BIOS / firmware satirlarinda zaten yazili; durum
        // satirindan kalkti, durum metninin HelpText'inde okunur.
    }
    // Spec H2: kurulum surerken alt satir kurulan ogeyi adlandirir. ADR-032: resmi paket hazirligi / kurulumu da.
    using LS = ViewModel::UpdatesView::LocalView::Stage;
    const bool vendor_run = uv.install.target.rfind(cd::kVendorTargetPrefix, 0) == 0;
    if (uv.install.running && !checking)
        out.sub = rep_u(t(vendor_run ? StringId::UpdatesLocalRunningSub : StringId::UpdatesInstallRunningSub), L"{name}", widen_u(uv.install.name)),
        out.warn = false;
    else if (uv.local.stage != LS::None && !checking)
        out.sub = rep_u(t(uv.local.stage == LS::Copying ? StringId::UpdatesLocalCopyingSub : StringId::UpdatesLocalVerifyingSub), L"{name}",
                        widen_u(uv.local.name)),
        out.warn = false;
    // Oyun acikken arama baslatilmaz (spec H): neden satiri durumu ezer (arama zaten surmuyorsa).
    if (vm.game_gate_blocked && !checking && !uv.install.running && uv.local.stage == LS::None) out.sub = t(StringId::UpdatesGameBlocked), out.warn = true;
    return out;
}

std::wstring Screen::updates_status_text(const ViewModel& vm) const {
    const auto st = updates_status(vm);
    return a11y_join(st.big, st.sub);
}

float Screen::updates_page(Graphics& g, const RectF& body_in, UiState& s, const ViewModel& vm, std::int64_t now, HitMap& hits) {
    // BOOST maketi: <main class="p-4"> govdeyi baslik seridinin (pb-1) 16 DIP altinda baslatir; ortak duzen 8 verir (Oyun
    // hazirligi / Olcumler ile ayni 5 DIP pay).
    constexpr float kTopInset = 5.f;
    const RectF body{body_in.x, body_in.y + kTopInset, body_in.w, std::max(body_in.h - kTopInset, 0.f)};
    const auto& p = pal();
    const bool hc = pal_ != &theme_.dark;
    const bool tr = cat_.locale() == core::i18n::Locale::TrTR;
    const auto& uv = vm.updates;
    const auto& scan = uv.scan;
    const float pad = theme_.card_pad;
    const float sl = theme_.role(Role::Secondary).line;
    const float bl = theme_.role(Role::Body).line;
    constexpr float kUnbounded = 10000.f;
    const auto single_w = [&](Role role, const std::wstring& text) { return std::ceil(g.measure(role, text, kUnbounded, false).w) + 1.f; };
    const auto date_text = [&](int y, int m, int d) -> std::wstring {
        if (y <= 0 || m <= 0 || d <= 0) return {};
        return rep_u(rep_u(rep_u(t(StringId::UpdatesDate), L"{y}", std::to_wstring(y)), L"{m}", two(m)), L"{d}", two(d));
    };
    Stack full{body, theme_.card_gap};
    game_banner(g, full, body, vm, s, now, hits);
    StatusSeq seq{0};
    float y = body.y + full.cursor;
    const bool checking = scan.state == cd::ScanState::Checking;
    s.updates_checking = checking;
    s.updates_links = 0;
    s.updates_installs.clear();
    s.updates_actions.clear();
    const auto& inst = uv.install;
    using LS = ViewModel::UpdatesView::LocalView::Stage;
    const bool preparing = uv.local.stage != LS::None;

    // ---- Ust kart (BOOST maketi view-updates, 2026-11-16 birebir): card-v2 r10, p-4 + kenar (17); sol blok 20px/600 durum
    // metni (26 satir) + 12px ikincil alt satir (mt 2: "Kaynak · Son denetim · BIOS ve firmware yazilmaz" ya da neden);
    // sagda .btn-primary (h 32). Sayfa basligi seridi yok.
    {
        constexpr float kCardPad = 17.f;
        const auto status = updates_status(vm);
        const std::wstring& big = status.big;
        const std::wstring& sub = status.sub;
        const Color sub_c = status.warn ? p.warning : p.text_secondary;
        const bool gate = vm.game_gate_blocked && !checking;
        const Hit btn = checking ? Hit::UpdatesCancel : Hit::UpdatesCheck;
        const std::wstring btn_label = t(checking ? StringId::UpdatesCancel : scan.state == cd::ScanState::Failed ? StringId::UpdatesRetry
                                         : scan.state == cd::ScanState::NeverChecked ? StringId::UpdatesCheck : StringId::UpdatesRecheck);
        const RectF br0 = panel_button_rect(g, btn_label, !checking);
        const float title_h = theme_.role(Role::Semi20).line;
        const float sub_line = theme_.role(Role::Small12).line;
        const float block_h = title_h + (sub.empty() ? 0.f : 2.f + sub_line);
        const float card_h = std::max(block_h, br0.h) + 2 * kCardPad;
        const RectF card{body.x, y, body.w, card_h};
        const float text_w = std::max(card.w - 2 * kCardPad - br0.w - 12.f, 1.f);
        const float ty = card.y + (card_h - block_h) / 2;
        const bool visible = card.bottom() >= s.view_top && card.y <= s.view_bottom;
        if (visible) {
            draw_card(g, card, false, depth::kPanelCardRadius);
            const Color big_c = scan.state == cd::ScanState::Failed ? ensure_contrast(p.warning, p.surface, p.text_primary, 4.5)
                                : scan.state == cd::ScanState::NeverChecked || checking ? p.text_secondary
                                                                                        : p.text_primary;
            g.text(Role::Semi20, fit_text(g, Role::Semi20, big, text_w), {card.x + kCardPad, ty, text_w, title_h}, big_c, {Align::Leading, false, true});
            if (!sub.empty())
                g.text(Role::Small12, fit_text(g, Role::Small12, sub, text_w), {card.x + kCardPad, ty + title_h + 2.f, text_w, sub_line},
                       status.warn ? ensure_contrast(sub_c, p.surface, p.text_primary, 4.5) : sub_c, {Align::Leading, false, true});
        } else {
            log_card(card);
        }
        const Hit st_id = seq.take();
        hits.add_text(st_id, {card.x + kCardPad, ty, text_w, block_h}, a11y_join(big, sub));
        hits.meta[st_id].help = a11y_join(t(StringId::UpdatesHelpSource), t(StringId::UpdatesFooterNote));
        const float bx = card.right() - kCardPad - br0.w;
        const float by = card.y + (card_h - br0.h) / 2;
        const bool enabled = checking || (!vm.game_gate_blocked && !inst.running && !preparing);  // spec H2: kurulum surerken denetlenmez
        panel_button(g, bx, by, btn, btn_label, !checking, s, now, hits, enabled);
        hits.meta[btn].help = inst.running ? t(StringId::UpdatesInstallBusy) : gate ? t(StringId::UpdatesGameBlocked) : t(StringId::UpdatesHelpSource);
        y += card_h + theme_.card_gap;
    }

    // ---- Spec H2: son kurulum sonucu (tek satir sonuc + varsa yeniden baslatma / geri yukleme noktasi notu). ----
    if (!inst.running && !preparing && !inst.notice_key.empty()) {
        std::wstring line = install_line(tk(inst.notice_key), inst);
        if (!inst.name.empty()) line = widen_u(inst.name) + L": " + line;
        std::wstring notes;
        if (inst.reboot) notes = t(StringId::UpdatesInstallReboot);
        if (inst.no_restore_point) notes += (notes.empty() ? L"" : L" ") + t(StringId::UpdatesInstallNoRestorePoint);
        if (inst.previous_interrupted) notes += (notes.empty() ? L"" : L" ") + t(StringId::UpdatesInstallPrevInterrupted);
        const float block_h = bl + (notes.empty() ? 0.f : theme_.space(0) + sl);
        const RectF card{body.x, y, body.w, block_h + 2 * pad};
        const float tw = std::max(card.w - 2 * pad, 1.f);
        if (card.bottom() >= s.view_top && card.y <= s.view_bottom) {
            draw_card(g, card, false, depth::kPanelCardRadius);
            const Color c = inst.warn ? ensure_contrast(p.warning, p.surface, p.text_primary, 4.5) : p.text_primary;
            g.text(Role::Body, fit_text(g, Role::Body, line, tw), {card.x + pad, card.y + pad, tw, bl}, c, {Align::Leading, false, true});
            if (!notes.empty())
                g.text(Role::Secondary, fit_text(g, Role::Secondary, notes, tw), {card.x + pad, card.y + pad + bl + theme_.space(0), tw, sl},
                       inst.reboot ? ensure_contrast(p.warning, p.surface, p.text_primary, 4.5) : p.text_secondary, {Align::Leading, false, true});
        } else {
            log_card(card);
        }
        const Hit rid = seq.take();
        hits.add_text(rid, {card.x + pad, card.y + pad, tw, block_h}, updates_install_text(vm));
        y += card.h + theme_.card_gap;
    }

    // ---- T17: Sorunlu aygitlar (Aygit Yoneticisi sorun kodlari; salt okuma, otomatik surucu eylemi yok). Liste hic
    // okunmadiysa blok yoktur; okunamadiysa "okunamadi" (asla bos "sorun yok" degil). ----
    {
        const auto& pd = uv.problem_devices;
        if (uv.problem_devices_loaded) {
            constexpr std::size_t kShow = 6;
            struct Line { std::wstring text; bool info = false; };
            std::vector<Line> lines;
            if (!pd.enumerated) {
                lines.push_back({t(StringId::UpdatesProblemDevicesUnread), true});
            } else if (pd.devices.empty()) {
                lines.push_back({t(StringId::UpdatesProblemDevicesNone), true});
            } else {
                for (std::size_t i = 0; i < pd.devices.size() && i < kShow; ++i) {
                    const auto& d = pd.devices[i];
                    std::wstring text = widen_u(d.name.empty() ? d.instance_id : d.name);
                    if (!d.device_class.empty()) text += L" · " + widen_u(d.device_class);
                    std::wstring what = tk(std::string(core::inventory::problem_text_key(d.problem_code)));
                    text += L" · " + rep_u(std::move(what), L"{code}", std::to_wstring(d.problem_code));
                    lines.push_back({std::move(text), core::inventory::problem_is_informational(d.problem_code)});
                }
                if (pd.devices.size() > kShow)
                    lines.push_back({rep_u(t(StringId::UpdatesProblemDevicesMore), L"{n}", std::to_wstring(pd.devices.size() - kShow)), true});
            }
            const float tl = theme_.role(Role::Semi13).line;
            const float line_gap = theme_.space(0);
            const float block_h = tl + static_cast<float>(lines.size()) * (line_gap + sl);
            const RectF card{body.x, y, body.w, block_h + 2 * pad};
            const float tw = std::max(card.w - 2 * pad, 1.f);
            const std::wstring title = t(StringId::UpdatesProblemDevicesTitle);
            std::wstring a11y = title;
            if (card.bottom() >= s.view_top && card.y <= s.view_bottom) {
                draw_card(g, card, false, depth::kPanelCardRadius);
                g.text(Role::Semi13, fit_text(g, Role::Semi13, title, tw), {card.x + pad, card.y + pad, tw, tl}, p.text_primary, {Align::Leading, false, true});
            } else {
                log_card(card);
            }
            float ly = card.y + pad + tl;
            for (const auto& ln : lines) {
                ly += line_gap;
                if (card.bottom() >= s.view_top && card.y <= s.view_bottom) {
                    const Color c = ln.info ? p.text_secondary : ensure_contrast(p.warning, p.surface, p.text_primary, 4.5);
                    g.text(Role::Secondary, fit_text(g, Role::Secondary, ln.text, tw), {card.x + pad, ly, tw, sl}, c, {Align::Leading, false, true});
                }
                a11y = a11y_join(a11y, ln.text);
                ly += sl;
            }
            const Hit pid = seq.take();
            hits.add_text(pid, {card.x + pad, card.y + pad, tw, block_h}, a11y);
            if (pid != Hit::None) hits.meta[pid].help = t(StringId::UpdatesProblemDevicesHelp);
            y += card.h + theme_.card_gap;
        }
    }

    // ---- Satirlar: once Windows Update onerileri, sonra ekran karti / yonga seti / BIOS (elle denetlenir). ----
    struct Row {
        Icon icon = Icon::Windows;
        std::wstring name, kind, meta, chip, help;
        ChipTone tone = ChipTone::Neutral;
        bool warn = false;          // ikon tonu: guncelleme var
        std::wstring link_label;    // bos = eylem yok
        std::size_t link = 0;       // UpdatesLink indeksi
        bool install = false;       // spec H2: "Kur" dugmesi (UpdatesInstall + install_index)
        std::size_t install_index = 0;
        bool install_enabled = false;
        std::wstring install_help;
        std::wstring install_label;  // bos = "Kur"; ADR-032 uretici satirinda "Indir ve kur"
        bool local = false;          // ADR-032: "Dosyadan kur" (UpdatesLocal + local_index; kopya surerken "Durdur")
        std::size_t local_index = 0;
        bool local_enabled = false;
        std::wstring local_label, local_help;
        std::wstring muted;          // ikinci satirin sonundaki soluk not (BIOS)
    };
    std::vector<Row> rows;
    const auto dash = std::wstring(L"—");
    const auto meta_join = [&](std::initializer_list<std::wstring> parts) {
        std::wstring m;
        for (const auto& x : parts)
            if (!x.empty()) m += (m.empty() ? L"" : L" · ") + x;
        return m.empty() ? dash : m;
    };
    const auto vendor_rows = updates_vendor_rows(vm);
    std::vector<bool> merged(scan.state == cd::ScanState::Found ? scan.updates.size() : 0, false);
    for (const auto& vr : vendor_rows)
        if (vr.wu && *vr.wu < merged.size()) merged[*vr.wu] = true;  // uretici satirinda gosterilir
    const bool busy = inst.running || preparing;
    // Windows Update kurulumu (DriverInstall) hizmet yolundan gecer: hizmet hazirsa "UAC sorulmaz" esi.
    const StringId wu_help = core::service::admin_copy(StringId::UpdatesHelpInstall, vm.admin_without_uac);
    if (scan.state == cd::ScanState::Found) {
        for (std::size_t ui = 0; ui < scan.updates.size(); ++ui) {
            const auto& u = scan.updates[ui];
            if (merged[ui]) continue;
            if (rows.size() >= static_cast<std::size_t>(kMaxUpdatesRows)) break;
            const bool fw = cd::is_firmware(u);
            const auto [kid, icon] = kind_of(cd::device_kind(u));
            Row r;
            r.icon = icon;
            r.name = widen_u(cd::display_name(u));
            if (r.name.empty()) r.name = dash;
            r.kind = t(kid);
            if (!u.provider.empty()) r.kind += L" · " + widen_u(u.provider);
            r.meta = meta_join({widen_u(u.version), date_text(u.year, u.month, u.day), u.size_bytes ? format_bytes(*u.size_bytes, tr) : std::wstring()});
            r.chip = t(fw ? StringId::UpdatesChipManual : StringId::UpdatesChipAvailable);
            r.tone = fw ? ChipTone::Neutral : ChipTone::Warn;
            r.warn = !fw;
            // Spec H2: "Kur" yalniz kimligi okunan, firmware olmayan Windows Update onerisinde. Bir seferde tek kurulum;
            // oyun acikken ve arama surerken devre disi. Kurulan satirin cipi "Kuruluyor…".
            const std::string target = fw ? std::string() : cd::install_target(u.update_id, u.revision);
            r.help = t(fw ? StringId::UpdatesHelpFirmware : target.empty() ? StringId::UpdatesHelpInstallLater : wu_help);
            if (!target.empty()) {
                r.install = true;
                r.install_index = ui;
                r.install_enabled = !busy && !vm.game_gate_blocked && !checking;
                r.install_help = busy ? t(StringId::UpdatesInstallBusy) : vm.game_gate_blocked ? t(StringId::UpdatesGameBlocked) : t(wu_help);
                if (inst.running && inst.target == target) r.chip = t(StringId::UpdatesInstallRunning), r.tone = ChipTone::Neutral;
            }
            rows.push_back(std::move(r));
        }
    }
    // Uretici satirlari updates_vendor_rows ile (MainWindow ayni listeden baglantiyi acar: indeks kaymaz).
    // Durum cipi (Windows Update sonucuna gore; "Guncel" = Windows Update'e gore guncel).
    const auto scan_chip = [&](Row& r) {
        switch (scan.state) {
            case cd::ScanState::NeverChecked: r.chip = t(StringId::UpdatesStateNever); break;
            case cd::ScanState::Checking: r.chip = t(StringId::UpdatesStateChecking); break;
            case cd::ScanState::UpToDate:
            case cd::ScanState::Found: r.chip = t(StringId::UpdatesStateUpToDate), r.tone = ChipTone::Good; break;
            case cd::ScanState::Failed: r.chip = t(StringId::UpdatesStateFailed), r.tone = ChipTone::Warn; break;
        }
    };
    for (const auto& vr : vendor_rows) {
        if (rows.size() >= static_cast<std::size_t>(kMaxUpdatesRows)) break;
        Row r;
        r.icon = vr.kind == UpdatesVendorKind::Gpu ? Icon::Gpu : vr.kind == UpdatesVendorKind::Chipset ? Icon::Cpu : Icon::Ram;
        r.name = vr.name.empty() ? dash : widen_u(vr.name);
        r.kind = t(vr.kind == UpdatesVendorKind::Gpu ? StringId::UpdatesKindDisplay
                   : vr.kind == UpdatesVendorKind::Chipset ? StringId::UpdatesKindChipset
                                                           : StringId::UpdatesKindBios);
        std::wstring date;
        if (vr.date_y > 0) date = date_text(vr.date_y, vr.date_m, vr.date_d);
        r.meta = meta_join({widen_u(vr.version), date});
        if (vr.kind == UpdatesVendorKind::Bios) {
            // ADR-032: yalniz bilgi. Eski metin ("Elle denetlenir", resmi sayfa notu) UIA HelpText'te kalir.
            r.chip = t(StringId::UpdatesChipInfo);
            r.muted = t(StringId::UpdatesBiosNoUpdate);
            r.help = a11y_join(a11y_join(t(StringId::UpdatesHelpBiosInfo), t(StringId::UpdatesChipManual)), t(StringId::UpdatesHelpVendor));
            rows.push_back(std::move(r));
            continue;
        }
        const bool gpu = vr.kind == UpdatesVendorKind::Gpu;
        if (vr.wu && *vr.wu < scan.updates.size()) {
            const auto& u = scan.updates[*vr.wu];
            const std::string target = cd::install_target(u.update_id, u.revision);
            r.chip = u.version.empty() ? t(StringId::UpdatesChipAvailable) : rep_u(t(StringId::UpdatesChipAvailableVer), L"{v}", widen_u(u.version));
            r.tone = ChipTone::Warn;
            r.warn = true;
            r.help = t(core::service::admin_copy(gpu ? StringId::UpdatesHelpGpuWu : StringId::UpdatesHelpInstall, vm.admin_without_uac));
            r.install = true;
            r.install_index = *vr.wu;
            r.install_label = t(StringId::UpdatesDownloadInstall);
            r.install_enabled = !busy && !vm.game_gate_blocked && !checking;
            r.install_help = busy ? t(StringId::UpdatesInstallBusy) : vm.game_gate_blocked ? t(StringId::UpdatesGameBlocked) : r.help;
            if (inst.running && inst.target == target) r.chip = t(StringId::UpdatesInstallRunning), r.tone = ChipTone::Neutral, r.warn = false;
        } else {
            scan_chip(r);
            r.help = t(gpu ? StringId::UpdatesHelpUpToDateWu : StringId::UpdatesHelpChipsetWu);
            if (vr.local) {
                const std::size_t gi = *vr.local;
                const auto vendor = cd::gpu_vendor_from_pci(gi < uv.gpus.size() ? uv.gpus[gi].vendor_id : 0);
                const bool mine = uv.local.gpu == gi;
                const bool copying = preparing && mine && uv.local.stage == LS::Copying;
                r.local = true;
                r.local_index = gi;
                r.local_label = t(copying ? StringId::UpdatesCancel : StringId::UpdatesLocalPick);
                r.local_enabled = copying || (!busy && !vm.game_gate_blocked);
                r.local_help = a11y_join(rep_u(t(StringId::UpdatesLocalPickTitle), L"{vendor}", vendor ? widen_u(cd::display_name(*vendor)) : dash),
                                         busy && !copying ? t(StringId::UpdatesInstallBusy) : vm.game_gate_blocked ? t(StringId::UpdatesGameBlocked)
                                                                                                                    : t(StringId::UpdatesHelpGpuLocal));
                if (preparing && mine) {
                    if (uv.local.stage == LS::Copying) {
                        const auto pct = uv.local.total > 0 ? static_cast<unsigned>(std::min<std::uint64_t>(100, uv.local.done * 100 / uv.local.total)) : 0u;
                        r.chip = rep_u(t(StringId::UpdatesLocalCopying), L"{p}", std::to_wstring(pct));
                    } else {
                        r.chip = t(StringId::UpdatesLocalVerifying);
                    }
                    r.tone = ChipTone::Neutral;
                } else if (const auto ref = inst.running ? cd::parse_vendor_target(inst.target) : std::nullopt; ref && vendor && ref->vendor == *vendor) {
                    r.chip = t(StringId::UpdatesInstallRunning), r.tone = ChipTone::Neutral;
                }
            }
        }
        rows.push_back(std::move(r));
    }
    if (rows.empty()) return std::max(y - theme_.card_gap - body.y, 0.f) + kTopInset;

    // BOOST maketi satiri (px-4 py-3 + kenar, gap-3): 32 DIP ikon kutusu r6 (ton %12), ad 13px/600 + tur 12px ikincil;
    // sagda satira ozgu akis: 12px mono surum / tarih / boyut, 11px rozet (px 8 py 2 r4, ton %15) ve dugme (yesil
    // .btn-primary kucuk = Windows Update onerisi, .btn-secondary = Dosyadan kur). Sutun yok; her satir kendi sag kenarinda.
    constexpr float kInset = 17.f, kRowPadY = 12.f, kIconBox = 32.f, kGap = 12.f, kBadgePadX = 8.f, kBadgePadY = 2.f, kBadgeR = 4.f;
    const std::wstring install_label = t(StringId::UpdatesInstall);
    const float inner_x = body.x + kInset, inner_w = std::max(body.w - 2 * kInset, 1.f);
    const float nl = theme_.role(Role::Semi13).line, kl = theme_.role(Role::Small12).line;
    const float row_h = 2 * kRowPadY + nl + kl;
    const float badge_h = theme_.role(Role::Small11).line + 2 * kBadgePadY;
    const float name_x = inner_x + kIconBox + kGap;
    const float card_h = 2.f + row_h * static_cast<float>(rows.size());
    const RectF card{body.x, y, body.w, card_h};
    const bool card_visible = card.bottom() >= s.view_top && card.y <= s.view_bottom;
    if (card_visible) draw_card(g, card, false, depth::kPanelCardRadius);
    else log_card(card);
    const Color divider = mix(p.surface, p.border, alpha_scale::kDivider);
    float ry = card.y + 1.f;
    for (std::size_t k = 0; k < rows.size(); ++k) {
        const auto& r = rows[k];
        const RectF rr{inner_x, ry, inner_w, row_h};
        const bool visible = rr.bottom() >= s.view_top && rr.y <= s.view_bottom;
        // Eylem: yesil = Windows Update kurulumu, ikincil = Dosyadan kur. Genislik kendi etiketine gore.
        const bool has_act = r.install || r.local;
        const std::wstring& act_label = r.install ? (r.install_label.empty() ? install_label : r.install_label) : r.local_label;
        const RectF act_r = has_act ? panel_button_rect(g, act_label, r.install, false, r.install) : RectF{};
        const float act_w = act_r.w;
        const float chip_w = r.chip.empty() ? 0.f : single_w(Role::Small11, r.chip) - 1.f + 2 * kBadgePadX;
        float meta_w = r.meta == dash ? 0.f : single_w(Role::Mono12, r.meta);
        float chip_wd = chip_w;
        const auto group_w = [&] {
            float w = act_w;
            if (chip_wd > 0) w += (w > 0 ? kGap : 0.f) + chip_wd;
            if (meta_w > 0) w += (w > 0 ? kGap : 0.f) + meta_w;
            return w;
        };
        // Dar govde: ad en az 200'e sigmiyorsa surum ikinci satira (turun yanina) iner; hala sigmiyorsa rozet de iner.
        bool meta_below = false, chip_below = false;
        if (inner_w - kIconBox - kGap - group_w() - kGap < 200.f && meta_w > 0) meta_below = true, meta_w = 0;
        if (inner_w - kIconBox - kGap - group_w() - kGap < 100.f && chip_wd > 0) chip_below = true, chip_wd = 0;
        const float gw = group_w();
        const float name_w = std::max(inner_w - kIconBox - kGap - (gw > 0 ? gw + kGap : 0.f), 1.f);
        const float act_x = inner_x + inner_w - act_w;
        const float chip_x = act_x - (act_w > 0 && chip_wd > 0 ? kGap : 0.f) - chip_wd;
        const float meta_x = chip_x - (meta_w > 0 && (chip_wd > 0 || act_w > 0) ? kGap : 0.f) - meta_w;
        const Color tone = r.warn ? p.warning : p.text_secondary;
        if (visible) {
            if (k > 0) g.fill({inner_x, std::floor(ry), inner_w, 1}, divider);
            const RectF ib{inner_x, ry + (row_h - kIconBox) / 2, kIconBox, kIconBox};
            if (!hc) g.fill_round(ib, 6.f, mix(p.surface, tone, alpha_scale::kIconBoxTint));
            else g.stroke_round(ib, 6.f, p.border);
            g.draw_icon(r.icon, ib.inset(8.f), hc ? p.text_primary : ensure_contrast(tone, p.surface, p.text_primary, 3.0));
            const float ly = ry + (row_h - nl - kl) / 2;
            g.text(Role::Semi13, fit_text(g, Role::Semi13, r.name, name_w), {name_x, ly, name_w, nl}, r.name == dash ? p.text_secondary : p.text_primary,
                   {Align::Leading, false, true});
            std::wstring second = meta_below && r.meta != dash ? r.kind + L" · " + r.meta : r.kind;
            if (!r.muted.empty()) second += L" · " + r.muted;
            float sx = name_x, sw = name_w;
            if (chip_below && !r.chip.empty()) {
                const auto st = fit_text(g, Role::Small12, r.chip, name_w);
                const float stw = std::min(single_w(Role::Small12, st), name_w);
                const Color stc = r.tone == ChipTone::Warn ? p.warning : r.tone == ChipTone::Danger ? p.error : p.text_secondary;
                g.text(Role::Small12, st, {name_x, ly + nl, stw, kl}, stc, {Align::Leading, false, true});
                sx += stw + theme_.space(1), sw = std::max(name_w - stw - theme_.space(1), 1.f);
                second = L"· " + second;
            }
            if (sw > 8.f)
                g.text(Role::Small12, fit_text(g, Role::Small12, second, sw), {sx, ly + nl, sw, kl}, p.text_secondary, {Align::Leading, false, true});
            if (meta_w > 0)
                g.text(Role::Mono12, r.meta, {meta_x, ry, meta_w, row_h}, p.text_secondary, {Align::Leading, false, true});
            if (chip_wd > 0) {
                const RectF cb{chip_x, ry + (row_h - badge_h) / 2, chip_wd, badge_h};
                const Color ct = r.tone == ChipTone::Warn ? p.warning : r.tone == ChipTone::Good ? p.accent : r.tone == ChipTone::Danger ? p.error : p.text_secondary;
                if (hc) {
                    g.stroke_round(cb, kBadgeR, p.border);
                    g.text(Role::Small11, r.chip, cb, p.text_primary, {Align::Center, false, true});
                } else {
                    const Color bg = r.tone == ChipTone::Neutral ? depth::kBadgeFill : mix(p.surface, ct, 0.15f);
                    g.fill_round(cb, kBadgeR, bg);
                    g.text(Role::Small11, r.chip, cb, r.tone == ChipTone::Neutral ? p.text_secondary : ensure_contrast(ct, bg, p.text_primary, 4.5),
                           {Align::Center, false, true});
                }
            }
        }
        const Hit rid = hit_of(HitKind::UpdatesRow, k);
        const float row_right = chip_wd > 0 ? chip_x + chip_wd : meta_w > 0 ? meta_x + meta_w : name_x + name_w;
        hits.add_text(rid, {inner_x, ry, std::max(row_right - inner_x, 1.f), row_h},
                      a11y_join(a11y_join(a11y_join(a11y_join(r.name, r.kind), r.muted), r.meta == dash ? std::wstring() : r.meta), r.chip));
        if (rid != Hit::None) hits.meta[rid].help = r.help;
        if (has_act) {
            const float by = ry + (row_h - act_r.h) / 2;
            const bool primary = r.install;
            const Hit aid = r.install ? hit_of(HitKind::UpdatesInstall, r.install_index) : hit_of(HitKind::UpdatesLocal, r.local_index);
            const bool en = r.install ? r.install_enabled : r.local_enabled;
            if (visible) {
                panel_button(g, act_x, by, aid, act_label, primary, s, now, hits, en, false, primary, act_w);
            } else {
                hits.add(aid, {act_x, by, act_w, act_r.h}, act_label, AccRole::Button);
                hits.meta[aid].enabled = en;
            }
            if (aid != Hit::None) hits.meta[aid].help = a11y_join(r.name, r.install ? r.install_help : r.local_help);
            if (r.install) s.updates_installs.push_back(r.install_index);
            s.updates_actions.push_back(aid);
        }
        ry += row_h;
    }
    y += card_h;
    return std::max(y - body.y, 0.f) + kTopInset;
}

}  // namespace ef::ui
