// P12-B1: "Duzelt" dugmesi yalniz gercekten duzelten bulguda cizilir (Oyun hazirligi sayfasi + Panel bulgu satirlari);
// Panel basligindaki "Duzelt" duzeltilecek bir sey yokken devre disi, varken etkindir; sayfa degistirmez. Cizim gercek
// Screen::paint ile ekran disi bitmap'e yapilir (pencere gerekmez); yalniz salt okunur bulgu listesi verilir, ayar yazilmaz.
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <memory>
#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "core/readiness/fix_plan.h"
#include "ef_test.h"
#include "ui/render/graphics.h"
#include "ui/shell/screen.h"
#include "ui/theme/theme.h"

using namespace ef::ui;
namespace i18n = ef::core::i18n;
namespace cr = ef::core::readiness;
using Microsoft::WRL::ComPtr;

namespace {
struct FixRig {
    Theme theme = default_theme();
    i18n::Catalog cat;
    std::unique_ptr<Graphics> g;
    std::unique_ptr<Screen> screen;
    ComPtr<IWICImagingFactory> wic;
    bool ok = false;
    explicit FixRig(i18n::Locale loc) {
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

cr::Check chk(const char* id, cr::Level l, const char* key, cr::ActionKind a, const char* arg) {
    cr::Check c;
    c.id = id;
    c.section = cr::Section::Windows;
    c.level = l;
    c.text_key = key;
    c.action = a;
    c.action_arg = arg;
    return c;
}

// 0: otomatik duzeltilebilir (Oyun Modu); 1: guc plani (AutoFix adli ama yalniz yonlendirme); 2: Windows ayar sayfasi;
// 3: resmi sayfa (surucu).
std::vector<cr::Check> mixed_checks() {
    return {chk("windows.game_mode", cr::Level::Advice, "chk.win.game_mode_off", cr::ActionKind::AutoFix, "fix.game_mode"),
            chk("windows.power", cr::Level::Advice, "chk.win.power_efficiency", cr::ActionKind::AutoFix, "fix.power"),
            chk("windows.vrr", cr::Level::Advice, "chk.win.vrr_off", cr::ActionKind::OpenSettings, "ms-settings:display-advancedgraphics"),
            chk("graphics.driver", cr::Level::Advice, "chk.win.vrr_off", cr::ActionKind::OpenUrl, "https://www.nvidia.com/Download/index.aspx")};
}

ViewModel vm_with(std::vector<cr::Check> checks) {
    ViewModel vm;
    vm.version = "0.9.0";
    vm.readiness.checks = std::move(checks);
    vm.readiness.net_available = true;
    vm.readiness.loading = false;  // tarama bitti (varsayilan "taraniyor": dugmeler devre disi)
    return vm;
}

UiState state_on(Page p) {
    UiState st;
    st.reduced_motion = true;
    st.page = p;
    return st;
}
}  // namespace

EF_TEST(readiness_page_shows_fix_button_only_for_real_fixes) {
    for (const auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        FixRig rig(loc);
        EF_CHECK(rig.ok);
        if (!rig.ok) return;
        auto st = state_on(Page::Readiness);
        const auto hits = rig.paint(st, vm_with(mixed_checks()));
        EF_CHECK(hits.meta.count(hit_of(HitKind::ChkAction, 0)) == 1);   // Oyun Modu: gercek duzeltme
        EF_CHECK(hits.meta.count(hit_of(HitKind::ChkAction, 1)) == 0);   // fix.power: yonlendirme, dugme yok
        EF_CHECK(hits.meta.count(hit_of(HitKind::ChkAction, 2)) == 0);   // Windows ayar sayfasi: dugme yok
        EF_CHECK(hits.meta.count(hit_of(HitKind::ChkAction, 3)) == 0);   // web sayfasi: dugme yok
        EF_CHECK(st.readiness_actions.size() >= 4 && st.readiness_actions[0] && !st.readiness_actions[1] && !st.readiness_actions[2] &&
                  !st.readiness_actions[3]);
        const std::string fix_label(rig.cat.get(i18n::StringId::ChkActionFix));
        if (const auto it = hits.meta.find(hit_of(HitKind::ChkAction, 0)); it != hits.meta.end())
            EF_CHECK(!it->second.name.empty() && !fix_label.empty());
    }
}

EF_TEST(panel_fix_button_follows_real_fixability) {
    FixRig rig(i18n::Locale::TrTR);
    EF_CHECK(rig.ok);
    if (!rig.ok) return;
    // Otomatik duzeltilebilir bulgu var: "Duzelt" etkin ve yalniz o bulgunun satir dugmesi cizilir.
    auto st = state_on(Page::Overview);
    auto hits = rig.paint(st, vm_with(mixed_checks()));
    EF_CHECK(hits.meta.count(Hit::OpenReadiness) == 1 && hits.activatable(Hit::OpenReadiness));
    EF_CHECK(hits.meta.count(hit_of(HitKind::ChkAction, 1)) == 0 && hits.meta.count(hit_of(HitKind::ChkAction, 2)) == 0);
    const auto plan = cr::plan_fix_all(mixed_checks(), {});
    EF_CHECK(plan.items.size() == 1);
    // Yalniz yonlendiren bulgular: "Duzelt" yerinde ama devre disi; satirlarda hicbir eylem dugmesi yok.
    std::vector<cr::Check> only_redirects = mixed_checks();
    only_redirects.erase(only_redirects.begin());
    auto st2 = state_on(Page::Overview);
    hits = rig.paint(st2, vm_with(only_redirects));
    EF_CHECK(hits.meta.count(Hit::OpenReadiness) == 1 && !hits.activatable(Hit::OpenReadiness));
    EF_CHECK(st2.panel_readiness_actions.empty());
    // Yerlesim kaymaz: devre disi dugme etkin dugmeyle ayni konum ve boyutta.
    const RectF* a = nullptr;
    auto st3 = state_on(Page::Overview);
    const auto hits_on = rig.paint(st3, vm_with(mixed_checks()));
    a = hits_on.rect_of(Hit::OpenReadiness);
    const RectF* b = hits.rect_of(Hit::OpenReadiness);
    EF_CHECK(a && b && a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h);
}

EF_TEST(panel_shows_fix_all_result_in_existing_slot) {
    FixRig rig(i18n::Locale::TrTR);
    EF_CHECK(rig.ok);
    if (!rig.ok) return;
    auto vm = vm_with(mixed_checks());
    vm.readiness.notice_key = "chk.notice.fix_all_done";
    vm.readiness.notice_check_id = cr::kFixAllNoticeId;
    vm.readiness.notice_n = 2;
    EF_CHECK(readiness_notice_fill(L"{n} ayar düzeltildi.", vm.readiness) == L"2 ayar düzeltildi.");
    vm.readiness.notice_failed = 1;
    EF_CHECK(readiness_notice_fill(L"{n} / {m}", vm.readiness) == L"2 / 1");
    auto st = state_on(Page::Overview);
    const auto hits = rig.paint(st, vm);  // bildirim anahtari katalogda yoksa ham anahtar gosterilir; cizim cokmez
    EF_CHECK(hits.meta.count(Hit::OpenReadiness) == 1);
}
