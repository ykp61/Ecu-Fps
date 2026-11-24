// P9-B6: sekiz sekmenin UIA agaci (HitMap = UiaHost'un tek kaynagi) ve klavye turu. Sayfalar ekran disi WIC bitmap'e
// gercek Screen::paint ile cizilir (pencere ve UIA saglayicisi gerekmez; saglayici bu haritadan ad / rol / durum uretir).
// Denetlenenler: bos / bosluklu ad yok, rol-durum tutarliligi, tekil AutomationId, Tab sirasi (ust menu once, soldan saga;
// devre disi / cizilmemis durak yok), Tab ve Shift+Tab ile tam tur, Evet/Hayir onaylarinda odak.
// Gercek UIA saglayicisi ve gercek klavye: tests/windows/check_uia.ps1 (windows.uia).
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "ef_test.h"
#include "ui/accessibility/uia.h"
#include "ui/consent/consent_dialog.h"
#include "ui/consent/fix_dialogs.h"
#include "ui/render/graphics.h"
#include "ui/shell/modal.h"
#include "ui/shell/screen.h"
#include "ui/theme/system_palette.h"
#include "ui/theme/theme.h"

using namespace ef::ui;
namespace i18n = ef::core::i18n;
using Microsoft::WRL::ComPtr;

namespace {

struct Rig {
    Theme theme = default_theme();
    i18n::Catalog cat;
    std::unique_ptr<Graphics> g;
    std::unique_ptr<Screen> screen;
    ComPtr<IWICImagingFactory> wic;
    bool ok = false;

    explicit Rig(i18n::Locale loc, float text_scale = 1.f) : theme(with_text_scale(default_theme(), text_scale)) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto c = i18n::Catalog::embedded(loc);
        if (!c.ok()) return;
        cat = c.value();
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return;
        g = Graphics::create(theme, loc == i18n::Locale::TrTR ? L"tr-TR" : L"en-US");
        if (!g) return;
        screen = std::make_unique<Screen>(theme, cat);
        ok = true;
    }

