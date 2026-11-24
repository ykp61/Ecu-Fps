#pragma once
// WIN-02: W08 yenileme hizi, W09 gorsel efektler, W10 surec guc kisitlamasi, W11 surec onceligi.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "core/recovery/engine.h"
#include "core/time/time.h"

namespace ef::core::adapters::windows {

// ---------------- W08 ----------------
struct DisplayMode {
    std::uint32_t width = 0, height = 0, hz = 0;
    bool operator==(const DisplayMode&) const = default;
};

// Uyumluluk B6 (bosluk 10): hedef ekran. `gdi` doluysa ekran GDI aygit adiyla ("\\.\DISPLAY<k>") secilir; sira
// numarasi yalniz gosterim/eski kayit icindir. `gdi` bossa (eski journal kaydi) masaustune bagli aygitlarin sirasi.
struct DisplayRef {
    std::uint32_t index = 0;
    std::string gdi;  // tam GDI aygit adi (UTF-8) ya da bos
};

class DisplayApi {
public:
    virtual ~DisplayApi() = default;
    virtual contracts::Result<DisplayMode> current(const DisplayRef& display) = 0;
    virtual contracts::Result<std::vector<DisplayMode>> modes(const DisplayRef& display) = 0;
    virtual contracts::Status test(const DisplayRef& display, const DisplayMode& m) = 0;  // surucu kabul ediyor mu
    virtual contracts::Status set(const DisplayRef& display, const DisplayMode& m) = 0;
    // CHK: Windows Ayarlar gibi kalici (yeniden baslatmada korunur). Varsayilan: oturumluk set.
    virtual contracts::Status set_persistent(const DisplayRef& display, const DisplayMode& m) { return set(display, m); }
};

// Ayni cozunurlukte desteklenen en yuksek yenileme hizi (yoksa nullopt).
std::optional<std::uint32_t> highest_refresh(const std::vector<DisplayMode>& modes, const DisplayMode& cur);

// GDI aygit adi "\\.\DISPLAY<k>" (k: 1-3 rakam) -> hedef belirteci "DISPLAY<k>". Baska bicim: bos (tahmin yok).
std::string gdi_display_token(std::string_view gdi_device);
// "display:<n>:refresh" (eski) | "display:<n>:refresh:DISPLAY<k>" -> DisplayRef (gdi = "\\.\DISPLAY<k>").
contracts::Result<DisplayRef> parse_display_target(std::string_view target);
// Yeni duzeltme hedefi; token gecersizse bos.
std::string format_display_target(std::uint32_t index, std::string_view token);

// Hedef "display:<n>:refresh[:DISPLAY<k>]", deger "<hz>"
class RefreshRateAdapter final : public recovery::SettingAdapter {
public:
    explicit RefreshRateAdapter(DisplayApi& api, bool persistent = false) : api_(api), persistent_(persistent) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

private:
    DisplayApi& api_;
    bool persistent_ = false;
};

// Ekran degisikligi onay zamanlayicisi: kullanici sure icinde "koru" demezse geri alinmali.
class DisplayConfirmation {
public:
    explicit DisplayConfirmation(Duration timeout = Duration::from_ms(15000)) : timeout_(timeout) {}
    void start(MonoTime now) { started_ = now; active_ = true; confirmed_ = false; }
    void confirm() { if (active_) { confirmed_ = true; active_ = false; } }
    // true: sure doldu ve onay yok -> geri al.
    bool should_revert(MonoTime now) const;
    bool active() const noexcept { return active_; }
    std::int64_t remaining_ms(MonoTime now) const;

private:
    Duration timeout_;
    MonoTime started_{};
    bool active_ = false, confirmed_ = false;
};

// ---------------- W09 ----------------
enum class VisualEffect { ClientAreaAnimation, MenuAnimation, ComboBoxAnimation, ListBoxSmoothScrolling, CursorShadow, DropShadow, Transparency };
std::string_view to_string(VisualEffect e) noexcept;
std::optional<VisualEffect> parse_visual_effect(std::string_view s) noexcept;  // "fontsmoothing" asla gecerli degil

class VisualEffectsApi {
public:
    virtual ~VisualEffectsApi() = default;
    virtual contracts::Result<bool> get(VisualEffect e) = 0;
    virtual contracts::Status set(VisualEffect e, bool on) = 0;
};

// Hedef "vfx:<ad>", deger "0"/"1"
class VisualEffectAdapter final : public recovery::SettingAdapter {
public:
    explicit VisualEffectAdapter(VisualEffectsApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

private:
    VisualEffectsApi& api_;
};

// ---------------- W10 / W11 ----------------
struct ProcessId {
    std::uint32_t pid = 0;
    std::uint64_t creation = 0;  // FILETIME (100 ns); PID yeniden kullanimina karsi
};
// "proc:<pid>:<creation>:priority" | "proc:<pid>:<creation>:power_throttling"
struct ProcessTarget {
    ProcessId id;
    bool priority = true;  // false: power_throttling
};
contracts::Result<ProcessTarget> parse_process_target(std::string_view s);
std::string format_process_target(const ProcessTarget& t);

enum class PriorityClass { Idle, BelowNormal, Normal, AboveNormal, High, Realtime };
std::string_view to_string(PriorityClass p) noexcept;
std::optional<PriorityClass> parse_priority(std::string_view s) noexcept;
// Urun yalniz Normal ve AboveNormal YAZAR (katalog W11). Geri yuklemede onceki sinif aynen yazilir.
bool is_writable_priority(PriorityClass p) noexcept;

struct ThrottleState {
    std::uint32_t control = 0;  // PROCESS_POWER_THROTTLING_* kontrol maskesi
    std::uint32_t state = 0;
    bool operator==(const ThrottleState&) const = default;
};

class ProcessApi {
public:
    virtual ~ProcessApi() = default;
    // Surec yasiyor ve olusturulma zamani eslesiyor mu? Degilse MissingValue (bitti/PID yeniden kullanildi).
    virtual contracts::Status verify(const ProcessId& id) = 0;
    virtual contracts::Result<PriorityClass> get_priority(const ProcessId& id) = 0;
    virtual contracts::Status set_priority(const ProcessId& id, PriorityClass p) = 0;
    virtual contracts::Result<ThrottleState> get_throttling(const ProcessId& id) = 0;
    virtual contracts::Status set_throttling(const ProcessId& id, ThrottleState s) = 0;
};

class ProcessAdapter final : public recovery::SettingAdapter {
public:
    explicit ProcessAdapter(ProcessApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    // Surec bittiyse geri yukleme gereksiz ve guvenli: hedef "yok" (RolledBack sayilir).
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;
    bool target_gone(const contracts::SettingOperation& op) override;

    static std::string encode(ThrottleState s);
    static std::optional<ThrottleState> decode_throttle(std::string_view v);

private:
    ProcessApi& api_;
};

inline constexpr std::uint32_t kThrottleExecutionSpeed = 0x1;  // PROCESS_POWER_THROTTLING_EXECUTION_SPEED

// ---- Uyumluluk B3 (bosluk 25): olcum sirasinda BU surecin EcoQoS'tan cikmasi ----
// Pencere simge durumunda / ortuluyken Windows (Intel hibrit, dizustu) arka plan surecini EcoQoS'a alir: olcum is
// parcaciklari verimlilik cekirdeklerine / dusuk saate iner, CPU adimlari kararsizlasir. Yalniz test suresince
// kendi surecimiz icin EXECUTION_SPEED kisitlamasi kapatilir ve test bitince ONCEKI durum geri yazilir. Bu bir
// sistem ayari degildir (surecle olur, surec bitince kalmaz); baska surece dokunulmaz.
class SelfThrottleApi {
public:
    virtual ~SelfThrottleApi() = default;
    // Okunamazsa (Windows 10: sorgu desteklenmeyebilir) nullopt.
    virtual std::optional<ThrottleState> get() = 0;
    virtual bool set(ThrottleState s) = 0;
};
// Onceki durumdan "EXECUTION_SPEED kisitlamasi kapali" durumu (saf): kontrol biti acilir, durum biti temizlenir;
// diger bitler (ornek IGNORE_TIMER_RESOLUTION) aynen kalir.
ThrottleState execution_speed_opt_out(ThrottleState prev) noexcept;
// RAII: kurucu once onceki durumu okur, sonra opt-out yazar; yikici yalniz yazma basariliysa onceki durumu geri
// yazar. Onceki durum okunamazsa sistemin varsayilani ({0,0}: Windows yonetir) onceki sayilir; uygulama kendi
// sureci icin bu ayari baska yerde yazmaz. Zaten kapaliysa hicbir sey yazilmaz.
class ScopedExecutionSpeedOptOut {
public:
    explicit ScopedExecutionSpeedOptOut(SelfThrottleApi& api);
    ~ScopedExecutionSpeedOptOut();
    ScopedExecutionSpeedOptOut(const ScopedExecutionSpeedOptOut&) = delete;
    ScopedExecutionSpeedOptOut& operator=(const ScopedExecutionSpeedOptOut&) = delete;
    bool active() const noexcept { return active_; }
    // Geri yazma basarili mi (yikicidan once cagrilirsa erken geri yukler; ikinci cagri bir sey yapmaz).
    bool restore();

private:
    SelfThrottleApi& api_;
    ThrottleState prev_{};
    bool active_ = false;
};

}  // namespace ef::core::adapters::windows
