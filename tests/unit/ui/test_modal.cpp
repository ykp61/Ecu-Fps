// Spec 2026-11-12 E: uygulama ici kip penceresi. Durum makinesi (ilk odak Hayir, Tab kartin icinde doner, Esc = Hayir,
// Enter yalniz odaktaki ogeyi calistirir, sessiz varsayilan Hayir, isaret kutusu / secim degeri), isabet kimlikleri,
// odak sirasi ve "ana pencere yokken soru sorulmaz" kurali. Ayrica urun kaynaginda Windows sinifi onay penceresi
// (TaskDialog / MessageBox) cagrisi kalmadigi dogrulanir.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "core/adapters/windows/dns.h"
#include "core/i18n/catalog.h"
#include "ef_test.h"
#include "ui/consent/consent_dialog.h"
#include "ui/consent/fix_dialogs.h"
#include "ui/shell/main_window.h"
#include "ui/shell/modal.h"
#include "ui/shell/screen.h"

using namespace ef::ui;

namespace {
ModalSpec plain() {
    ModalSpec s;
    s.title = L"Uygulansin mi?";
    s.body = {L"60 Hz -> 165 Hz", L"Geri alinabilir"};
    s.yes = L"Evet";
    s.no = L"Hayir";
    return s;
}
std::vector<ModalItem> items(const ModalState& m) {
    std::vector<ModalItem> v;
    for (const auto& f : modal_focus_order(m)) v.push_back(f.item);
    return v;
}
}  // namespace

// Ilk odak Hayir'dadir: pencereyi acan Enter / yanlislikla Enter hicbir seyi onaylamaz.
EF_TEST(modal_first_focus_is_no) {
    ModalState m = modal_open(plain());
    EF_CHECK(!m.done);
    EF_CHECK((m.focus == ModalFocus{ModalItem::No, 0}));
    modal_key(m, ModalKey::Enter);
    EF_CHECK(m.done);
    EF_CHECK(m.result.answer == ModalAnswer::No);
    EF_CHECK(!m.result.dismissed);  // Hayir dugmesi: karar verildi
    // Bitmis pencere baska girdi almaz.
    modal_key(m, ModalKey::Tab);
    modal_activate(m, {ModalItem::Yes, 0});
    EF_CHECK(m.result.answer == ModalAnswer::No);
}

// Sessiz varsayilan (pencere gosterilemedi / kapatildi): Hayir, dismissed.
EF_TEST(modal_default_is_no) {
    const ModalResult r = modal_not_shown();
    EF_CHECK(r.answer == ModalAnswer::No && r.dismissed && !r.checked && r.choice == -1);
    EF_CHECK(ModalResult{}.answer == ModalAnswer::No);
    ModalState m = modal_open(plain());
    EF_CHECK(m.result == modal_not_shown());  // yanit gelmeden okunan sonuc da Hayir'dir
    modal_dismiss(m);
    EF_CHECK(m.done && m.result.answer == ModalAnswer::No && m.result.dismissed);
    // Yanit verildikten sonra kapatma yaniti degistirmez.
    ModalState y = modal_open(plain());
    modal_activate(y, {ModalItem::Yes, 0});
    modal_dismiss(y);
    EF_CHECK(y.result.answer == ModalAnswer::Yes && !y.result.dismissed);
}

