#pragma once
// OPT-07: tek tik sistem kontrolu ve iyilestirme akisi (saf durum makinesi; birim testlenir).
//  Tara -> bulgular "otomatik duzeltilebilir" (kullanici kapsami, journal'li plan ogesi) ve "rehberli"
//  (BIOS, surucu guncellemesi, donanim, yonetici) olarak ayrilir -> Evet/Hayir -> gercek ilerlemeyle
//  journal'li uygulama -> istege bagli A/B olcum -> rapor.
//  - Otomatik BIOS yazimi ve sessiz surucu kurulumu YOK: bunlar her zaman rehberli adim metnidir.
//  - Rehberli bulgu uygulanmis sayilmaz; rapor "kullanicinin yapacagi adim" olarak listeler.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/perf/controller.h"

namespace ef::core::perf {

enum class FixKind { Auto, GuidedBios, GuidedDriver, GuidedHardware, GuidedAdmin, GuidedWindowsSetting };
std::string_view to_string(FixKind k) noexcept;

struct CheckFinding {
    std::string code;           // "health.xmp_off", "vfx.animations_on" ...
    FixKind kind = FixKind::GuidedHardware;
    std::optional<PlanItem> item;  // yalniz Auto icin (journal'li, geri alinabilir)
    std::vector<std::string> step_keys;  // rehberli adimlar (Turkce metin anahtarlari)
    std::vector<PlanItem> extra_items;  // ayni gruptaki diger adimlar (tek Evet/Hayir ile birlikte)
};

enum class CheckPhase { Idle, Scanning, Review, Applying, Measuring, Report, Failed };

struct CheckDeps {
    std::function<std::vector<CheckFinding>()> scan;
    // Secilen otomatik ogeleri journal'li uygular; ilerleme gercek adim sayisindan (done/total).
    std::function<Outcome(const Plan&, const std::function<void(std::size_t, std::size_t)>&)> apply;
    std::function<std::optional<double>()> measure_ab;  // istege bagli: A/B farki (yoksa nullopt)
    std::function<void(std::function<void()>)> run_background;
    std::function<void(std::function<void()>)> post_ui;
};

struct CheckReport {
    std::size_t auto_applied = 0, auto_skipped_by_user = 0;
    std::vector<CheckFinding> guided;  // kullanicinin yapacaklari
    OutcomeKind outcome = OutcomeKind::NothingToDo;
    std::optional<double> ab_delta;    // olculduyse
};

class SystemCheck {
public:
    explicit SystemCheck(CheckDeps d) : d_(std::move(d)) {}
    void start();                              // Idle/Report/Failed -> Scanning -> Review
    void set_selected(std::size_t i, bool on);  // yalniz Auto bulgu, Review'da
    void answer(bool yes, bool measure);       // Review -> Applying (Evet) / Report (Hayir: hicbir sey yazilmaz)
    CheckPhase phase() const { return phase_; }
    const std::vector<CheckFinding>& findings() const { return findings_; }
    std::pair<std::size_t, std::size_t> progress() const { return {done_, total_}; }
    const std::optional<CheckReport>& report() const { return report_; }
    std::function<void()> on_change;

private:
    void set(CheckPhase p);
    CheckDeps d_;
    CheckPhase phase_ = CheckPhase::Idle;
    std::vector<CheckFinding> findings_;
    std::vector<bool> selected_;
    std::size_t done_ = 0, total_ = 0;
    std::optional<CheckReport> report_;
};

}  // namespace ef::core::perf
