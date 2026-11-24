#pragma once
// DIAG-06/BM-02: uretime gomulu kalibrasyon ozeti.
//
// SIMDIKI DURUM: gercek donanim holdout veri seti YOK; kapi kapalidir. Bu dosya yalniz
// `ef_diag_calibrate --emit-header` ciktisiyla, kapisi ACILAN bir veri seti icin degistirilir.
// Sentetik fikstur gercek sayilmadigi icin arac sentetik veri seti icin baslik uretmez.
//
// Yeni baslik uretimi: tools/diag_calibrate ve docs/18_CALIBRATION_PROTOCOL.md.

#include "core/diagnosis/release_gate.h"

namespace ef::core::diagnosis {

inline constexpr ValidatedCalibration kValidatedCalibration{};

}  // namespace ef::core::diagnosis