// Tab kartin icinde doner: Hayir -> Evet -> (isaret kutusu) -> Hayir; Shift+Tab tersine.
EF_TEST(modal_tab_cycles_inside_card) {
    ModalState m = modal_open(plain());
    EF_CHECK((items(m) == std::vector<ModalItem>{ModalItem::No, ModalItem::Yes}));
    modal_key(m, ModalKey::Tab);
    EF_CHECK(m.focus.item == ModalItem::Yes);
    modal_key(m, ModalKey::Tab);
    EF_CHECK(m.focus.item == ModalItem::No);  // sarar; karttan cikmaz
    modal_key(m, ModalKey::ShiftTab);
    EF_CHECK(m.focus.item == ModalItem::Yes);
    modal_key(m, ModalKey::ShiftTab);
    EF_CHECK(m.focus.item == ModalItem::No);
    EF_CHECK(!m.done);

    ModalSpec s = plain();
    s.checkbox = L"Cokme kayitlarini da ekle";
    ModalState c = modal_open(s);
    EF_CHECK((items(c) == std::vector<ModalItem>{ModalItem::Check, ModalItem::No, ModalItem::Yes}));
    EF_CHECK(c.focus.item == ModalItem::No);  // isaret kutusu olsa da ilk odak Hayir
    modal_key(c, ModalKey::Tab);
    EF_CHECK(c.focus.item == ModalItem::Yes);
    modal_key(c, ModalKey::Tab);
    EF_CHECK(c.focus.item == ModalItem::Check);
    modal_key(c, ModalKey::Tab);
    EF_CHECK(c.focus.item == ModalItem::No);
    modal_key(c, ModalKey::ShiftTab);
    EF_CHECK(c.focus.item == ModalItem::Check);
    // Sol / Sag dugme satirinda yalniz odagi tasir.
    ModalState a = modal_open(plain());
    modal_key(a, ModalKey::Right);
    EF_CHECK(a.focus.item == ModalItem::Yes && !a.done);
    modal_key(a, ModalKey::Right);
    EF_CHECK(a.focus.item == ModalItem::Yes);
    modal_key(a, ModalKey::Left);
    EF_CHECK(a.focus.item == ModalItem::No && !a.done);
}

// Esc = Hayir (odak nerede olursa olsun) ve "karar verilmedi" olarak isaretlenir.
EF_TEST(modal_escape_is_no) {
    for (const ModalItem at : {ModalItem::No, ModalItem::Yes}) {
        ModalState m = modal_open(plain());
        if (at == ModalItem::Yes) modal_key(m, ModalKey::Tab);
        EF_CHECK(m.focus.item == at);
        modal_key(m, ModalKey::Escape);
        EF_CHECK(m.done && m.result.answer == ModalAnswer::No && m.result.dismissed);
    }
}

// Enter / Bosluk yalniz odaktaki ogeyi calistirir: ortuk "varsayilan Evet" yoktur.
EF_TEST(modal_enter_activates_focused_only) {
    ModalState m = modal_open(plain());
    modal_key(m, ModalKey::Tab);  // Evet
    modal_key(m, ModalKey::Enter);
    EF_CHECK(m.done && m.result.answer == ModalAnswer::Yes && !m.result.dismissed);

    ModalState sp = modal_open(plain());
    modal_key(sp, ModalKey::Tab);
    modal_key(sp, ModalKey::Space);
    EF_CHECK(sp.done && sp.result.answer == ModalAnswer::Yes);

    // Odak isaret kutusundayken Enter kutuyu degistirir; pencere kapanmaz, hicbir sey onaylanmaz.
    ModalSpec s = plain();
    s.checkbox = L"Cokme kayitlarini da ekle";
    ModalState c = modal_open(s);
    modal_key(c, ModalKey::ShiftTab);
    EF_CHECK(c.focus.item == ModalItem::Check);
    modal_key(c, ModalKey::Enter);
    EF_CHECK(!c.done && c.checked);
    modal_key(c, ModalKey::Space);
    EF_CHECK(!c.done && !c.checked);
}

// Isaret kutusu degeri yanitla birlikte doner (Evet, Hayir ve Esc'te de son deger).
EF_TEST(modal_checkbox_value) {
    ModalSpec s = plain();
    s.checkbox = L"Cokme kayitlarini da ekle";
    {
        ModalState m = modal_open(s);
        EF_CHECK(!m.checked);  // varsayilan kapali
        modal_activate(m, {ModalItem::Yes, 0});
        EF_CHECK(m.result.answer == ModalAnswer::Yes && !m.result.checked);
    }
    {
        ModalState m = modal_open(s);
        modal_activate(m, {ModalItem::Check, 0});
        EF_CHECK(m.checked && !m.done && m.focus.item == ModalItem::Check);
        modal_activate(m, {ModalItem::Yes, 0});
        EF_CHECK(m.result.answer == ModalAnswer::Yes && m.result.checked);
    }
    {
        ModalState m = modal_open(s);
        modal_activate(m, {ModalItem::Check, 0});
        modal_key(m, ModalKey::Escape);
        EF_CHECK(m.result.answer == ModalAnswer::No && m.result.checked);
    }
    {
        ModalSpec on = s;
        on.checkbox_checked = true;
        ModalState m = modal_open(on);
        EF_CHECK(m.checked);
        modal_activate(m, {ModalItem::Check, 0});
        modal_activate(m, {ModalItem::No, 0});
        EF_CHECK(m.result.answer == ModalAnswer::No && !m.result.checked);
    }
    {
        // Kutu yoksa deger her zaman false; kutu ogesi calismaz ve odak sirasinda yoktur.
        ModalSpec none = plain();
        none.checkbox_checked = true;
        ModalState m = modal_open(none);
        EF_CHECK(!m.checked);
        modal_activate(m, {ModalItem::Check, 0});
        EF_CHECK(!m.checked && m.focus.item == ModalItem::No);
    }
}

