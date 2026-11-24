#pragma once
// P2 WP3: arka plan surecini DUSUK ONCELIK + EcoQoS'a alan journal'li adaptor (kapatma/askiya alma YOK).
// Hedef "bgproc:<pid>:<olusturma>:<yol ozeti 16 hex>", deger "low". Kimlik (pid + olusturma zamani + goruntu yolu
// ozeti) uc parcayi birden dogrular: geri alma yalniz AYNI surec ornegine dokunur. Mevcut ProcessAdapter'a dokunulmaz
// (o yalniz Normal/AboveNormal yazar; bu ayri islem kimligiyle YALNIZ dusurur).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/adapters/windows/display_process.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

struct BgProcessTarget {
    ProcessId id;
    std::string path_hash;  // sha256(kucuk harf, '\\' normalize yol) ilk 16 hex
};
contracts::Result<BgProcessTarget> parse_bg_process_target(std::string_view s);
std::string format_bg_process_target(const BgProcessTarget& t);
// Goruntu yolundan kimlik ozeti (platformdan bagimsiz, saf).
std::string bg_path_hash(std::string_view image_path);

// Platform arayuzu. Gercek surum yalniz PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION ile acar.
class BackgroundProcessApi {
public:
    virtual ~BackgroundProcessApi() = default;
    // Surec yasiyor ve olusturulma zamani eslesiyor mu? Degilse MissingValue (bitti/PID yeniden kullanildi).
    virtual contracts::Status verify(const ProcessId& id) = 0;
    virtual contracts::Result<std::string> image_path(const ProcessId& id) = 0;
    virtual contracts::Result<PriorityClass> get_priority(const ProcessId& id) = 0;
    virtual contracts::Status set_priority(const ProcessId& id, PriorityClass p) = 0;
    virtual contracts::Result<ThrottleState> get_throttling(const ProcessId& id) = 0;
    virtual contracts::Status set_throttling(const ProcessId& id, ThrottleState s) = 0;
};

class BackgroundProcessAdapter final : public recovery::SettingAdapter {
public:
    explicit BackgroundProcessAdapter(BackgroundProcessApi& api) : api_(api) {}
    // Yakalama: yalniz Normal/BelowNormal/AboveNormal oncelikli ve EcoQoS'u zaten acik olmayan surecler. Kullanicinin
    // High/Realtime ya da zaten Idle yaptigi surece dokunulmaz (Denied).
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    // "low" (Idle + EcoQoS acik) ya da kanonik "prio=<ad>;thr=<kontrol:durum>"; surec yok/baska ornek: nullopt.
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    // Surec bittiyse/yol degistiyse dokunulmaz (RolledBack: geri alinacak hedef yok).
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;
    bool target_gone(const contracts::SettingOperation& op) override;

private:
    // Kimlik dogrulamasi: surec yasiyor + yol ozeti hedefle ayni. MissingValue = kimlik tutmuyor/yok.
    contracts::Status check_identity(const BgProcessTarget& t);
    BackgroundProcessApi& api_;
};

}  // namespace ef::core::adapters::windows
