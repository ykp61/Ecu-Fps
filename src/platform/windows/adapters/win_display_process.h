#pragma once
// WIN-02 platform: gercek ekran modu, gorsel efekt (SystemParametersInfo) ve surec API'leri.

#include "core/adapters/windows/display_process.h"
#include "core/games/process_risk.h"

namespace ef::platform::adapters {

class WinDisplayApi final : public core::adapters::windows::DisplayApi {
public:
    contracts::Result<core::adapters::windows::DisplayMode> current(const core::adapters::windows::DisplayRef& display) override;
    contracts::Result<std::vector<core::adapters::windows::DisplayMode>> modes(const core::adapters::windows::DisplayRef& display) override;
    contracts::Status test(const core::adapters::windows::DisplayRef& display, const core::adapters::windows::DisplayMode& m) override;
    // Oturumluk (kayit defterine yazmaz): cokme/yeniden baslatmada Windows kendi kayitli moduna doner.
    contracts::Status set(const core::adapters::windows::DisplayRef& display, const core::adapters::windows::DisplayMode& m) override;
    // CHK: CDS_UPDATEREGISTRY (Windows Ayarlar ile ayni kalicilik)
    contracts::Status set_persistent(const core::adapters::windows::DisplayRef& display, const core::adapters::windows::DisplayMode& m) override;
};

// Uyumluluk B3 (bosluk 25): yalniz BU surecin (GetCurrentProcess) guc kisitlamasi durumu.
class WinSelfThrottleApi final : public core::adapters::windows::SelfThrottleApi {
public:
    std::optional<core::adapters::windows::ThrottleState> get() override;
    bool set(core::adapters::windows::ThrottleState s) override;
};

class WinVisualEffectsApi final : public core::adapters::windows::VisualEffectsApi {
public:
    contracts::Result<bool> get(core::adapters::windows::VisualEffect e) override;
    contracts::Status set(core::adapters::windows::VisualEffect e, bool on) override;
};

class WinProcessApi final : public core::adapters::windows::ProcessApi {
public:
    contracts::Status verify(const core::adapters::windows::ProcessId& id) override;
    contracts::Result<core::adapters::windows::PriorityClass> get_priority(const core::adapters::windows::ProcessId& id) override;
    contracts::Status set_priority(const core::adapters::windows::ProcessId& id, core::adapters::windows::PriorityClass p) override;
    contracts::Result<core::adapters::windows::ThrottleState> get_throttling(const core::adapters::windows::ProcessId& id) override;
    contracts::Status set_throttling(const core::adapters::windows::ProcessId& id, core::adapters::windows::ThrottleState s) override;
};

// Bir surecin olusturulma zamani (FILETIME). Hedef kimligi olusturmak icin.
std::uint64_t process_creation_time(std::uint32_t pid);

// F-43: calisan surecin oncelik sinifi ve affinity maskeleri (salt okuma; PROCESS_QUERY_LIMITED_INFORMATION).
// Okunamayan alan nullopt kalir; hicbiri okunamazsa known=false. Hata firlatmaz, surec baslatmaz.
core::games::ProcessState read_process_state(std::uint32_t pid);

}  // namespace ef::platform::adapters