// Secim listesi (EF-X14 DNS): secim yapilmadan Evet kapali; ok tuslari gezer ve secer; secim yanitla doner.
EF_TEST(modal_choice_gates_yes) {
    ModalSpec s = plain();
    s.choices = {L"Cloudflare", L"Google", L"Quad9"};
    ModalState m = modal_open(s);
    EF_CHECK(!modal_yes_enabled(m) && m.choice == -1);
    EF_CHECK(m.focus.item == ModalItem::No);
    EF_CHECK((items(m) == std::vector<ModalItem>{ModalItem::Choice, ModalItem::No}));  // devre disi Evet Tab almaz
    modal_activate(m, {ModalItem::Yes, 0});
    EF_CHECK(!m.done);  // devre disi Evet calismaz
    modal_key(m, ModalKey::Tab);
    EF_CHECK((m.focus == ModalFocus{ModalItem::Choice, 0}));
    EF_CHECK(m.choice == -1);  // odak gelmesi secim degildir
    modal_key(m, ModalKey::Down);
    EF_CHECK(m.choice == 1 && m.focus.index == 1 && modal_yes_enabled(m));
    modal_key(m, ModalKey::Down);
    modal_key(m, ModalKey::Down);
    EF_CHECK(m.choice == 2);  // uclarda durur
    modal_key(m, ModalKey::Up);
    EF_CHECK(m.choice == 1);
    EF_CHECK((items(m) == std::vector<ModalItem>{ModalItem::Choice, ModalItem::No, ModalItem::Yes}));
    EF_CHECK(modal_focus_order(m).front().index == 1);  // roving: Tab duragi secili secenek
    modal_activate(m, {ModalItem::Choice, 7});
    EF_CHECK(m.choice == 1);  // liste disi indeks etkisiz
    modal_key(m, ModalKey::Tab);
    EF_CHECK(m.focus.item == ModalItem::No);
    modal_key(m, ModalKey::Tab);
    modal_key(m, ModalKey::Enter);
    EF_CHECK(m.done && m.result.answer == ModalAnswer::Yes && m.result.choice == 1);
    // Enter / Bosluk odaktaki secenegi secer.
    ModalState e = modal_open(s);
    modal_key(e, ModalKey::Tab);
    modal_key(e, ModalKey::Space);
    EF_CHECK(e.choice == 0 && !e.done);
    modal_key(e, ModalKey::Escape);
    EF_CHECK(e.result.answer == ModalAnswer::No && e.result.choice == 0);
}

// Geri sayim: "{s}" kalan saniyeyle yazilir; sure dolunca Hayir (dismissed). Geri sayimsiz pencerede tik etkisiz.
EF_TEST(modal_countdown_times_out_to_no) {
    ModalSpec s = plain();
    s.body = {L"{s} saniye icinde onaylamazsaniz eski ayara donulur."};
    s.countdown_s = 3;
    ModalState m = modal_open(s);
    EF_CHECK(modal_body_text(m, 0) == L"3 saniye icinde onaylamazsaniz eski ayara donulur.");
    modal_tick(m);
    EF_CHECK(!m.done && m.seconds_left == 2);
    EF_CHECK(modal_body_text(m, 0).starts_with(L"2 "));
    modal_tick(m);
    modal_tick(m);
    EF_CHECK(m.done && m.result.answer == ModalAnswer::No && m.result.dismissed);
    modal_tick(m);
    EF_CHECK(m.seconds_left == 0);
    ModalState keep = modal_open(s);
    modal_tick(keep);
    modal_activate(keep, {ModalItem::Yes, 0});
    EF_CHECK(keep.result.answer == ModalAnswer::Yes);
    ModalState none = modal_open(plain());
    for (int i = 0; i < 100; ++i) modal_tick(none);
    EF_CHECK(!none.done);
    EF_CHECK(modal_body_text(none, 0) == L"60 Hz -> 165 Hz");
    EF_CHECK(modal_body_text(none, 9).empty());
}