    // Tek kare; ikinci cizim gruplar / satirlar gibi ilk karede ogrenilen durumu da kapsar (ui_snapshot ile ayni yol).
    HitMap paint(UiState& st, const ViewModel& vm, float w = 1180.f, float h = 720.f) {
        HitMap hits;
        for (int pass = 0; pass < 2; ++pass) {
            ComPtr<IWICBitmap> bmp;
            if (FAILED(wic->CreateBitmap(static_cast<UINT>(w), static_cast<UINT>(h), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) ||
                !g->bind_wic(bmp.Get(), 96.f) || !g->begin())
                return hits;
            hits = screen->paint(*g, w, h, st, vm, 0);
            g->end();
        }
        return hits;
    }
};

ViewModel base_vm() {
    ViewModel vm;
    vm.version = "0.9.0";
    vm.os = "Windows 11 Pro 26H2 (26300.9711)";
    vm.cpu = "AMD Ryzen 7 9800X3D 8-Core Processor · 8C/8T";
    vm.gpu = "NVIDIA GeForce RTX 5080 · 16 GB";
    vm.ram = "64 GB · 2×32 GB DDR5 6000 MT/s";
    vm.board = "Gigabyte X870E AORUS ELITE WIFI7 ICE · BIOS F11b";
    vm.display = "DELL S2721DGF · 2560×1440 · 165 Hz";
    return vm;
}

UiState base_state(Page p) {
    UiState st;
    st.reduced_motion = true;
    st.page = p;
    return st;
}

bool has(const std::vector<Hit>& v, Hit h) { return std::find(v.begin(), v.end(), h) != v.end(); }
std::ptrdiff_t pos(const std::vector<Hit>& v, Hit h) { return std::find(v.begin(), v.end(), h) - v.begin(); }

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

// Denetlenecek sekmeler: ust menudeki sekiz sekme + Sistem araclari'nin iki alt sayfasi (Baslangic, Sistem).
std::vector<Page> audited_pages() {
    std::vector<Page> v(kNavOrder.begin(), kNavOrder.end());
    v.push_back(Page::Startup);
    v.push_back(Page::System);
    return v;
}

const char* page_name(Page p) {
    switch (p) {
        case Page::Overview: return "overview";
        case Page::Readiness: return "readiness";
        case Page::Games: return "games";
        case Page::Measurements: return "measurements";
        case Page::Cleanup: return "cleanup";
        case Page::Startup: return "startup";
        case Page::History: return "history";
        case Page::Settings: return "settings";
        case Page::System: return "system";
        case Page::Updates: return "updates";
        default: return "?";
    }
}

// Bir sayfanin UIA agacini denetler; bulgu sayisini doner ve her bulguyu yazar (ilk hata kaniti okunabilir kalsin).
int audit_tree(const UiState& st, const HitMap& hits, const char* where, bool quiet = false) {
    int bad = 0;
    const auto fail = [&](const char* what, Hit id) {
        ++bad;
        if (!quiet) std::fprintf(stderr, "  A11Y %s id=%d (%s)\n", what, static_cast<int>(id), where);
    };
    std::set<std::wstring> ids;
    for (const auto& [id, r] : hits.items) {
        const auto it = hits.meta.find(id);
        if (it == hits.meta.end()) {
            fail("item without meta", id);
            continue;
        }
        const HitMeta& m = it->second;
        // Ad: bos degil, bas / son bosluk yok (ekran okuyucu bos konusur), ham anahtar degil.
        // UIA'ya giden ad uia_clean_text'ten gecer (bas / son bosluk saglayicida temizlenir); temizlenmis ad bos olamaz.
        const std::wstring uia_name = uia_clean_text(m.name);
        if (uia_name.empty()) fail("empty name", id);
        else if (uia_name.find(L"{") != std::wstring::npos && uia_name.find(L"}") != std::wstring::npos) fail("unfilled placeholder in name", id);
        // Sinir kutusu: gecerli ve sonlu (UIA BoundingRectangle).
        if (!(r.w > 0) || !(r.h > 0)) fail("empty bounds", id);
        // Rol / durum tutarliligi.
        switch (m.role) {
            case AccRole::ToggleSwitch:
            case AccRole::CheckBox:
                if (m.toggle != 0 && m.toggle != 1) fail("toggle without state", id);
                break;
            case AccRole::ProgressBar:
                if (!(m.range_value >= 0.0 && m.range_value <= 100.0)) fail("progress without 0..100 value", id);
                if (hits.activatable(id)) fail("progress is activatable", id);
                break;
            case AccRole::Text:
                if (hits.activatable(id)) fail("text is activatable", id);
                break;
            case AccRole::TabItem:
            case AccRole::RadioButton:
                // secili durumu okunur: SelectionItem deseni bool tasir (alan her zaman tanimli); yalniz sekme icin ayrica
                // asagida tam-bir-secili sinanir.
                break;
            default: break;
        }
        // Tekil, bilinen AutomationId.
        const std::wstring aid = hit_automation_id(id);
        if (aid == L"unknown" || aid.empty()) fail("unknown automation id", id);
        else if (!ids.insert(aid).second) fail("duplicate automation id", id);
    }
    // Ust menu: sekiz sekme TabItem, adli, soldan saga kNavOrder sirasinda; ayni anda yalniz biri secili.
    int selected = 0;
    float prev_x = -1.f;
    for (const Page pg : kNavOrder) {
        const Hit h = nav_hit(static_cast<std::size_t>(pg));
        const auto it = hits.meta.find(h);
        const RectF* r = hits.rect_of(h);
        if (it == hits.meta.end() || !r) {
            fail("nav tab missing", h);
            continue;
        }
        if (it->second.role != AccRole::TabItem) fail("nav tab is not TabItem", h);
        if (r->x <= prev_x) fail("nav tabs not left-to-right in kNavOrder", h);
        prev_x = r->x;
        selected += it->second.selected ? 1 : 0;
        if (it->second.selected != nav_tab_selected(pg, st.page)) fail("nav tab selection mismatch", h);
    }
    if (selected != 1) fail("not exactly one selected nav tab", Hit::None);
    return bad;
}

// Gercek Tab sirasi (niyet sirasinin cizilmis, etkin ogeleri) denetimi.
int audit_focus_order(const UiState& st, const HitMap& hits, const char* where, bool quiet = false) {
    int bad = 0;
    const auto fail = [&](const char* what, Hit id) {
        ++bad;
        if (!quiet) std::fprintf(stderr, "  A11Y %s id=%d (%s)\n", what, static_cast<int>(id), where);
    };
    const auto order = focus_order(st, hits);
    if (order.empty()) fail("empty tab order", Hit::None);
    std::set<Hit> seen;
    for (const Hit h : order) {
        if (!seen.insert(h).second) fail("tab stop listed twice", h);
        if (!hits.meta.count(h)) fail("tab stop without element", h);
        else if (!hits.activatable(h)) fail("disabled / read-only tab stop", h);
    }
    // Ust menu once, kNavOrder sirasinda (Baslangic / Sistem alt sekmeleri Tab duragi degildir).
    for (std::size_t i = 0; i < kNavTabCount; ++i)
        if (i >= order.size() || order[i] != nav_hit(static_cast<std::size_t>(kNavOrder[i]))) {
            fail("tab order does not start with the eight nav tabs", i < order.size() ? order[i] : Hit::None);
            break;
        }
    // Tum etkin, Tab duragi olan ogeler sirada.
    for (const auto& [id, r] : hits.items)
        if (hits.tabbable(id) && !has(order, id)) fail("tabbable element missing from tab order", id);
    return bad;
}

// Klavye turu: Tab ile baslangictan (odak yok) tum duraklari bir kez dolasir ve basa doner; Shift+Tab tersini yapar.
int audit_keyboard_tour(const UiState& st, const HitMap& hits, const char* where, bool quiet = false) {
    int bad = 0;
    const auto fail = [&](const char* what, Hit id) {
        ++bad;
        if (!quiet) std::fprintf(stderr, "  A11Y %s id=%d (%s)\n", what, static_cast<int>(id), where);
    };
    const auto intent = focus_order(st);
    const auto real = focus_order(st, hits);
    if (real.empty()) return bad + 1;
    std::vector<Hit> forward;
    Hit cur = Hit::None;
    for (std::size_t i = 0; i < real.size() + 1; ++i) {
        cur = focus_step(intent, real, cur, +1);
        forward.push_back(cur);
    }
    // Ilk Tab ilk durakta, sonra sirayla; n. Tab'tan sonra (n + 1.) ilk durakta (tur kapanir).
    if (!std::equal(real.begin(), real.end(), forward.begin())) fail("Tab walk differs from the tab order", forward.front());
    if (forward.back() != real.front()) fail("Tab walk does not wrap to the first stop", forward.back());
    std::vector<Hit> backward;
    cur = Hit::None;
    for (std::size_t i = 0; i < real.size() + 1; ++i) {
        cur = focus_step(intent, real, cur, -1);
        backward.push_back(cur);
    }
    std::vector<Hit> rev(real.rbegin(), real.rend());
    if (!std::equal(rev.begin(), rev.end(), backward.begin())) fail("Shift+Tab walk is not the reverse order", backward.front());
    if (backward.back() != real.back()) fail("Shift+Tab walk does not wrap to the last stop", backward.back());
    return bad;
}

}  // namespace

