#pragma once
// UI-01: ana ekran modelini gercek envanterden doldurur (salt okuma). Arka plan is parcaciginda
// cagrilir; UI is parcacigini bloklamaz.

#include "core/diagnosis/mismatch.h"
#include "ui/shell/view_model.h"

namespace ef::app {

// Hizli kisim: OS/CPU/GPU/RAM/anakart.
void load_system(ui::ViewModel& vm);
// Yavas kisim: saglik + guncelleme + guc/guvenlik bulgulari.
// DIAG-11 + B10: runtime kalici son sahne kanitli Uygulama Testi kaydindan (sinif yalniz kapi acikken; kapali: measured).
// Spec 2026-11-12 C2: pcie_load son denge kaydinin S3 adiminda (GPU yuk altinda) okunan linkidir; verilirse anakart
// satiri evaluate_pcie kuraliyla bu okumadan degerlendirilir (bostaki envanter okumasi onun yerine gecmez).
void load_findings(ui::ViewModel& vm, const core::diagnosis::BenchmarkRuntime& runtime = {},
                   const std::optional<core::diagnosis::PcieLoadReading>& pcie_load = std::nullopt);
// DIAG-08: birimler + diskler + kurulu oyunlar -> doluluk ve bos alan onerisi.
void load_storage(ui::ViewModel& vm);

}  // namespace ef::app
