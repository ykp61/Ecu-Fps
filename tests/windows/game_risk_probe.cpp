// F-59/F-60: gerçek Steam başlatma seçenekleri, CS2 video ayarları ve koruma servisleri — salt okuma.
// Ham seçenek metni yazdırılmaz (gizli bilgi olabilir); yalnız sayı, uzunluk ve risk kodları.
#include <windows.h>

#include <algorithm>
#include <cstdio>

#include <string>
#include <string_view>
#include <vector>

#include "core/adapters/windows/appcompat_layers.h"
#include "core/cleanup/admin_transfer.h"
#include "core/games/config_risk.h"
#include "core/games/consent.h"
#include "core/games/exe_resolver.h"
#include "core/games/process_risk.h"
#include "platform/windows/adapters/win_display_process.h"
#include "platform/windows/adapters/win_power_subs.h"
#include "platform/windows/adapters/win_gaming.h"
#include "platform/windows/games/library_scan.h"
#include "platform/windows/games/protection_services.h"
#include "platform/windows/games/steam_config.h"
#include "platform/windows/games/upscaler_scan.h"
#include "platform/windows/journal_fs.h"
#include "platform/windows/win_env.h"

using namespace ef;

int main() {
    const auto launch = platform::games::steam_launch_options();
    std::printf("game_risk: launch_scanned=%d launch_apps=%zu", launch.scanned ? 1 : 0, launch.options.size());
    const auto it = launch.options.find("730");
    if (it == launch.options.end()) {
        std::printf(" cs2_options=absent");
    } else {
        const auto risks = core::games::scan_launch_options(it->second);
        std::printf(" cs2_options=present len=%zu risks=%zu", it->second.size(), risks.size());
    }
    const auto video = platform::games::steam_cs2_video();
    if (!video) {
        std::printf(" cs2_video=absent");
    } else {
        const auto risks = core::games::scan_cs2_video(*video);
        std::printf(" cs2_video=present risks=%zu", risks.size());
    }
    // F-43: kendi sürecimizde okuyucuyu doğrula (oyun sürecine dokunmadan).
    const auto st = platform::adapters::read_process_state(GetCurrentProcessId());
    const auto pr = core::games::scan_process_state(st);
    std::printf(" process_known=%d priority=%u affinity=0x%llX/0x%llX risks=%zu", st.known ? 1 : 0,
                st.priority_class.value_or(0), static_cast<unsigned long long>(st.affinity_mask.value_or(0)),
                static_cast<unsigned long long>(st.system_mask.value_or(0)), pr.size());
    // F-57: kurulu oyun klasörlerinde upscaler DLL taraması (paylaşılan bütçe, salt okuma).
    const auto games = platform::games::scan_installed_games();
    std::size_t budget = 12000, scanned = 0, assets = 0;
    std::string kinds;
    for (const auto& g : games) {
        if (budget == 0) break;
        const auto us = platform::games::scan_upscalers(g.install_dir, std::min<std::size_t>(budget, 4000));
        if (us.scanned) ++scanned;
        assets += us.assets.size();
        for (const auto& a : us.assets) {
            const std::string k(core::games::to_string(a.kind));
            if (kinds.find(k) == std::string::npos) kinds += (kinds.empty() ? "" : ",") + k;
        }
        budget -= std::min(budget, us.files_seen);
    }
    std::printf(" upscaler_games=%zu upscaler_scanned=%zu upscaler_assets=%zu upscaler_kinds=%s", games.size(), scanned,
                assets, kinds.empty() ? "-" : kinds.c_str());
    // F-47: EXE cozumu + HKCU AppCompatFlags\Layers jeton durumu (SALT OKUMA; yol/oyun adi yazdirilmaz).
    {
        core::games::ConsentStore consent;
        if (const auto dir = platform::app_data_dir()) {
            platform::WinJournalFs fs(*dir);
            if (auto txt = fs.read("consent.json"); txt.ok()) {
                if (auto parsed = core::games::ConsentStore::parse(txt.value()); parsed.ok()) consent = std::move(parsed).value();
            }
        }
        std::vector<std::string> observed;
        for (const auto& e : consent.list()) observed.push_back(e.id);
        platform::adapters::WinUserRegistryApi reg;
        core::adapters::windows::AppCompatLayerAdapter appcompat(reg);  // token_state yalniz okur
        std::size_t resolved = 0, token = 0, other = 0, absent = 0, read_fail = 0;
        for (const auto& g : games) {
            const auto r = core::games::resolve_game_exe(g.install_dir, g.exe_hint, observed);
            if (r.status != core::games::ExeResolveStatus::Resolved) continue;
            ++resolved;
            const auto state = appcompat.token_state(core::adapters::windows::appcompat_target_for_exe(r.path));
            if (!state.ok()) ++read_fail;
            else if (!state.value()) ++absent;          // deger yok = jeton yok
            else if (*state.value()) ++token;
            else ++other;                                // baska katmanlar var, jeton yok
        }
        std::printf(" appcompat_games=%zu appcompat_resolved=%zu appcompat_token=%zu appcompat_other_layers=%zu appcompat_absent=%zu appcompat_read_fail=%zu",
                    games.size(), resolved, token, other, absent, read_fail);
    }
    const auto ev = platform::games::scan_protection_services();
    std::printf(" protection_query=%d eac=%d/%d be=%d/%d vanguard=%d/%d\n", ev.query_ok ? 1 : 0, ev.eac_installed ? 1 : 0,
                ev.eac_running ? 1 : 0, ev.be_installed ? 1 : 0, ev.be_running ? 1 : 0, ev.vanguard_installed ? 1 : 0,
                ev.vanguard_running ? 1 : 0);
    // WIN-08 ek: yonetici temizlik protokolu izin listesi + round-trip (SALT OKUMA; UAC/yazma yok).
    {
        const auto& admin = core::cleanup::admin_product_categories();
        core::cleanup::AdminScanRequest req;
        req.categories = admin;
        req.volume = "C:\\";
        const auto dec = core::cleanup::decode_admin_scan_request(core::cleanup::encode_admin_scan_request(req));
        const bool roundtrip = dec.ok() && dec.value().categories == admin && dec.value().volume == "C:\\";
        std::printf(" admin_cleanup_categories=%zu admin_cleanup_roundtrip=%d\n", admin.size(), roundtrip ? 1 : 0);
    }
    // F-32/F-33/F-42: etkin planin AC alt ayarlari (SALT OKUMA; yazma/UAC yok). Okunamayan "-1".
    {
        platform::adapters::WinPowerSubApi pa;
        auto val = [&](std::string_view t) -> int {
            const auto r = pa.read_active_ac(t);
            return r.ok() ? static_cast<int>(r.value().ac_index) : -1;
        };
        std::printf(" power_subs boost=%d parking=%d aspm=%d\n", val(core::adapters::windows::kPowerBoostAcTarget),
                    val(core::adapters::windows::kPowerParkingAcTarget), val(core::adapters::windows::kPowerAspmAcTarget));
    }
    return 0;
}