EF_TEST(a11y_tree_eight_pages_names_roles_ids_both_locales) {
    for (const i18n::Locale loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        Rig rig(loc);
        EF_CHECK(rig.ok);
        if (!rig.ok) return;
        for (const Page pg : audited_pages()) {
            UiState st = base_state(pg);
            const auto hits = rig.paint(st, base_vm());
            char where[64];
            std::snprintf(where, sizeof where, "%s %s", loc == i18n::Locale::TrTR ? "tr" : "en", page_name(pg));
            EF_CHECK(!hits.items.empty());
            EF_CHECK(audit_tree(st, hits, where) == 0);
        }
    }
}

EF_TEST(a11y_tree_tab_order_and_full_keyboard_tour_per_page) {
    for (const i18n::Locale loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        Rig rig(loc);
        EF_CHECK(rig.ok);
        if (!rig.ok) return;
        for (const Page pg : audited_pages()) {
            UiState st = base_state(pg);
            const auto hits = rig.paint(st, base_vm());
            char where[64];
            std::snprintf(where, sizeof where, "%s %s", loc == i18n::Locale::TrTR ? "tr" : "en", page_name(pg));
            EF_CHECK(audit_focus_order(st, hits, where) == 0);
            EF_CHECK(audit_keyboard_tour(st, hits, where) == 0);
            if (GetEnvironmentVariableW(L"EF_A11Y_DUMP", nullptr, 0) > 0) {
                const auto order = focus_order(st, hits);
                std::printf("ORDER %s n=%zu\n", where, order.size());
                for (const Hit h : order) {
                    const RectF* r = hits.rect_of(h);
                    std::printf("   %4d %-34s y=%7.1f x=%7.1f \"%.40s\"\n", static_cast<int>(h), narrow(hit_automation_id(h)).c_str(), r ? r->y : -1.f, r ? r->x : -1.f,
                                narrow(hits.meta.at(h).name).c_str());
                }
            }
        }
    }
}

