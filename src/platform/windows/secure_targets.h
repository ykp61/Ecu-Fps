#pragma once
// SEC-03 platform: makine islem kilidi, oyuncunun kendi hive'i (HKU\SID), dogrulanmis dosya tanitici.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "contracts/result.h"

namespace ef::platform {

// Adlandirilmis mutex kilidi. Broker "Global\\EcuFps.Operation" kullanir (kullanicilar arasi).
// abandoned: onceki sahip kilidi birakmadan oldu -> journal kurtarmasi gerekir.
class OperationLock {
public:
    static contracts::Result<std::unique_ptr<OperationLock>> acquire(const std::wstring& name, std::uint32_t timeout_ms);
    ~OperationLock();
    bool previous_owner_abandoned() const noexcept { return abandoned_; }

private:
    OperationLock(void* h, bool abandoned) : h_(h), abandoned_(abandoned) {}
    void* h_;
    bool abandoned_;
};
inline constexpr wchar_t kMachineLockName[] = L"Global\\EcuFps.Operation";

// Oyuncunun hive'i: HKEY_USERS\<SID>\<subkey>. HKEY_CURRENT_USER KULLANILMAZ (yukselten hesabin
// profili olabilir). Donen deger HKEY (void*); RegCloseKey ile kapatilir.
contracts::Result<void*> open_user_key(const std::string& user_sid, const std::wstring& subkey, bool write);

// Kok altinda dosyayi tek seferde acar ve tanitici uzerinden dogrular:
// '..'/mutlak yol yok, yeniden ayristirma noktasi (symlink/junction) yok, tek baglanti (hardlink yok),
// son yol kokun icinde. Islem bu tanitici ile yapilir (TOCTOU yok).
struct VerifiedFile {
    void* handle = nullptr;  // HANDLE; CloseHandle
    std::wstring final_path;
};
contracts::Result<VerifiedFile> open_verified_file(const std::filesystem::path& root, const std::wstring& relative, bool write);
void close_verified(VerifiedFile& f);

}  // namespace ef::platform