// Govde en cok iki satir, liste / secim sinirli; fazla metin kaybolmaz (son satira eklenir).
EF_TEST(modal_open_bounds_content) {
    ModalSpec s = plain();
    s.body = {L"a", L"", L"b", L"c"};
    for (int i = 0; i < 20; ++i) s.list.push_back(L"m" + std::to_wstring(i));
    for (int i = 0; i < 20; ++i) s.choices.push_back(L"c" + std::to_wstring(i));
    s.countdown_s = -5;
    const ModalState m = modal_open(s);
    EF_CHECK(m.spec.body.size() == kModalMaxBody);
    EF_CHECK(m.spec.body[0] == L"a" && m.spec.body[1] == L"b c");
    EF_CHECK(m.spec.list.size() == kModalMaxList);
    EF_CHECK(m.spec.list.back().find(L"m19") != std::wstring::npos);
    EF_CHECK(m.spec.choices.size() == kModalMaxChoices);
    EF_CHECK(m.spec.countdown_s == 0 && m.seconds_left == 0);
}

// R-C4: yazi alani (oyun sunucusu adresi). Ilk odak alanda; yalniz izinli karakterler; Evet gecerli metinle etkin.
namespace {
ModalSpec with_input() {
    ModalSpec s = plain();
    s.input_label = L"Sunucu IPv4 adresi";
    s.input_allowed = L"0123456789.";
    s.input_max = 15;
    s.input_valid = [](const std::wstring& t) { return t.size() >= 7; };
    return s;
}
}  // namespace

EF_TEST(modal_input_first_focus_typing_and_yes_gate) {
    ModalState m = modal_open(with_input());
    EF_CHECK(modal_has_input(m) && m.focus.item == ModalItem::Input);
    EF_CHECK((items(m) == std::vector<ModalItem>{ModalItem::Input, ModalItem::No}));  // Evet metin gecerli olana dek yok
    EF_CHECK(!modal_yes_enabled(m));
    for (const wchar_t c : std::wstring(L"1.2.3.4")) EF_CHECK(modal_input_char(m, c));
    EF_CHECK(m.text == L"1.2.3.4" && modal_yes_enabled(m));
    EF_CHECK((items(m) == std::vector<ModalItem>{ModalItem::Input, ModalItem::No, ModalItem::Yes}));
    EF_CHECK(!modal_input_char(m, L'a') && !modal_input_char(m, L' ') && !modal_input_char(m, L'\t') && !modal_input_char(m, L'\b'));
    EF_CHECK(m.text == L"1.2.3.4");
    EF_CHECK(modal_input_backspace(m) && m.text == L"1.2.3." && !modal_yes_enabled(m));
    EF_CHECK(modal_input_clear(m) && m.text.empty() && !modal_input_clear(m) && !modal_input_backspace(m));
}

