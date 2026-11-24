#pragma once
// Saha 2026-11-15: kurtarma ("Geri donus tamamlanamadi") ekraninda hangi ayarin neden geri alinamadigi. Saf bicim
// yardimcisi (cizim yok; katalog arama islevi verilir); birim testlenir. Veri journal'dan gelir, uydurulmaz.

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/adapters/windows/appx_remove.h"
#include "core/adapters/windows/defender_exclusion.h"
#include "core/recovery/restore_report.h"
#include "ui/shell/view_model.h"

namespace ef::ui {

// Gorunum ogeleri: ayni ad + neden + ayrinti tek satir (ayni ayar birden cok kayitta olabilir), journal sirasi korunur.
inline std::vector<PerfView::RecoveryItemVM> recovery_items_vm(const std::vector<core::recovery::FailedRestoreItem>& items) {
    std::vector<PerfView::RecoveryItemVM> out;
    for (const auto& i : items) {
        PerfView::RecoveryItemVM vm;
        vm.name_key = core::recovery::restore_item_name_key(i);
        vm.reason_key = std::string(core::recovery::restore_reason_key(i.reason));
        // Oturumluk hizmet: hizmetin adi (Windows hizmet kisa adi; kisisel veri degil) gosterilir.
        if (i.op == contracts::OperationId::ServiceSessionStop && i.target.rfind("svcstop:", 0) == 0) vm.detail = i.target.substr(8);
        if (i.op == contracts::OperationId::ServiceStartTypeSet && i.target.rfind("svcstart:", 0) == 0) vm.detail = i.target.substr(9);  // T12
        if (i.op == contracts::OperationId::DefenderExclusionSet) {  // T11: klasor yolu (oyun klasoru; kisisel veri degil, kullanicinin onayladigi satir)
            if (const auto p = core::adapters::windows::defender_path_from_target(i.target)) vm.detail = *p;
        }
        if (i.op == contracts::OperationId::AppxPackageRemove) {  // T13: urun adi (derlenmis liste)
            if (const auto f = core::adapters::windows::appx_family_from_target(i.target))
                if (const auto* p = core::adapters::windows::find_appx_package(*f)) vm.detail = std::string(p->display_name);
        }
        bool dup = false;
        for (const auto& o : out) dup = dup || o == vm;
        if (!dup) out.push_back(std::move(vm));
    }
    return out;
}

// "Geri alinamayan: Pencereli oyun iyilestirmeleri (geri okuma dogrulanamadi), ... ve 2 ayar daha". Bos liste -> bos.
// text: katalog anahtari -> metin (bilinmeyen anahtar bos doner; o oge yine sayilir ama adsiz yazilmaz).
inline std::wstring recovery_items_text(const std::vector<PerfView::RecoveryItemVM>& items,
                                        const std::function<std::wstring(std::string_view)>& text, std::size_t shown = 3, std::string_view list_key = "perf.recovery_items") {
    if (items.empty()) return {};
    const auto replace = [](std::wstring s, std::wstring_view key, const std::wstring& val) {
        if (const auto p = s.find(key); p != std::wstring::npos) s.replace(p, key.size(), val);
        return s;
    };
    std::wstring list;
    std::size_t listed = 0;
    for (const auto& i : items) {
        if (listed == shown) break;
        std::wstring name = text(i.name_key);
        if (name.empty()) continue;
        if (!i.detail.empty()) name += L" — " + std::wstring(i.detail.begin(), i.detail.end());
        const std::wstring reason = text(i.reason_key);
        if (!list.empty()) list += L", ";
        list += reason.empty() ? name : name + L" (" + reason + L")";
        ++listed;
    }
    if (list.empty()) return {};
    if (items.size() > listed) list += L", " + replace(text("perf.recovery_items_more"), L"{n}", std::to_wstring(items.size() - listed));
    return replace(text(list_key), L"{items}", list);
}

}  // namespace ef::ui
