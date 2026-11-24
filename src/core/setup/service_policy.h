#pragma once
// ADR-033 ek: yardimci hizmetin SCM hata eylemleri politikasi (saf). Amac: hizmet kurulumda bir kez kurulur ve her kosulda
// ayakta kalir (guncelleme yeniden baslatmasi, cokme, tekrarlayan durus, yeniden baslatma); Performans Acik/Kapali UAC'siz kalir.
// Saha 2026-11-17: ucuncu eylem NONE idi ve sifirlama 24 saatti; gunde 3. durusta hizmet durmus kaldi ve her ayar UAC istedi.
// SCM son eylemi sonraki tum hatalarda tekrarlar: uc RESTART = sinirsiz yeniden baslatma (5 sn arayla). Hizmetin yapabildigi
// islem listesi (service_handles_op) buradan etkilenmez.
#include <array>
#include <cstdint>

namespace ef::core::setup {

enum class FailureActionKind : std::uint8_t { None, Restart, Reboot, RunCommand };

struct FailureAction {
    FailureActionKind kind = FailureActionKind::None;
    std::uint32_t delay_ms = 0;
    friend bool operator==(const FailureAction&, const FailureAction&) = default;
};

struct FailureConfig {
    std::uint32_t reset_seconds = 0;
    std::uint32_t action_count = 0;
    std::array<FailureAction, 3> actions{};
    bool on_noncrash_failures = false;  // sifir-disi hizmet cikis kodu da hata sayilir (guncelleme yeniden baslatmasi bunu kullanir)
};

inline constexpr std::uint32_t kFailureResetSeconds = 3600;
inline constexpr std::uint32_t kRestartDelayMs = 5000;

inline constexpr FailureConfig required_failure_config() {
    FailureConfig c;
    c.reset_seconds = kFailureResetSeconds;
    c.action_count = 3;
    for (auto& a : c.actions) a = {FailureActionKind::Restart, kRestartDelayMs};
    c.on_noncrash_failures = true;
    return c;
}

// Okunan yapilandirma gereken politikayi karsiliyor mu (kurulum geri okumasi ve hizmet acilis onarimi ayni olcut).
inline bool failure_config_ok(const FailureConfig& c) {
    const auto want = required_failure_config();
    if (c.reset_seconds == 0 || c.reset_seconds > want.reset_seconds || !c.on_noncrash_failures || c.action_count < 3) return false;
    for (std::size_t i = 0; i < 3; ++i)
        if (c.actions[i].kind != FailureActionKind::Restart || c.actions[i].delay_ms > 60000) return false;
    return true;
}

}  // namespace ef::core::setup