EF_TEST(a11y_tree_tab_order_follows_reading_order) {
    // Tab sirasi okuma sirasidir: ust menuden sonra icerik duraklari yukaridan asagi, ayni satirda soldan saga. Satir
    // sapmasi 14 DIP'e kadar ayni satir sayilir (dugmeler satir icinde dikey ortalanir).
    constexpr float kRow = 14.f;
    for (const i18n::Locale loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        Rig rig(loc);
        EF_CHECK(rig.ok);
        if (!rig.ok) return;
        for (const Page pg : audited_pages()) {
            UiState st = base_state(pg);
            const auto hits = rig.paint(st, base_vm());
            const auto order = focus_order(st, hits);
            const RectF* prev = nullptr;
            for (std::size_t i = kNavTabCount; i < order.size(); ++i) {
                const RectF* r = hits.rect_of(order[i]);
                EF_CHECK(r != nullptr);
                if (!r) continue;
                if (prev) {
                    const bool next_row = r->y > prev->y + kRow;
                    const bool same_row_right = std::fabs(r->y - prev->y) <= kRow && r->x >= prev->x;
                    if (!(next_row || same_row_right))
                        std::fprintf(stderr, "  A11Y reading order broken at %s (%s %s): y=%.1f x=%.1f after y=%.1f x=%.1f\n", narrow(hit_automation_id(order[i])).c_str(),
                                     loc == i18n::Locale::TrTR ? "tr" : "en", page_name(pg), r->y, r->x, prev->y, prev->x);
                    EF_CHECK(next_row || same_row_right);
                }
                prev = r;
            }
        }
    }
}

namespace {
ViewModel confirm_vm(ef::core::perf::Phase phase) {
    ViewModel vm = base_vm();
    vm.perf.phase = phase;
    vm.perf.switch_on = phase == ef::core::perf::Phase::ConfirmRestore;
    vm.perf.groups = {{"plan.power_mode", "plan.power_mode.desc", true, true, true},
                      {"plan.power_scheme", "plan.power_scheme.desc", true, false, true},
                      {"plan.vfx", "plan.vfx.desc", true, true, false}};
    return vm;
}
}  // namespace

