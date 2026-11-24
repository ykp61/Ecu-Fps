// EF-X20 K15 gercek tur bulgusu: arka planda doldurulan bilesen raporu ana modele aktarilirken yayin kapisi
// anahtari (diag.gate.*) dusuyordu; Olcumler sayfasindaki "Bilesen raporu" karti kapi kapali metnini hic
// gostermiyordu (BM-02). Aktarim tek yardimciyla yapilir ve kapi anahtari da tasinir.
#include <utility>

#include "ef_test.h"
#include "ui/shell/view_model.h"

using namespace ef::ui;

EF_TEST(component_report_adopt_carries_gate_key) {
    ViewModel loaded;
    loaded.findings.push_back({"health.example", true});
    loaded.components.resize(2);
    loaded.diagnosis_gate_key = "diag.gate.no_real_holdout";

    ViewModel vm;  // uygulamanin ana modeli: yukleme suruyor, kapi anahtari bos
    EF_CHECK(vm.components_loading && vm.findings_loading && vm.diagnosis_gate_key.empty());
    adopt_component_report(vm, std::move(loaded.findings), std::move(loaded.components),
                           std::move(loaded.diagnosis_gate_key));
    EF_CHECK(vm.diagnosis_gate_key == "diag.gate.no_real_holdout");
    EF_CHECK(vm.findings.size() == 1 && vm.findings[0].text_key == "health.example");
    EF_CHECK(vm.components.size() == 2);
    EF_CHECK(!vm.components_loading && !vm.findings_loading);
}

EF_TEST(component_report_adopt_replaces_stale_gate_key) {
    ViewModel vm;
    vm.diagnosis_gate_key = "diag.gate.ready";  // eski deger kalmamali
    adopt_component_report(vm, {}, {}, "diag.gate.no_real_holdout");
    EF_CHECK(vm.diagnosis_gate_key == "diag.gate.no_real_holdout");
    adopt_component_report(vm, {}, {}, {});
    EF_CHECK(vm.diagnosis_gate_key.empty());
}
