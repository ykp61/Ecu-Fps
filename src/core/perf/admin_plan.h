#pragma once
// OPT-10 (plan v3.5): yonetici kapsamli Performans gruplari (broker, tek UAC).
//  plan.hags   : HAGS acik (HwSchMode=2). Yalniz GPU/surucu destegi DOGRULANMISSA; yeniden baslatma gerekir.
//  plan.nvidia : NVIDIA surucu global profili: guc "en yuksek performansi tercih et", Low Latency "Acik",
//                shader onbellek siniri en az 10 GB (daha buyuk/sinirsiz deger dusurulmez) — M3: birim dogrulanana
//                kadar KAPALI (broker::kShaderCacheItemEnabled = false).
// Yalniz hedeften FARKLI olan ogeler plana girer; okunamayan hedef plana girmez (uydurma yok).
// NVIDIA yoksa/DRS acilamazsa drs = nullptr -> grup plana girmez (desteklenmiyor, "veri yetersiz" degil).
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/adapters/nvidia/drs.h"
#include "core/adapters/windows/background.h"
#include "core/adapters/windows/fast_startup.h"
#include "core/adapters/windows/hags.h"
#include "core/adapters/windows/power_subs.h"
#include "core/inventory/devices.h"
#include "core/perf/controller.h"
#include "core/vendors/dispatch.h"