EF_TEST(a11y_tree_inline_yes_no_confirmation_buttons) {
    // Performans onayi (Evet / Hayir): iki dugme, Button rolu, adli, ayni boyda, Evet Hayir'dan once; tam klavye turu ve
    // agac denetimi gecer. Oyun calisirken Evet cizilmez (agacta ve Tab sirasinda yok), Hayir kalir.
    for (const i18n::Locale loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        const bool tr = loc == i18n::Locale::TrTR;
        Rig rig(loc);
        EF_CHECK(rig.ok);
        if (!rig.ok) return;
        for (const auto phase : {ef::core::perf::Phase::Confirming, ef::core::perf::Phase::ConfirmRestore}) {
            UiState st = base_state(Page::Overview);
            auto vm = confirm_vm(phase);
            sync_perf_animations(st, PerfView{}, vm.perf, rig.theme, 0);
            const auto hits = rig.paint(st, vm);
            EF_CHECK(audit_tree(st, hits, "confirm") == 0);
            EF_CHECK(audit_focus_order(st, hits, "confirm") == 0);
            EF_CHECK(audit_keyboard_tour(st, hits, "confirm") == 0);
            EF_CHECK(hits.meta.count(Hit::Yes) && hits.meta.count(Hit::No));
            if (!hits.meta.count(Hit::Yes) || !hits.meta.count(Hit::No)) return;
            EF_CHECK(hits.meta.at(Hit::Yes).role == AccRole::Button && hits.meta.at(Hit::No).role == AccRole::Button);
            EF_CHECK(hits.meta.at(Hit::Yes).name == (tr ? L"Evet" : L"Yes"));
            EF_CHECK(hits.meta.at(Hit::No).name == (tr ? L"Hay\u0131r" : L"No"));
            const auto order = focus_order(st, hits);
            EF_CHECK(has(order, Hit::Yes) && has(order, Hit::No) && pos(order, Hit::Yes) < pos(order, Hit::No));
            const RectF* y = hits.rect_of(Hit::Yes);
            const RectF* n = hits.rect_of(Hit::No);
            EF_CHECK(y && n && y->w == n->w && y->h == n->h && y->y == n->y && y->x < n->x);
            // Onay asamasinda Performans anahtari agacta kalir ama devre disidir; Tab sirasinda yok (odak Evet / Hayir'da).
            EF_CHECK(!hits.meta.count(Hit::Toggle) || (!hits.activatable(Hit::Toggle) && !has(order, Hit::Toggle)));

            // Oyun calisiyor: Evet yok.
            UiState sb = base_state(Page::Overview);
            auto vb = confirm_vm(phase);
            vb.game_gate_blocked = true;
            sb.game_blocked = true;
            sync_perf_animations(sb, PerfView{}, vb.perf, rig.theme, 0);
            const auto hb = rig.paint(sb, vb);
            EF_CHECK(!hb.meta.count(Hit::Yes) && hb.meta.count(Hit::No));
            EF_CHECK(!has(focus_order(sb, hb), Hit::Yes) && has(focus_order(sb, hb), Hit::No));
            EF_CHECK(audit_tree(sb, hb, "confirm game running") == 0);
            EF_CHECK(audit_keyboard_tour(sb, hb, "confirm game running") == 0);
        }
    }
}

EF_TEST(a11y_tree_modal_yes_no_default_focus_is_no_for_every_confirmation) {
    // Uygulama ici Evet/Hayir pencereleri: ilk odak Hayir, Enter / Bosluk Hayir'i calistirir (Evet degil), Esc Hayir (dismissed),
    // Tab sirasi [..] Hayir, Evet; Evet etkin. Her onay icin ayni (yikici olanlar dahil).
    for (const i18n::Locale loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        auto cat = i18n::Catalog::embedded(loc);
        EF_CHECK(cat.ok());
        if (!cat.ok()) return;
        const auto& c = cat.value();
        const std::vector<std::pair<const char*, ModalSpec>> specs = {
            {"consent", game_consent_modal_spec(c, L"Oyun")},
            {"fix", fix_modal_spec(c, L"Soru?", L"Ayrinti")},
            {"fix_warning", fix_modal_spec(c, L"Soru?", L"Ayrinti", FixDialogIcon::Warning)},
            {"fix_apply", fix_apply_modal_spec(c, L"Ayar: Kapali \u2192 Acik", L"Bulgu", L"Ayrinti")},
            {"cleanup", cleanup_modal_spec(c, 3, L"1,2 GB", L"C:\\")},
            {"startup", startup_modal_spec(c)},
            {"support", support_modal_spec(c)},
            {"uninstall", uninstall_modal_spec(c)},
            {"driver", driver_install_modal_spec(c, L"Paket")},
            {"vendor", vendor_install_modal_spec(c, L"Paket")},
        };
        for (const auto& [name, spec] : specs) {
            ModalState m = modal_open(spec);
            EF_CHECK(!spec.yes.empty() && !spec.no.empty() && spec.yes != spec.no);
            EF_CHECK(m.focus.item == ModalItem::No);
            EF_CHECK(modal_yes_enabled(m));
            const auto order = modal_focus_order(m);
            const auto find_item = [&](ModalItem it) {
                return std::find_if(order.begin(), order.end(), [&](const ModalFocus& f) { return f.item == it; }) - order.begin();
            };
            EF_CHECK(find_item(ModalItem::No) < find_item(ModalItem::Yes) && find_item(ModalItem::Yes) < static_cast<std::ptrdiff_t>(order.size()));
            for (const ModalKey k : {ModalKey::Enter, ModalKey::Space}) {
                ModalState e = modal_open(spec);
                modal_key(e, k);
                EF_CHECK(e.done && e.result.answer == ModalAnswer::No);
                if (!(e.done && e.result.answer == ModalAnswer::No)) std::fprintf(stderr, "  A11Y default focus answers Yes (%s)\n", name);
            }
            ModalState esc = modal_open(spec);
            modal_key(esc, ModalKey::Escape);
            EF_CHECK(esc.done && esc.result.answer == ModalAnswer::No && esc.result.dismissed);
            // Tab Hayir'dan Evet'e gider; Evet ancak ikinci, bilincli tusla calisir.
            ModalState t = modal_open(spec);
            modal_key(t, ModalKey::Tab);
            EF_CHECK(!t.done && t.focus.item == ModalItem::Yes);
            modal_key(t, ModalKey::Enter);
            EF_CHECK(t.done && t.result.answer == ModalAnswer::Yes);
        }
        // DNS listesi: secim yapilmadan Evet kapali, ilk odak yine Hayir.
        ModalState dns = modal_open(dns_modal_spec(c, L"Bulgu", L"Ayrinti"));
        EF_CHECK(dns.focus.item == ModalItem::No && !modal_yes_enabled(dns));
        // Geri sayimli ekran onayi: sure dolunca Hayir.
        ModalState keep = modal_open(display_keep_modal_spec(c, 15));
        EF_CHECK(keep.focus.item == ModalItem::No);
    }
}