EF_TEST(modal_input_limit_paste_and_result) {
    ModalState m = modal_open(with_input());
    EF_CHECK(modal_input_paste(m, L" 203.0.113.10\r\n"));  // izinsiz karakterler atilir
    EF_CHECK(m.text == L"203.0.113.10");
    EF_CHECK(modal_input_paste(m, L"99999"));  // sinira (15) kadar
    EF_CHECK(m.text.size() == 15 && !modal_input_char(m, L'1'));
    EF_CHECK(!modal_input_paste(m, L"123"));  // dolu: hicbiri eklenmez
    modal_input_clear(m);
    modal_input_paste(m, L"10.0.0.99");
    // Enter / Bosluk alanda hicbir sey calistirmaz; Tab ile Evet'e gelinir
    modal_key(m, ModalKey::Enter);
    modal_key(m, ModalKey::Space);
    EF_CHECK(!m.done && m.focus.item == ModalItem::Input);
    modal_key(m, ModalKey::Tab);
    modal_key(m, ModalKey::Tab);
    EF_CHECK(m.focus.item == ModalItem::Yes);
    modal_key(m, ModalKey::Enter);
    EF_CHECK(m.done && m.result.answer == ModalAnswer::Yes && m.result.text == L"10.0.0.99");
    // Esc: Hayir; metin sonuca tasinsa da yanit Hayir'dir
    ModalState e = modal_open(with_input());
    modal_input_paste(e, L"10.0.0.99");
    modal_key(e, ModalKey::Escape);
    EF_CHECK(e.done && e.result.answer == ModalAnswer::No && e.result.dismissed);
    // Baslangic metni suzulur ve sinirlanir; alan yoksa yazi islevleri etkisizdir
    ModalSpec pre = with_input();
    pre.input_text = L"203.0.113.10 abc";
    EF_CHECK(modal_open(pre).text == L"203.0.113.10");
    ModalState none = modal_open(plain());
    EF_CHECK(!modal_has_input(none) && !modal_input_char(none, L'1') && !modal_input_paste(none, L"12") && none.focus.item == ModalItem::No);
    // Alan oku: Yukari/Asagi/Sol/Sag alanda odagi tasimaz
    ModalState a = modal_open(with_input());
    for (const ModalKey k : {ModalKey::Left, ModalKey::Right, ModalKey::Up, ModalKey::Down}) modal_key(a, k);
    EF_CHECK(a.focus.item == ModalItem::Input && !a.done);
}

EF_TEST(modal_input_hit_and_automation_ids) {
    EF_CHECK(classify_hit(Hit::ModalInput).kind == HitKind::Fixed);
    EF_CHECK(std::find(std::begin(kFixedHits), std::end(kFixedHits), Hit::ModalInput) != std::end(kFixedHits));
    EF_CHECK(hit_automation_id(Hit::ModalInput) == L"modal.input");
    EF_CHECK(modal_hit({ModalItem::Input, 0}) == Hit::ModalInput && modal_focus_of(Hit::ModalInput).item == ModalItem::Input);
    EF_CHECK(classify_hit(Hit::SpeedServerPing).kind == HitKind::Fixed);
    EF_CHECK(std::find(std::begin(kFixedHits), std::end(kFixedHits), Hit::SpeedServerPing) != std::end(kFixedHits));
    EF_CHECK(hit_automation_id(Hit::SpeedServerPing) == L"measure.speed.server_ping");
}

// Kip ogeleri sabit / dinamik bloklarda adlandirilmis kontrollerdir; UIA kimlikleri degismez.
EF_TEST(modal_hits_and_automation_ids) {
    for (const Hit h : {Hit::Modal, Hit::ModalTitle, Hit::ModalBody0, Hit::ModalBody1, Hit::ModalCheck, Hit::ModalNo, Hit::ModalYes}) {
        EF_CHECK(classify_hit(h).kind == HitKind::Fixed);
        EF_CHECK(std::find(std::begin(kFixedHits), std::end(kFixedHits), h) != std::end(kFixedHits));
    }
    EF_CHECK(hit_automation_id(Hit::Modal) == L"modal");
    EF_CHECK(hit_automation_id(Hit::ModalTitle) == L"modal.title");
    EF_CHECK(hit_automation_id(Hit::ModalBody0) == L"modal.body.0");
    EF_CHECK(hit_automation_id(Hit::ModalBody1) == L"modal.body.1");
    EF_CHECK(hit_automation_id(Hit::ModalCheck) == L"modal.check");
    EF_CHECK(hit_automation_id(Hit::ModalNo) == L"modal.no");
    EF_CHECK(hit_automation_id(Hit::ModalYes) == L"modal.yes");
    EF_CHECK(hit_automation_id(hit_of(HitKind::ModalList, 2)) == L"modal.list.2");
    EF_CHECK(hit_automation_id(hit_of(HitKind::ModalChoice, 1)) == L"modal.choice.1");
    EF_CHECK(hit_of(HitKind::ModalChoice, kModalMaxChoices) == Hit::None);
    // Oge <-> kimlik gidis donusu; kip ogesi olmayan kimlik None.
    for (const ModalFocus f : {ModalFocus{ModalItem::No, 0}, ModalFocus{ModalItem::Yes, 0}, ModalFocus{ModalItem::Check, 0},
                               ModalFocus{ModalItem::Choice, 3}})
        EF_CHECK(modal_focus_of(modal_hit(f)) == f);
    EF_CHECK(modal_hit({}) == Hit::None);
    for (const Hit h : {Hit::None, Hit::Yes, Hit::No, Hit::Toggle, nav_hit(0), Hit::Modal, Hit::ModalTitle, hit_of(HitKind::ModalList, 0)})
        EF_CHECK(modal_focus_of(h).item == ModalItem::None);
    // Pencere, baslik ve govde etkilesimsizdir (Tab almaz); isaret kutusu etkilesimlidir.
    EF_CHECK(!interactive(AccRole::Window) && !interactive(AccRole::Text) && interactive(AccRole::CheckBox));
}

