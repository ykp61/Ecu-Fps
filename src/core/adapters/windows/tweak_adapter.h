#pragma once
// ADR-035 / T02: derlenmis ayar katalogundaki (tweak_catalog.h) kayit degerlerini yazan adaptor.
// Islem: OperationId::TweakValueSet, hedef "tweak:<id>", deger = girdinin on_value'su (baska deger reddedilir).
// Kapsam girdinin hive'ina uyar: User = CurrentUser (HKCU, uygulama icinde), Machine = Machine (HKLM, yalniz broker).
// Yakalama: ham tur + bayt (yoksa "yok"; anahtar da yoksa source_version="key_new"). Geri alma: ham deger birebir yazilir;
// onceden yoksa deger SILINIR (varsayilan uydurulmaz) ve bizim actigimiz bos anahtar kaldirilir.
// read_back: DWORD ondalik metin, SZ metin; beklenmeyen turde "raw:<tur>:<hex>" (capture ile ayni bicim).

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/adapters/windows/gaming_toggles.h"
#include "core/adapters/windows/tweak_catalog.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

// HKLM (64-bit gorunum) erisimi; UserRegistryApi ile ayni sekil + bos anahtari kaldirma. Yazma yalniz broker'da kullanilir.
class MachineRegistryApi {
public:
    virtual ~MachineRegistryApi() = default;
    virtual contracts::Result<std::optional<RegRaw>> read(std::wstring_view subkey, std::wstring_view name) = 0;
    virtual contracts::Status write(std::wstring_view subkey, std::wstring_view name, const RegRaw& v) = 0;
    virtual contracts::Status remove(std::wstring_view subkey, std::wstring_view name) = 0;
    virtual contracts::Result<bool> key_exists(std::wstring_view subkey) = 0;
    // Anahtarda hic deger ve alt anahtar yoksa siler; doluysa dokunmaz (basari).
    virtual contracts::Status remove_key_if_empty(std::wstring_view subkey) = 0;
};

class TweakAdapter final : public recovery::SettingAdapter {
public:
    // hklm bos olabilir (HKLM erisimi olmayan baglam): Machine girdileri okunamaz/yazilamaz sayilir.
    TweakAdapter(UserRegistryApi& hkcu, MachineRegistryApi* hklm) : hkcu_(hkcu), hklm_(hklm) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

    // Girdi duzeyi (plan okumasi ve testler). Islem duzeyi cagrilar once op'u katalogdan cozer (id/kapsam/deger denetimi).
    contracts::Result<std::optional<RegRaw>> read_raw(const TweakEntry& e);
    contracts::Result<recovery::Snapshot> capture_entry(const TweakEntry& e);
    // Hedef deger runtime'dan (hesaplanan girdi) veya on_value'dan; hedef yoksa yazmaz.
    contracts::Status apply_entry(const TweakEntry& e);
    // Islem duzeyi yeni yazma kurali (apply): girdi bayragi acik + new_value adaptorun KENDI hesapladigi hedefle ayni.
    // Istemcinin dayattigi deger (ornegin baska RAM degeri) reddedilir; hicbir sey yazilmaz.
    contracts::Status apply_value(const TweakEntry& e, std::string_view new_value);
    contracts::Result<std::optional<std::string>> read_back_entry(const TweakEntry& e);
    contracts::Status restore_entry(const TweakEntry& e, const recovery::Snapshot& snap);

    bool machine_available() const noexcept { return hklm_ != nullptr; }
    // ADR-040 m.3: hesaplanan degerler icin runtime. Verilmezse current_tweak_runtime() (surecin kendi okuyucusu; broker'da
    // broker'in okumasi). Testler MOCK runtime verir.
    void set_runtime(const TweakRuntime& rt) { rt_ = rt; }
    TweakRuntime runtime() const noexcept { return rt_ ? *rt_ : current_tweak_runtime(); }

private:
    UserRegistryApi& hkcu_;
    MachineRegistryApi* hklm_;
    std::optional<TweakRuntime> rt_;
};

// "tweak:<id>" hedef dizesi.
std::string tweak_target(const TweakEntry& e);

// Bellek ici sahte (birim testleri).
class FakeMachineRegistry final : public MachineRegistryApi {
public:
    std::map<std::wstring, RegRaw> values;  // "subkey|name"
    std::set<std::wstring> keys;            // var olan anahtarlar (deger yazilinca eklenir)
    bool fail_write = false;
    bool fail_read = false;
    int writes = 0, removes = 0;
    contracts::Result<std::optional<RegRaw>> read(std::wstring_view subkey, std::wstring_view name) override;
    contracts::Status write(std::wstring_view subkey, std::wstring_view name, const RegRaw& v) override;
    contracts::Status remove(std::wstring_view subkey, std::wstring_view name) override;
    contracts::Result<bool> key_exists(std::wstring_view subkey) override;
    contracts::Status remove_key_if_empty(std::wstring_view subkey) override;
    static std::wstring key(std::wstring_view subkey, std::wstring_view name) { return std::wstring(subkey) + L"|" + std::wstring(name); }
};

}  // namespace ef::core::adapters::windows