EF_TEST(a11y_tree_audit_catches_seeded_defects) {
    // Pozitif kontrol: denetim gercekten yakalar (bos gecen denetim kanit degildir).
    HitMap m;
    for (std::size_t i = 0; i < kNavTabCount; ++i)
        m.add(nav_hit(static_cast<std::size_t>(kNavOrder[i])), {200.f + 90.f * static_cast<float>(i), 10, 80, 32}, L"Sekme", AccRole::TabItem, i == 0);
    UiState st = base_state(Page::Overview);
    EF_CHECK(audit_tree(st, m, "seed", true) == 0);  // temiz harita temiz
    HitMap empty_name = m;
    empty_name.add(Hit::Details, {300, 100, 80, 32}, L"", AccRole::Button);
    EF_CHECK(audit_tree(st, empty_name, "seed", true) >= 1);
    HitMap blank = m;
    blank.add(Hit::Details, {300, 100, 80, 32}, L" \n ", AccRole::Button);  // yalniz bosluk: temizlenince bos
    EF_CHECK(audit_tree(st, blank, "seed", true) >= 1);
    HitMap no_state = m;
    no_state.add(Hit::Toggle, {300, 100, 44, 24}, L"Performans", AccRole::ToggleSwitch);  // toggle = -1
    EF_CHECK(audit_tree(st, no_state, "seed", true) >= 1);
    HitMap two_selected = m;
    two_selected.meta[nav_hit(static_cast<std::size_t>(Page::Games))].selected = true;
    EF_CHECK(audit_tree(st, two_selected, "seed", true) >= 1);
    HitMap wrong_role = m;
    wrong_role.meta[nav_hit(static_cast<std::size_t>(Page::Games))].role = AccRole::Button;
    EF_CHECK(audit_tree(st, wrong_role, "seed", true) >= 1);
    // Tab sirasi: ust menu sirasi bozuk / devre disi durak / eksik durak.
    HitMap ok = m;
    ok.add(Hit::Details, {300, 100, 80, 32}, L"Ayrinti", AccRole::Button);
    EF_CHECK(audit_focus_order(st, ok, "seed", true) == 0 && audit_keyboard_tour(st, ok, "seed", true) == 0);
    HitMap reversed = ok;
    std::swap(reversed.items[0].second.x, reversed.items[1].second.x);
    EF_CHECK(audit_tree(st, reversed, "seed", true) >= 1);
    HitMap no_nav = ok;
    no_nav.items.erase(no_nav.items.begin());
    no_nav.meta.erase(nav_hit(static_cast<std::size_t>(kNavOrder[0])));
    EF_CHECK(audit_focus_order(st, no_nav, "seed", true) >= 1);
}

