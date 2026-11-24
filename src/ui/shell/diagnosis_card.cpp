// DIAG-05: Olcumler sayfasinda "Bilesen raporu" karti. Gercek envanterden (INV-06) + varsa calisma ani
// teshisinden. Her satir: bilesen, hukum (Sorun bulunmadi / Sinirlayabilir / Karar yok) ve kanit ya da
// karar verilmeme nedeni. Yuzde/puan yok.
#include <windows.h>

#include <string>
#include <vector>

#include "core/measure/balance_view.h"
#include "ui/shell/panel_text.h"
#include "ui/shell/screen.h"

namespace ef::ui {

using core::i18n::StringId;
namespace dg = core::diagnosis;

namespace {
std::wstring wide8(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
StringId comp_name(dg::Component c) {
    switch (c) {
        case dg::Component::Cpu: return StringId::DiagCompCpu;
        case dg::Component::Gpu: return StringId::DiagCompGpu;
        case dg::Component::Motherboard: return StringId::DiagCompMotherboard;
        case dg::Component::Ram: return StringId::DiagCompRam;
        case dg::Component::Storage: return StringId::DiagCompStorage;
    }
    return StringId::DiagCompCpu;
}
// Kanit belirteci "onek:deger" ya da "a;b". Onek metne cevrilir; teknik deger parantezde (olculen veri).
std::vector<std::string> split(std::string_view s, char d) {
    std::vector<std::string> out;
    std::size_t p = 0;
    while (p <= s.size()) {
        const auto q = s.find(d, p);
        out.emplace_back(s.substr(p, q == std::string_view::npos ? std::string_view::npos : q - p));
        if (q == std::string_view::npos) break;
        p = q + 1;
    }
    return out;
}
std::wstring put_res(std::wstring s, const std::wstring& res) {
    const std::wstring k = L"{res}";
    for (auto i = s.find(k); i != std::wstring::npos; i = s.find(k)) s.replace(i, k.size(), res);
    return s;
}
}  // namespace

void Screen::components_card(Graphics& g, CardSlot& slot, const ViewModel& vm, HitMap& hits, StatusSeq& seq) {
    const auto& p = pal();
    const float pad = theme_.card_pad;
    const float inner_w = slot.area.w - 2 * pad;
    const float section_h = theme_.role(Role::Section).line;
    const float bl = theme_.role(Role::Body).line;
    const auto sub = t(StringId::DiagSubtitle);
    const float sh = g.measure(Role::Secondary, sub, inner_w).h;
    // BM-02: yayin kapisi durumu acikca yazilir (kapi kapaliyken siniflandirici hukum yayinlamaz).
    // Kart: tek kisa durum cumlesi; uzun gerekce UIA HelpText'te kalir (kisaltilmis, silinmedi).
    const bool gate_ready = vm.diagnosis_gate_key == "diag.gate.ready";
    const auto gate_full = vm.components_loading || vm.diagnosis_gate_key.empty() ? std::wstring() : tk(vm.diagnosis_gate_key);
    const auto gate = gate_full.empty() ? std::wstring() : tk(gate_ready ? "diag.gate.open_short" : "diag.gate.closed_short");
    const float gate_h = gate.empty() ? 0.f : g.measure(Role::Secondary, gate, inner_w).h;
    // Durum rozeti sutunu en uzun gercek metne gore; karar metni kesilmez.
    float badge_w = 150.f;
    for (const auto& f : vm.components)
        badge_w = std::max(badge_w, std::ceil(g.measure(Role::Secondary, tk(dg::verdict_key(f)), 1000.f, false).w) + theme_.space(2));
    badge_w = std::min(badge_w, inner_w * 0.45f);
    const float text_w = std::max(80.f, inner_w - badge_w - theme_.space(2));
    struct Row {
        std::wstring name, badge, detail, help;
        Color c;
        float h;
    };
    std::vector<Row> rows;
    if (!vm.components_loading) {
        for (const auto& f : vm.components) {
            Row r{t(comp_name(f.component)), {}, {}, {}, p.text_secondary, 0};
            r.badge = tk(dg::verdict_key(f));
            if (f.verdict == dg::Verdict::NoIssueFound) r.c = p.success;
            else if (f.verdict == dg::Verdict::Limiting) r.c = p.warning;
            // Kapi kapaliyken CPU/GPU: kendi Darboğaz testinin SAHNE sonucu (olculmus; genel teshis degil) gosterilir.
            std::optional<core::diagnosis::BalanceVerdict> scene;
            std::size_t scene_row = 0;
            if (f.verdict == dg::Verdict::Abstain && f.abstain_reason == "diagnosis_calibration_unverified" &&
                (f.component == dg::Component::Cpu || f.component == dg::Component::Gpu) && vm.measure.balance_latest) {
                const auto& bv = *vm.measure.balance_latest;
                scene_row = balance_display_row(vm.display).value_or(1);  // ekran satiri; bilinmiyorsa 1440p
                const auto v = bv.rows[scene_row].verdict;
                if (v == core::diagnosis::BalanceVerdict::CpuLimited || v == core::diagnosis::BalanceVerdict::GpuLimited ||
                    v == core::diagnosis::BalanceVerdict::Balanced)
                    scene = v;
            }
            if (scene) {
                const bool cpu = f.component == dg::Component::Cpu;
                const bool lim = (cpu && *scene == core::diagnosis::BalanceVerdict::CpuLimited) ||
                                 (!cpu && *scene == core::diagnosis::BalanceVerdict::GpuLimited);
                const bool balanced = *scene == core::diagnosis::BalanceVerdict::Balanced;
                r.badge = tk(lim ? "diag.scene.limiting" : balanced ? "diag.scene.balanced" : "diag.scene.not_limiting");
                r.c = lim ? p.warning : p.success;
                const auto res = tk(std::string(core::measure::balance_row_key(scene_row)));
                r.detail = put_res(tk("diag.scene.short"), res);
                r.help = put_res(tk("diag.scene.detail"), res);
            } else if (f.verdict == dg::Verdict::Abstain) {
                // Tekrarlanan "kanit yok"/"kapi kapali" gerekcesi her bilesende yazilmaz: alt baslik ve kapi satiri bir kez
                // soyler; satirin tam gerekcesi HelpText'tedir. Bilesene ozgu nedenler gorunur kalir.
                const auto why = tk("diag.abstain_" + f.abstain_reason);
                if (f.abstain_reason == "no_runtime_evidence" || f.abstain_reason == "diagnosis_calibration_unverified") r.help = why;
                else r.detail = why;
            } else {
                for (const auto& e : f.evidence)
                    for (const auto& tok : split(e, ';')) {
                        const auto c = tok.find(':');
                        std::string key = tok.substr(0, c);
                        std::string val = c == std::string::npos ? std::string{} : tok.substr(c + 1);
                        if (key == "runtime") key += "_" + val, val.clear();
                        if (!r.detail.empty()) r.detail += L" · ";
                        r.detail += tk("diag.ev_" + key);
                        if (!val.empty()) r.detail += L" (" + wide8(val) + L")";
                    }
            }
            const float nh = std::max(g.measure(Role::Body, r.name, text_w).h, bl);
            r.h = r.detail.empty() ? nh : nh + theme_.space(0) + g.measure(Role::Secondary, r.detail, text_w).h;
            rows.push_back(std::move(r));
        }
    }
    float total = 0;
    for (const auto& r : rows) total += r.h + theme_.space(3);
    const float loading_h = vm.components_loading ? bl + theme_.space(3) : 0.f;
    RectF card;
    if (!slot.place(pad + section_h + theme_.space(1) + sh + (gate_h > 0 ? theme_.space(1) + gate_h : 0.f) + theme_.space(3) + loading_h +
                        (total > 0 ? total - theme_.space(3) : 0.f) + pad,
                    card))
        return;
    draw_card(g, card, false, depth::kPanelCardRadius);
    float y = card.y + pad;
    g.text(Role::Section, t(StringId::DiagTitle), {card.x + pad, y, inner_w, section_h}, p.text_primary);
    y += section_h + theme_.space(1);
    g.text(Role::Secondary, sub, {card.x + pad, y, inner_w, sh}, p.text_secondary);
    // EF-A11Y: cizilen metin UIA agacinda da okunur (Text; etkilesimsiz). Gorsel degismez.
    {
        const Hit id = seq.take();
        hits.add_text(id, {card.x + pad, card.y + pad, inner_w, y + sh - card.y - pad}, a11y_join(t(StringId::DiagTitle), sub));
        if (const auto it = hits.meta.find(id); it != hits.meta.end()) it->second.help = tk("diag.help");
    }
    y += sh;
    if (gate_h > 0) {
        y += theme_.space(1);
        g.text(Role::Secondary, gate, {card.x + pad, y, inner_w, gate_h}, gate_ready ? p.success : p.warning);
        const Hit id = seq.take();
        hits.add_text(id, {card.x + pad, y, inner_w, gate_h}, gate);
        if (const auto it = hits.meta.find(id); it != hits.meta.end()) it->second.help = gate_full;
        y += gate_h;
    }
    y += theme_.space(3);
    if (vm.components_loading) {
        g.text(Role::Body, t(StringId::OverviewLoading), {card.x + pad, y, inner_w, bl}, p.text_secondary);
        hits.add_text(seq.take(), {card.x + pad, y, inner_w, bl}, t(StringId::OverviewLoading));
        return;
    }
    for (const auto& r : rows) {
        const float nh = std::max(g.measure(Role::Body, r.name, text_w).h, bl);
        g.text(Role::Body, r.name, {card.x + pad, y, text_w, nh}, p.text_primary);
        if (!r.detail.empty())
            g.text(Role::Secondary, r.detail, {card.x + pad, y + nh + theme_.space(0), text_w, r.h - nh - theme_.space(0)}, p.text_secondary);
        g.text(Role::Secondary, r.badge, {card.right() - pad - badge_w, y + 2, badge_w, bl}, r.c, {Align::Trailing, true});
        std::wstring uia = r.name + L": " + r.badge;
        uia = a11y_join(std::move(uia), r.detail);
        const Hit id = seq.take();
        hits.add_text(id, {card.x + pad, y, inner_w, r.h}, std::move(uia));
        if (!r.help.empty())
            if (const auto it = hits.meta.find(id); it != hits.meta.end()) it->second.help = r.help;
        y += r.h + theme_.space(3);
    }
}

}  // namespace ef::ui