// Kip acikken odak sirasi yalniz kart ogeleridir: ust menu sekmeleri ve sayfa ogeleri yoktur.
EF_TEST(modal_focus_order_replaces_page_order) {
    UiState s;
    s.page = Page::Overview;
    const auto page_order = focus_order(s);
    EF_CHECK(std::find(page_order.begin(), page_order.end(), nav_hit(0)) != page_order.end());
    ModalSpec spec = plain();
    spec.checkbox = L"Cokme kayitlarini da ekle";
    ModalState m = modal_open(spec);
    s.modal = &m;
    EF_CHECK((focus_order(s) == std::vector<Hit>{Hit::ModalCheck, Hit::ModalNo, Hit::ModalYes}));
    // Zorunlu guncelleme kartinda da kip sirasi gecerlidir.
    s.update_gate = true;
    EF_CHECK((focus_order(s) == std::vector<Hit>{Hit::ModalCheck, Hit::ModalNo, Hit::ModalYes}));
    s.update_gate = false;
    // Gercek Tab sirasi: cizimde kayitli ve etkin ogeler (devre disi Evet atlanir).
    ModalSpec dns = plain();
    dns.choices = {L"Cloudflare", L"Google"};
    ModalState d = modal_open(dns);
    s.modal = &d;
    HitMap hits;
    hits.add(hit_of(HitKind::ModalChoice, 0), {0, 0, 80, 28}, L"Cloudflare", AccRole::RadioButton);
    hits.add(hit_of(HitKind::ModalChoice, 1), {0, 28, 80, 28}, L"Google", AccRole::RadioButton);
    hits.meta[hit_of(HitKind::ModalChoice, 1)].tab_stop = false;
    hits.add(Hit::ModalNo, {0, 60, 72, 32}, L"Hayir", AccRole::Button);
    hits.add(Hit::ModalYes, {80, 60, 72, 32}, L"Evet", AccRole::Button, false, -1, false);
    EF_CHECK((focus_order(s, hits) == std::vector<Hit>{hit_of(HitKind::ModalChoice, 0), Hit::ModalNo}));
    s.modal = nullptr;
    EF_CHECK(focus_order(s) == page_order);
}

// UIA noktadan oge (ElementProviderFromPoint): kip penceresi kendi ogelerinin kapsayicisidir ve isabet haritasinda
// onlardan once gelir; noktadaki dugme / metin once doner, pencere yalniz kartin bos yerinde.
EF_TEST(modal_element_at_prefers_child_over_window) {
    HitMap hits;
    hits.add(Hit::Modal, {100, 100, 480, 200}, L"Soru?", AccRole::Window);
    hits.add_text(Hit::ModalTitle, {120, 120, 440, 28}, L"Soru?");
    hits.add(Hit::ModalNo, {380, 250, 80, 32}, L"Hayir", AccRole::Button);
    hits.add(Hit::ModalYes, {468, 250, 80, 32}, L"Evet", AccRole::Button, false, -1, false);
    EF_CHECK(hits.element_at(400, 260) == Hit::ModalNo);
    EF_CHECK(hits.element_at(480, 260) == Hit::ModalYes);  // devre disi oge de okunur
    EF_CHECK(hits.element_at(130, 130) == Hit::ModalTitle);
    EF_CHECK(hits.element_at(110, 290) == Hit::Modal);  // kartin bos yeri: pencerenin kendisi
    EF_CHECK(hits.element_at(50, 50) == Hit::None);     // karartilmis icerik
    // Fare: pencere ve metin isabet almaz; devre disi Evet tiklanmaz.
    EF_CHECK(hits.at(400, 260) == Hit::ModalNo && hits.at(480, 260) == Hit::None && hits.at(110, 290) == Hit::None);
}