namespace {
std::size_t paint_overflows(Rig& rig, UiState& st, const ViewModel& vm, HitMap& out, float w = 1180.f, float h = 720.f) {
    rig.g->set_audit(true);
    rig.g->clear_overflows();
    out = rig.paint(st, vm, w, h);
    return rig.g->overflows().size();
}

// Temsili Windows yuksek kontrast tablolari (ui_snapshot'taki "Siyah" tablosu + "Beyaz" tablosu).
Palette hc_black() {
    const auto hex = [](const char* hx) { return *Color::from_hex(hx); };
    return high_contrast_palette_from(
        SystemColorTable{hex("#000000"), hex("#000000"), hex("#FFFFFF"), hex("#1AEBFF"), hex("#000000"), hex("#FFFF00"), hex("#3FF23F")});
}
Palette hc_white() {
    const auto hex = [](const char* hx) { return *Color::from_hex(hx); };
    return high_contrast_palette_from(
        SystemColorTable{hex("#FFFFFF"), hex("#FFFFFF"), hex("#000000"), hex("#37006E"), hex("#FFFFFF"), hex("#00009F"), hex("#600000")});
}
}  // namespace

EF_TEST(a11y_visual_high_contrast_palettes_meet_wcag_on_every_surface) {
    // WCAG 1.4.3 (metin 4,5:1) ve 1.4.11 (odak halkasi / sinir / vurgu 3:1) her yuzeyde; yuksek kontrastta sistem renkleri.
    for (const Palette& p : {hc_black(), hc_white()}) {
        for (const Color& surface : {p.background, p.sidebar, p.surface, p.surface_raised}) {
            EF_CHECK(contrast_ratio(p.text_primary, surface) >= 7.0);
            EF_CHECK(contrast_ratio(p.text_secondary, surface) >= 4.5);
            EF_CHECK(contrast_ratio(p.focus_ring, surface) >= 3.0);
            EF_CHECK(contrast_ratio(p.border, surface) >= 3.0);
            EF_CHECK(contrast_ratio(p.accent, surface) >= 3.0);
        }
        EF_CHECK(contrast_ratio(p.on_accent, p.accent) >= 4.5);  // ana dugme etiketi
        // Durum renkleri metinle de okunur ama anlam yalniz renkle tasinmaz; en az 4,5:1.
        for (const Color& tone : {p.success, p.warning, p.error}) EF_CHECK(contrast_ratio(tone, p.background) >= 4.5);
    }
}

EF_TEST(a11y_visual_high_contrast_pages_tree_order_and_no_overflow) {
    const Palette black = hc_black(), white = hc_white();
    for (const Palette* pal : {&black, &white}) {
        for (const i18n::Locale loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
            Rig rig(loc);
            EF_CHECK(rig.ok);
            if (!rig.ok) return;
            rig.screen->set_palette(pal);
            for (const Page pg : audited_pages()) {
                UiState st = base_state(pg);
                st.focus = nav_hit(static_cast<std::size_t>(kNavOrder[1]));
                st.focus_visible = true;
                HitMap hits;
                const std::size_t of = paint_overflows(rig, st, base_vm(), hits);
                char where[64];
                std::snprintf(where, sizeof where, "hc %s %s", loc == i18n::Locale::TrTR ? "tr" : "en", page_name(pg));
                EF_CHECK(of == 0);
                EF_CHECK(audit_tree(st, hits, where) == 0);
                EF_CHECK(audit_focus_order(st, hits, where) == 0);
                EF_CHECK(audit_keyboard_tour(st, hits, where) == 0);
                EF_CHECK(hits.focus_drawn);  // odak halkasi yuksek kontrastta da cizilir
            }
            // Evet / Hayir onayi da ayni agac ve tur denetiminden gecer.
            UiState st = base_state(Page::Overview);
            auto vm = confirm_vm(ef::core::perf::Phase::Confirming);
            sync_perf_animations(st, PerfView{}, vm.perf, rig.theme, 0);
            HitMap hits;
            EF_CHECK(paint_overflows(rig, st, vm, hits) == 0);
            EF_CHECK(audit_tree(st, hits, "hc confirm") == 0 && audit_keyboard_tour(st, hits, "hc confirm") == 0);
            EF_CHECK(hits.meta.count(Hit::Yes) && hits.meta.count(Hit::No));
        }
    }
}

