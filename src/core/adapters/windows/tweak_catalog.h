#pragma once
// ADR-035 / T02: derlenmis ayar katalogu. Her girdi SABIT bir kayit degeridir; serbest kayit yolu yok.
// Islem hedefi yalniz "tweak:<id>" kimligidir; anahtar yolu, deger adi, tur ve hedef deger HER ZAMAN bu tablodan okunur
// (IPC'den yol/ad gelmez). Gelen deger tablonun on_value'sundan farkliysa islem reddedilir.
// Kanit/risk etiketleri girdide degil, girdinin grubunda (PlanGroup) durur; bu tablo yalniz "ne yazilir"i bilir.
// Bu surumde tabloda girdi YOKTUR: ilk tuketiciler (T03/T14...) girdilerini kendi gruplariyla ekler.
// Yasak yollar (tweak_entry_valid reddeder, test tum katalogu tarar): Image File Execution Options, Defender, Lsa,
// SecureBoot, DeviceGuard, CI\Config (guvenlik/anti-cheat kapatma ADR-035 degismezi).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/adapters/windows/gaming_toggles.h"  // RegRaw

namespace ef::core::adapters::windows {

enum class TweakHive { User, Machine };  // HKCU (uygulama icinde) / HKLM (yalniz broker, 64-bit gorunum)

// Planlama onkosulu (veri): mevcut deger bu kosulu saglarsa oge plana GIRMEZ (ornegin daha siki bir degeri gevsetme).
enum class TweakSkip {
    None,
    WhenCurrentLowerOrEqual,   // mevcut DWORD <= on_value ise atla (daha kisitlayici deger zaten var)
    WhenCurrentHigherOrEqual,  // mevcut DWORD >= on_value ise atla
    WhenPresent,               // deger zaten varsa (hedefle ayni degilse de) atla; kurum/daha siki ayara dokunulmaz (T14)
};

inline constexpr std::uint32_t kTweakRegDword = 4;  // REG_DWORD
inline constexpr std::uint32_t kTweakRegSz = 1;     // REG_SZ
inline constexpr std::string_view kTweakTargetPrefix = "tweak:";

// ADR-040 m.3 / B3: hedef degerin kaynagi. Fixed = on_value (sabit). InstalledRamKb = kurulu RAM (KiB, DWORD); on_value BOS
// olmak zorundadir (tweak_entry_valid) ve deger yalniz TweakRuntime'dan hesaplanir. Istemci degeri dayatamaz: broker kendi
// runtime'iyla hesaplar ve gelen degeri yalniz karsilastirir.
enum class TweakValueSource { Fixed, InstalledRamKb };

struct TweakEntry {
    std::string_view id;  // kararli kimlik: kucuk harf/rakam/_/. ("telemetry.allow")
    TweakHive hive = TweakHive::User;
    std::wstring_view subkey, name;
    std::uint32_t reg_type = kTweakRegDword;
    std::string_view on_value;   // DWORD icin ondalik metin, SZ icin ASCII metin (InstalledRamKb: bos)
    std::string_view group_key;  // plan_groups() icindeki grup ("plan.xxx")
    TweakSkip skip_if = TweakSkip::None;
    bool needs_reboot = false;   // ADR-040: deger yeniden baslatmadan sonra etkin (plan ogesi op.needs_reboot)
    TweakValueSource value_source = TweakValueSource::Fixed;
};

// ADR-040 m.3: hesaplanan deger turleri icin calisma zamani girdileri. Broker tarafinda broker'in KENDI okumasiyla dolar
// (set_tweak_runtime_provider); istemciden gelen deger yalniz bununla karsilastirilir. Bos alan = okunamadi (deger yok).
struct TweakRuntime {
    std::optional<std::uint64_t> installed_ram_kb;  // GetPhysicallyInstalledSystemMemory (KiB); 0 veya > DWORD = kullanilmaz
};
// Broker sureci baslangicta (platform) kendi okuyucusunu baglar; baglanmamissa bos TweakRuntime doner (hesaplanan
// deger kabul edilmez). Tek sefer, ilk istekten once cagrilir.
using TweakRuntimeProvider = TweakRuntime (*)() noexcept;
void set_tweak_runtime_provider(TweakRuntimeProvider p) noexcept;
TweakRuntime current_tweak_runtime() noexcept;

// ADR-040 m.2 / B2: Win32PrioritySeparation (sched.priority_separation) kapali bayragi. Resmi Microsoft belgesi
// (Win32_OperatingSystem.ForegroundApplicationBoost) varsayilan 2'yi ve yalniz dusuk iki biti (0/1/2) tanimlar; hedef degerin (38)
// istemci varsayilaniyla (2) davranis denkligi ve ust bitlerin (quantum) anlami belgelenmedi -> DOGRULANMADI. Bu yuzden girdi tabloda
// durur (kimlik/yol/deger sabit, geri alma sinanir) ama bayrak kapaliyken: grup plan ogesi uretmez (tweaks_in_group) ve broker
// girdiyi kabul etmez (tweak_admin_allowed). Atlama kurali (mevcut 2 iken atla) BILEREK yok. Acma kosulu: denklik resmi kaynakla
// dogrulanir VE gercek cihazda Evet/Hayir akisiyla uygula -> geri oku -> Kapali turu kaniti gelir (kanit: artifacts/evidence/R-B2.md).
// B3 apply-only kapi: bayrak yalniz YENI yazmayi kapatir. tweak_admin_allowed (broker izin listesi; Apply, Restore, UndoFix,
// Recover icin ortak) bayraga bakmaz, yalniz bicimi denetler; yeni yazma tweak_apply_allowed ve TweakAdapter::capture/apply
// (motor capture'da durur: journal ve kayit yazilmaz) tarafindan reddedilir. Boylece bayrak sonradan kapatilsa da kayitli
// geri alma onceki degeri yazar. (Ek savunma: admin_apply_allowed'a tweak_apply_allowed eklenmesi admin.cpp sahibine notlandi.)
inline constexpr bool kPrioritySeparationItemEnabled = false;
inline constexpr std::string_view kPrioritySeparationId = "sched.priority_separation";

// Derlenmis tablo (bos olabilir).
std::span<const TweakEntry> tweak_catalog() noexcept;
const TweakEntry* find_tweak(std::string_view id) noexcept;
// "tweak:<id>" hedefinden girdi.
const TweakEntry* find_tweak_target(std::string_view target) noexcept;
// Kapali bayrakli girdi (yalniz sched.priority_separation) bayrak kapaliyken listelenmez; `priority_sep_enabled` test icindir.
std::vector<const TweakEntry*> tweaks_in_group(std::string_view group, bool priority_sep_enabled = kPrioritySeparationItemEnabled);
// false = girdi bayragi kapali (plan ve broker bu girdiyi kullanmaz). Diger girdiler her zaman true.
bool tweak_item_enabled(const TweakEntry& e, bool priority_sep_enabled = kPrioritySeparationItemEnabled) noexcept;

// Girdinin hedef degeri ham bicimde (gecersizse nullopt): DWORD ondalik, SZ ASCII. Runtime'siz imza hesaplanan girdide nullopt.
std::optional<RegRaw> tweak_on_raw(const TweakEntry& e);
// Hesaplanan girdi: InstalledRamKb -> kurulu RAM KiB (1..0xFFFFFFFF), yoksa/sigmazsa nullopt. Fixed: runtime yok sayilir.
std::optional<RegRaw> tweak_on_raw(const TweakEntry& e, const TweakRuntime& rt);
// Ayni hedefin islem metni (op.new_value ve read_back bicimi): DWORD ondalik, SZ ASCII.
std::optional<std::string> tweak_on_text(const TweakEntry& e, const TweakRuntime& rt);
// Girdi ic tutarliligi + yasak yol denetimi (katalog testi bunu tum tabloya uygular).
bool tweak_entry_valid(const TweakEntry& e);
// Broker izin listesi bicimi: girdi var, hive'i Machine ve gelen deger hedef degerin tam metni. Bayraga BAKMAZ (kayitli geri
// alma icin; yukaridaki apply-only kapi). Runtime'siz imza hesaplanan girdiyi kabul etmez.
bool tweak_admin_allowed(const TweakEntry* e, std::string_view new_value) noexcept;
// ADR-040: broker'in cagirdigi imza (admin.cpp); rt broker'in kendi okumasi (current_tweak_runtime()).
bool tweak_admin_allowed(const TweakEntry* e, std::string_view new_value, const TweakRuntime& rt) noexcept;
// Yeni yazma kurali: izin listesi bicimi + girdi bayragi acik.
bool tweak_apply_allowed(const TweakEntry* e, std::string_view new_value, const TweakRuntime& rt) noexcept;
// Ayni yeni yazma kurali (sabit girdi, bos runtime), girdi bayragi acikca verilir (test).
bool tweak_admin_allowed_gated(const TweakEntry* e, std::string_view new_value, bool priority_sep_enabled) noexcept;
// true = bu mevcut degerde oge plana girmez (skip_if). cur nullopt = deger yok (atlanmaz). Hedef hesaplanamazsa atlanmaz.
bool tweak_should_skip(const TweakEntry& e, const std::optional<RegRaw>& cur);
bool tweak_should_skip(const TweakEntry& e, const std::optional<RegRaw>& cur, const TweakRuntime& rt);

}  // namespace ef::core::adapters::windows
