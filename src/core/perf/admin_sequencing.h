#pragma once
// v3.5 round 3: ertelenen arka plan islerinin sirasi (saf; birim testlenir). App bu kararlara gore bekletir.
//  - Otomatik Performans geri alma (perf_auto_restore): korunan oyun calisirken ASLA ayar degistirmez; oyun
//    kapaninca izleyici (install_watch_state) yeniden cagirir. Yonetici isi (UAC bekliyor olabilir) suruyorsa is
//    bitince (finish_admin_job) yeniden denenir.
//  - Acilista onaylanan makine kurtarmasi (Recover): Evet sonrasi reddedilmez; oyun, yonetici isi veya
//    Performans uygulama/geri yukleme bitene kadar bekletilir ve sonra kendiliginden baslar (tek UAC).

namespace ef::core::perf {

enum class DeferredRun { Now, AfterGameExit, AfterAdminJob, AfterPerf };

constexpr DeferredRun auto_restore_when(bool game_running, bool admin_busy) noexcept {
    if (game_running) return DeferredRun::AfterGameExit;
    if (admin_busy) return DeferredRun::AfterAdminJob;
    return DeferredRun::Now;
}

constexpr DeferredRun machine_recovery_when(bool game_running, bool admin_busy, bool perf_busy) noexcept {
    if (game_running) return DeferredRun::AfterGameExit;
    if (admin_busy) return DeferredRun::AfterAdminJob;
    if (perf_busy) return DeferredRun::AfterPerf;
    return DeferredRun::Now;
}

}  // namespace ef::core::perf