// Kip penceresini kapatan tus basili tutulursa tekrarlari sayfaya ulasmaz (odak pencereyi acan ogeye doner: tekrar
// ayni soruyu yeniden acardi). Tus birakilip yeniden basilinca ya da baska tusta koruma biter.
EF_TEST(modal_key_guard_swallows_held_closing_key) {
    constexpr unsigned kEnter = 0x0D, kSpace = 0x20, kEsc = 0x1B;
    ModalKeyGuard g;
    EF_CHECK(!g.swallow(kEnter, true) && !g.swallow(kEnter, false));  // koruma yokken hicbir tus yutulmaz
    g.closed_by(kSpace);
    EF_CHECK(g.swallow(kSpace, true) && g.swallow(kSpace, true));  // basili tutulan Bosluk
    EF_CHECK(!g.swallow(kSpace, false));                           // birakilip yeniden basildi: gecer
    EF_CHECK(!g.swallow(kSpace, true));                            // koruma bitti: sonraki tekrarlar da gecer
    g.closed_by(kEsc);
    EF_CHECK(g.swallow(kEsc, true));
    EF_CHECK(!g.swallow(kEnter, true));  // baska tus: koruma biter
    EF_CHECK(!g.swallow(kEsc, true));
}

// Kip acikken karartilmis icerik duragandir: RGB serit ve Performans dugmesi hareketi durur.
EF_TEST(modal_pauses_decorative_motion) {
    StripInputs in;
    in.visible = in.foreground = in.app_animations = in.strip_setting = true;
    EF_CHECK(strip_should_animate(in) && breathe_should_animate(in, true));
    in.modal = true;
    EF_CHECK(!strip_should_animate(in) && !breathe_should_animate(in, true));
}

// Ana pencere yokken (tepside) / sahip ana pencere degilken soru sorulmaz: islem baslatilmaz, varsayilan Hayir.
EF_TEST(modal_callers_default_no_without_main_window) {
    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    if (!cat.ok()) return;
    EF_CHECK(MainWindow::from_hwnd(nullptr) == nullptr);
    for (const HWND owner : {static_cast<HWND>(nullptr), GetDesktopWindow()}) {
        EF_CHECK(MainWindow::from_hwnd(owner) == nullptr);
        EF_CHECK(!ask_fix(owner, cat.value(), L"Soru?", L"Ayrinti"));
        EF_CHECK(!ask_fix(owner, cat.value(), L"Soru?", L"Ayrinti", FixDialogIcon::Warning));
        EF_CHECK(!ask_dns_preset(owner, cat.value(), L"Soru?", L"Ayrinti").has_value());
        EF_CHECK(!confirm_display_keep(owner, cat.value(), 15));
        EF_CHECK(!ask_game_consent(owner, cat.value(), L"Test Oyunu").has_value());  // karar kaydedilmez
    }
}