EF_TEST(a11y_visual_text_scale_pages_tree_order_and_no_overflow) {
    // Windows "Metin boyutu" %125 (kMaxTextScale; daha buyuk istek kirpilir): sayfalar tasmaz, dokunma hedefi >= 24 DIP,
    // ag / Tab sirasi / klavye turu %100 ile ayni sozlesmeyi saglar; 960x540 asgari pencere dahil.
    for (const float requested : {1.25f, 1.5f, 2.0f}) {
        for (const i18n::Locale loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
            Rig rig(loc, requested);
            EF_CHECK(rig.ok);
            if (!rig.ok) return;
            EF_CHECK(rig.theme.role(Role::Body).size == default_theme().role(Role::Body).size * kMaxTextScale);
            for (const auto& wh : {std::pair<float, float>{1180.f, 720.f}, std::pair<float, float>{rig.theme.min_window_w, rig.theme.min_window_h}}) {
                for (const Page pg : audited_pages()) {
                    UiState st = base_state(pg);
                    HitMap hits;
                    const std::size_t of = paint_overflows(rig, st, base_vm(), hits, wh.first, wh.second);
                    char where[80];
                    std::snprintf(where, sizeof where, "text x%.2f %s %s %.0fx%.0f", requested, loc == i18n::Locale::TrTR ? "tr" : "en", page_name(pg), wh.first, wh.second);
                    if (of != 0) std::fprintf(stderr, "  A11Y %zu text overflow(s) (%s)\n", of, where);
                    EF_CHECK(of == 0);
                    EF_CHECK(audit_tree(st, hits, where) == 0);
                    EF_CHECK(audit_focus_order(st, hits, where) == 0);
                    EF_CHECK(audit_keyboard_tour(st, hits, where) == 0);
                    for (const auto& [id, r] : hits.items) {
                        const auto it = hits.meta.find(id);
                        if (it != hits.meta.end() && interactive(it->second.role) && r.w > 0 && !it->second.offscreen)
                            EF_CHECK(r.w >= rig.theme.touch_min - 0.01f && r.h >= rig.theme.touch_min - 0.01f);
                    }
                }
            }
            // Onay dugmeleri %125'te de es boyda; etiket genisligi dugme genisligine sigar.
            UiState st = base_state(Page::Overview);
            auto vm = confirm_vm(ef::core::perf::Phase::Confirming);
            sync_perf_animations(st, PerfView{}, vm.perf, rig.theme, 0);
            HitMap hits;
            EF_CHECK(paint_overflows(rig, st, vm, hits) == 0);
            const RectF* y = hits.rect_of(Hit::Yes);
            const RectF* n = hits.rect_of(Hit::No);
            EF_CHECK(y && n && y->w == n->w && y->h == n->h && y->y == n->y);
            // Yerlesim y'leri tam sayidir: dugme kenari ve etiket taban cizgisi ayni piksel izgarasina oturur (P9-B6).
            EF_CHECK(y && std::floor(y->y) == y->y);
        }
    }
}

EF_TEST(a11y_uia_clean_text_trims_outer_whitespace_only) {
    // Saglayici Name / HelpText / duyuru metnini temizler: WMI disk modelleri gibi kaynaklar sonuna bosluk doldurur.
    EF_CHECK(uia_clean_text(L"MLD M500 NVMe SSD ") == L"MLD M500 NVMe SSD");
    EF_CHECK(uia_clean_text(L"  Panel\r\n") == L"Panel");
    EF_CHECK(uia_clean_text(L"\t\u00A0Ayarlar\u202F\u200B") == L"Ayarlar");
    EF_CHECK(uia_clean_text(L"ic  bosluk korunur") == L"ic  bosluk korunur");
    EF_CHECK(uia_clean_text(L"satir\nsonu ortada kalir") == L"satir\nsonu ortada kalir");
    EF_CHECK(uia_clean_text(L"").empty() && uia_clean_text(L" \n\t ").empty());
    EF_CHECK(uia_clean_text(L"T\u00fcrk\u00e7e \u00e7\u011f\u0131\u015f ") == L"T\u00fcrk\u00e7e \u00e7\u011f\u0131\u015f");
}
