#include "app/model_loader.h"

#include <ctime>
#include <string>

#include "core/adapters/windows/update_check.h"
#include "core/diagnosis/calibration.h"
#include "core/diagnosis/components.h"
#include "core/inventory/disk_space.h"
#include "core/inventory/health.h"
#include "core/inventory/power_security.h"
#include "core/readiness/readiness.h"
#include "platform/windows/games/library_scan.h"
#include "platform/windows/inventory/cpu_mem.h"
#include "platform/windows/inventory/driver_info.h"
#include "platform/windows/inventory/gpu_storage.h"
#include "platform/windows/inventory/health_win.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/inventory/power_security_win.h"
#include "platform/windows/inventory/problem_devices_reader.h"
#include "platform/windows/inventory/update_status.h"
#include "platform/windows/readiness/display_reader.h"

namespace ef::app {

namespace inv = platform::inventory;
namespace ci = core::inventory;

namespace {
std::string join(const std::string& a, const std::string& b, const char* sep = " ") {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + sep + b;
}
}  // namespace

void load_system(ui::ViewModel& vm) {
    const auto os = inv::read_os();
    vm.os = join(os.product_name.value.value_or(""), os.display_version.value.value_or(""));
    if (os.build.has() && !vm.os.empty()) {
        vm.os += " (" + std::to_string(*os.build.value);
        if (os.ubr.has()) vm.os += "." + std::to_string(*os.ubr.value);
        vm.os += ")";
    }
    const auto cpu = inv::read_cpu();
    vm.cpu = cpu.brand.value.value_or("");
    if (!vm.cpu.empty() && cpu.topology.physical_cores.has() && cpu.topology.logical_processors.has())
        vm.cpu += " · " + std::to_string(*cpu.topology.physical_cores.value) + "C/" + std::to_string(*cpu.topology.logical_processors.value) + "T";
    vm.updates.cpu_vendor = cpu.vendor.value.value_or("");
    std::string gpus;
    vm.gpu_cards.clear();
    for (const auto& g : inv::read_gpus()) {
        if (g.kind == ci::GpuKind::Software || g.kind == ci::GpuKind::Remote || !g.name.has()) continue;
        // Spec 2026-11-12 H: Guncellemeler ekran karti satiri (uretici basina bir satir; resmi sayfa dugmesi).
        const std::uint32_t vid = g.vendor_id.value.value_or(0);
        bool seen = false;
        for (const auto& u : vm.updates.gpus) seen = seen || u.vendor_id == vid;
        if (!seen) vm.updates.gpus.push_back({*g.name.value, vid, g.driver_version.value.value_or("")});
        std::string one = *g.name.value;
        if (g.kind == ci::GpuKind::Discrete && g.dedicated_vram_mib.has())
            one += " · " + std::to_string((*g.dedicated_vram_mib.value + 512) / 1024) + " GB";
        gpus = join(gpus, one, "\n");
        vm.gpu_cards.push_back({*g.name.value, one, g.luid.value.value_or("")});
    }
    vm.gpu = gpus;
    for (const auto& d : inv::read_display_drivers())
        if (d.version.has()) vm.gpu_driver = join(vm.gpu_driver, *d.version.value, " / ");
    const auto mem = inv::read_memory();
    if (mem.installed_mib.has()) {
        vm.ram = std::to_string((*mem.installed_mib.value + 512) / 1024) + " GB";
        const auto& mods = mem.smbios.populated;
        if (!mods.empty()) {
            const auto& m = mods.front();
            std::string detail = m.type.value.value_or("");
            if (m.configured_mts.has()) detail = join(detail, std::to_string(*m.configured_mts.value) + " MT/s");
            vm.ram += " · " + std::to_string(mods.size()) + "×" + (m.size_mib.has() ? std::to_string(*m.size_mib.value / 1024) + " GB" : "?");
            if (!detail.empty()) vm.ram += " " + detail;
        }
    }
    const auto board = inv::read_board();
    vm.board = join(board.board_vendor.value.value_or(""), board.board_product.value.value_or(""));
    if (!vm.board.empty() && board.bios_version.has()) vm.board += " · BIOS " + *board.bios_version.value;
    vm.updates.board_vendor = board.board_vendor.value.value_or("");
    vm.updates.board_model = board.board_product.value.value_or("");
    vm.updates.bios_version = board.bios_version.value.value_or("");
    vm.updates.bios_date = board.bios_date.value.value_or("");
    vm.updates.problem_devices = inv::read_problem_devices();  // T17 salt okuma
    vm.updates.problem_devices_loaded = true;
    // Panel Sistem karti "Ekran" hucresi: etkin ekran yollari (salt okuma; ayar degismez).
    vm.display = core::readiness::display_summary(platform::readiness::read_displays());
    vm.system_loading = false;
}

void load_findings(ui::ViewModel& vm, const core::diagnosis::BenchmarkRuntime& runtime,
                   const std::optional<core::diagnosis::PcieLoadReading>& pcie_load) {
    vm.findings.clear();
    const auto hin = inv::collect_health_input();
    const auto health = ci::evaluate_health(hin);
    for (const auto& f : health) vm.findings.push_back({std::string(ci::text_key(f.code)), true});
    {
        // DIAG-05/07: bilesen raporu (envanterden; calisma ani teshis yoksa CPU/GPU "karar yok").
        core::diagnosis::ComponentInput ci_in;
        ci_in.inventory = hin;
        ci_in.health = health;
        ci_in.runtime = runtime.cls;  // DIAG-11: son Uygulama Testi sahnesi (kapi acikken dolu)
        ci_in.runtime_measured = runtime.measured;  // B10: kapi kapaliyken de "olculdu" bilinir
        if (pcie_load) {  // C2: denge testi S3 sirasinda okunan link (yuk altinda)
            ci_in.pcie_load = core::diagnosis::pcie_sample_from(*pcie_load);
            ci_in.pcie_load_text = core::diagnosis::pcie_link_text(*pcie_load);
            ci_in.pcie_measured_under_load = core::diagnosis::evaluate_pcie_under_load(*pcie_load).measured_under_load;
        }
        // BM-02: yayin kapisi uretimde bagli; kapaliyken UI nedeni acikca gosterir.
        const auto gate = core::diagnosis::diagnosis_release_status();
        ci_in.diagnosis_release_ready = gate.ready;
        vm.diagnosis_gate_key = gate.reason_key;
        for (const auto& v : ci::advise_volumes(inv::read_volumes(), inv::read_disks(), platform::games::scan_installed_games()))
            if (v.games > 0) ci_in.games.push_back({v.largest_game.empty() ? v.root : v.largest_game, v.games_on_hdd, v.free_pct});
        vm.components = core::diagnosis::component_report(ci_in);
        vm.components_loading = false;
    }
    const auto today = static_cast<std::int64_t>(std::time(nullptr) / 86400);
    const auto status = inv::read_update_status();
    for (const auto& f : core::adapters::windows::evaluate_update_check(inv::read_display_drivers(), status, today).findings)
        vm.findings.push_back({std::string(core::adapters::windows::text_key(f.code)), true});
    for (auto r : core::adapters::windows::advise_for_game_session(status).risks)
        if (r != core::adapters::windows::UpdateRisk::RebootPending)  // ayni bulgu yukarida
            vm.findings.push_back({std::string(core::adapters::windows::text_key(r)), true});
    for (auto n : ci::power_security_notes(inv::read_power_security())) {
        // Guvenlik ozellikleri bilgi notudur (uyari degil, kapatma onerisi yok).
        const bool info = n == ci::PowerNote::HvciRunning || n == ci::PowerNote::VbsRunning ||
                          n == ci::PowerNote::BatteryStatusUnknown;
        if (n == ci::PowerNote::BatteryStatusUnknown) continue;  // masaustunde gurultu
        vm.findings.push_back({std::string(ci::note_key(n)), !info});
    }
    vm.findings_loading = false;
}

void load_storage(ui::ViewModel& vm) {
    vm.storage = ci::advise_volumes(inv::read_volumes(), inv::read_disks(), platform::games::scan_installed_games());
    vm.storage_loading = false;
}

}  // namespace ef::app