namespace ef::core::perf {

std::vector<PlanItem> hags_plan_items(adapters::windows::HagsApi& api);
// F-32/F-33/F-42: etkin planin AC alt ayarlari (yalniz broker). Ayni islemde plan degisikligi
// (PowerSchemeSet) varsa bos doner: anlik goruntu eski plana, yazma yeni plana giderdi.
std::vector<PlanItem> power_subs_plan_items(adapters::windows::PowerSubApi& api, bool scheme_change_planned);
// A3 (P12 karar 7) melez GPU kapisi: NVIDIA surucu profili YALNIZ hedef bagdastirici DOGRULANMIS NVIDIA ise yazilir.
// Hedef: (a) cagiranin bildigi bagdastirici indeksi (oyun surecinin adaptoru; DXGI GpuAdapter::index) ya da
// (b) sistemde tek donanim GPU'su varsa o. Birden cok donanim GPU'su varken hedef bilinmiyorsa Insufficient
// (ad/DXGI sirasi/"ilk kart" kimlik degildir). Hicbir durumda surucu/enjeksiyon/bellek okumasi yoktur.
//  Allowed     : parca plana girebilir (hala DRS okumasi + yalniz farkli degerler kurali gecerli).
//  Unsupported : hedef kesin NVIDIA degil / NVAPI runtime yok / uretici x ozellik tablosunda yazma satiri yok.
//  Insufficient: hedef adaptor ya da ureticisi bilinmiyor (veri yetersiz). "Desteklenmiyor" DEGILDIR.
enum class GpuGateState { Allowed, Unsupported, Insufficient };
struct NvidiaGate {
    GpuGateState state = GpuGateState::Insufficient;
    std::string reason_key;                      // Allowed iken bos; aksi halde "plan.nvidia.gate.*"
    std::optional<std::uint32_t> adapter_index;  // dogrulanan NVIDIA bagdastiricisi (yalniz Allowed)
    bool allowed() const noexcept { return state == GpuGateState::Allowed; }
};
NvidiaGate nvidia_adapter_gate(const std::vector<inventory::GpuAdapter>& gpus, std::optional<std::uint32_t> target_adapter_index,
                               bool nvapi_runtime_loaded);
// Kapi kapaliysa plana girmeyen parca kaydi (nullopt = kapi acik).
std::optional<PlanOmission> nvidia_omission(const NvidiaGate& g);
// Kapi Allowed degilse (ya da drs yoksa) bos doner.
std::vector<PlanItem> nvidia_plan_items(adapters::nvidia::DrsApi* drs, const NvidiaGate& gate);
// B6 (plan.nvidia_extra, Gelismis, varsayilan Hayir): ayni kapi + ek ogeler yalniz bayrak aciksa. Iki argumanli surum
// broker::DrsExtraFlags{} (admin.h sabitleri, ikisi de KAPALI) ile cagirir.
//  G13 is parcacikli iyilestirme -> 1 (ENABLE); mevcut etkin deger 1 ise oge yok.
//  G14 FXAA -> 0 (OFF); yalniz mevcut etkin deger 1 (acik) ise oge var (varsayilan zaten kapali; bilinmeyen deger dokunulmaz).
struct NvidiaExtraFlags {
    bool threaded_optimization = false;
    bool fxaa_off = false;
};
std::vector<PlanItem> nvidia_plan_items(adapters::nvidia::DrsApi* drs, const NvidiaGate& gate, const NvidiaExtraFlags& extra);
// HAGS duzeltmesi (Oyun Hazirligi "fix.hags") icin tek islem.
contracts::SettingOperation hags_on_operation();
// F-48: hizli baslatma duzeltmesi (Oyun Hazirligi "fix.fast_startup") icin tek islem; yeniden baslatma gerekmez,
// bir sonraki kapanistan itibaren gecerlidir.
contracts::SettingOperation fast_startup_off_operation();
// Hedef bir yonetici (broker) duzeltmesi mi? UI "Duzelt/Geri al/Koru" yolunu secerken kullanir.
bool admin_fix_target(std::string_view target) noexcept;
// WIN-10: kullanicinin sectigi izinli hizmetler icin oturumluk durdurma islemleri (makine, broker).
// Yalniz derlenmis izin listesindeki, su an calisan ve durdurma onkosullari (bagimli/is denetimi)
// gecen hizmetler plana girer; uygun olmayan hedef sessizce atlanir. Kullanici secimi bos ise plan bos.
std::vector<PlanItem> service_stop_plan_items(adapters::startup::ServiceApi& api,
                                              adapters::windows::ServiceBusyProbe& probe,
                                              const std::vector<std::string>& selected);

// EF-X16: broker izin listesindeki tek sabit istek.
contracts::SettingOperation restore_point_operation();
// T08: Performans Acik oncesi otomatik geri yukleme noktasi (grup "plan.restore_point"). Saf karar:
//  - Plan zaten yonetici (UAC) ogesi icermiyorsa oge YOK (yalniz bunun icin ek UAC istenmez).
//  - Sistem Geri Yukleme kapali biliniyorsa oge YOK (uygulama acmaz; broker da ProtectionOff'ta acmaz).
//  - Son nokta 24 saatten yeniyse oge YOK; yas bilinmiyorsa oge VAR (Windows kendi siklik sinirini uygular, atlarsa
//    "atlandi" durustce bildirilir).
std::optional<PlanItem> restore_point_plan_item(bool plan_has_admin_items, std::optional<bool> system_restore_enabled,
                                                std::optional<std::int64_t> last_restore_point_age_s);
inline constexpr std::int64_t kRestorePointMinAgeSeconds = 24 * 3600;
// Plandaki geri yukleme noktasi ogesi (varsa) tek basina yonetici gerektirir; secimden sonra baska yonetici ogesi kalmadiysa
// bu oge dusurulur (yalniz onun icin UAC acilmaz). Diger ogeler ve sira korunur.
void drop_lone_restore_point(std::vector<PlanItem>& items);
// Yonetici ogelerinde geri yukleme noktasi ogesini basa alir (ayni broker oturumunda ilk adim).
void restore_point_first(std::vector<PlanItem>& admin_items);
bool is_restore_point_item(const PlanItem& i) noexcept;

// A1.1 (F-02): oyun EXE'si icin NVIDIA surucu profili islemleri (guc: en yuksek performans, hazirlanan kare: 1).
// Yalniz surucude profili bulunan oyunlar icin; yeni surucu profili olusturulmaz.
std::vector<contracts::SettingOperation> nvidia_game_profile_ops(const std::string& exe_name);

// Satir durumu journal'dan: iki islem ayri kayittir, biri yazilip digeri yazilamadiysa "ayarli" denmez.
// Conflict: deger bu uygulamadan sonra degismis; geri alma yazmaz, tek cikis "Mevcut degeri koru".
// Attention: kesilmis veya dogrulanamamis geri alma; deger bu uygulamanindir denemez.
enum class GameProfileRecord { None, Active, Partial, Attention, Conflict };
GameProfileRecord game_profile_record(const std::string& exe_name, const std::vector<std::string>& active,
                                      const std::vector<std::string>& recovery, const std::vector<std::string>& interrupted,
                                      const std::vector<std::string>& conflict);

}  // namespace ef::core::perf