// Sorularin tanimlari: ayni dizgi anahtarlari, ayni anlam (TaskDialog'dan tasinan icerik).
EF_TEST(modal_specs_keep_strings_and_meaning) {
    using ef::core::i18n::StringId;
    namespace aw = ef::core::adapters::windows;
    auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    EF_CHECK(cat.ok());
    if (!cat.ok()) return;
    const auto& c = cat.value();
    const auto wide = [](std::string_view s) { return std::wstring(s.begin(), s.end()); };  // ASCII karsilastirma icin
    const ModalSpec fix = fix_modal_spec(c, L"Duzeltilsin mi?", L"Onceki degerler kaydedilir.");
    EF_CHECK(fix.title == L"Duzeltilsin mi?" && fix.body.size() == 1 && fix.body[0] == L"Onceki degerler kaydedilir.");
    EF_CHECK(fix.yes == L"Evet" && fix.no == L"Hayır");  // action.confirm / action.cancel
    EF_CHECK(!fix.destructive && fix.choices.empty() && fix.checkbox.empty() && fix.countdown_s == 0);
    EF_CHECK(fix.caption.find(L"Ecu Fps") != std::wstring::npos);
    EF_CHECK(fix_modal_spec(c, L"q", L"").body.empty());  // ayrintisiz soru: govde satiri yok
    // RDY-05: geri donussuz islem (uyari) -> ana dugme hata renginde.
    EF_CHECK(fix_modal_spec(c, L"q", L"d", FixDialogIcon::Warning).destructive);
    const ModalSpec dns = dns_modal_spec(c, L"q", L"d");
    EF_CHECK(dns.choices.size() == std::size(aw::kDnsPresets));
    for (std::size_t i = 0; i < dns.choices.size(); ++i) EF_CHECK(dns.choices[i].find(wide(aw::kDnsPresets[i].name)) != std::wstring::npos);
    EF_CHECK(!modal_yes_enabled(modal_open(dns)));  // varsayilan secim yok: Evet kapali
    const ModalSpec keep = display_keep_modal_spec(c, 15);
    EF_CHECK(keep.countdown_s == 15 && keep.body.size() == 1 && keep.body[0].find(L"{s}") != std::wstring::npos);
    EF_CHECK(keep.yes == L"Koru" && keep.no == L"Geri al" && !keep.destructive);
    EF_CHECK(modal_body_text(modal_open(keep), 0).find(L"15") != std::wstring::npos);
    const ModalSpec consent = game_consent_modal_spec(c, L"Test Oyunu");
    EF_CHECK(consent.title.find(L"Test Oyunu") != std::wstring::npos && consent.title.find(L"{game}") == std::wstring::npos);
    EF_CHECK(consent.body.size() == 1 && consent.yes == L"Evet" && consent.no == L"Hayır");
    EF_CHECK(consent.caption.find(L"Oyun alg") != std::wstring::npos);  // consent.title UIA HelpText'te yasar
}

// Urun kaynaginda Windows sinifi onay / bildirim penceresi cagrisi yoktur (spec E: hepsi kip penceresine tasindi).
EF_TEST(modal_no_windows_dialog_calls_in_product_source) {
    namespace fs = std::filesystem;
    const fs::path src = fs::path(EF_SOURCE_ROOT) / "src";
    EF_CHECK(fs::is_directory(src));
    std::size_t files = 0;
    for (const auto& e : fs::recursive_directory_iterator(src)) {
        if (!e.is_regular_file()) continue;
        const auto ext = e.path().extension().string();
        if (ext != ".cpp" && ext != ".h") continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string text = ss.str();
        ++files;
        for (const char* call : {"TaskDialogIndirect(", "TaskDialog(", "MessageBoxW(", "MessageBoxA(", "MessageBox(", "MessageBoxExW(",
                                 "MessageBoxIndirectW(", "ShellMessageBoxW(", "DialogBoxParamW(", "DialogBoxIndirectParamW("}) {
            if (text.find(call) == std::string::npos) continue;
            std::fprintf(stderr, "  %s contains %s\n", e.path().string().c_str(), call);
            EF_CHECK(false);
        }
    }
    EF_CHECK(files > 50);
}

// Sayfada basili Enter / Bosluk tekrari odaktaki ogeyi yeniden calistirmaz; tekerlek Windows satir ayarina uyar.
EF_TEST(page_keys_and_wheel_follow_windows_input_settings) {
    EF_CHECK(page_activation_accepted(false));
    EF_CHECK(!page_activation_accepted(true));
    EF_CHECK(wheel_scroll_dip(-120, 3) == 48.f);    // varsayilan: eski davranis (centik = 48 DIP asagi)
    EF_CHECK(wheel_scroll_dip(120, 3) == -48.f);    // yukari
    EF_CHECK(wheel_scroll_dip(-120, 1) == 16.f);    // yavas ayar
    EF_CHECK(wheel_scroll_dip(-240, 6) == 192.f);   // iki centik, hizli ayar
    EF_CHECK(wheel_scroll_dip(-120, 0) == 0.f);     // 0 satir: kaydirma yok
    EF_CHECK(wheel_scroll_dip(-120, 0xFFFFFFFFu) == 400.f);  // sayfa kaydirma
    EF_CHECK(wheel_scroll_dip(-60, 3) == 24.f);     // hassas tekerlek (yarim centik)
}
