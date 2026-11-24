#pragma once
// REC-01 platform: atomik journal dosya sistemi, kullanici/makine journal dizinleri, makine kimligi.

#include <filesystem>
#include <optional>
#include <string>

#include "contracts/result.h"
#include "core/recovery/journal.h"

namespace ef::platform {

class WinJournalFs final : public core::recovery::JournalFs {
public:
    explicit WinJournalFs(std::filesystem::path dir) : dir_(std::move(dir)) {}
    contracts::Status write_atomic(const std::string& name, const std::string& bytes) override;
    contracts::Result<std::string> read(const std::string& name) override;
    contracts::Result<std::vector<std::string>> list() override;
    contracts::Status remove(const std::string& name) override;
    // CORE-05: cokme sonrasi kalan commit edilmemis *.json.tmp dosyalari (salt okuma).
    std::vector<std::string> temp_files() override;
    const std::filesystem::path& dir() const noexcept { return dir_; }

private:
    std::filesystem::path dir_;
};

// %LOCALAPPDATA%\EcuFps\journal (kullanici kapsamli islemler; yukseltme gerekmez)
contracts::Result<std::filesystem::path> user_journal_dir();

// CORE-01: var olan bir makine journal dizinini guvenlik acisindan dogrular (salt okuma; yazmaz).
// Sahiplik SYSTEM veya Administrators olmali; DACL'de bu iki SID disinda yazma izni veren allow ACE
// bulunmamali (NULL DACL ve reparse point reddedilir). Uygunsuzsa Denied + tanilama ayrintisi doner.
contracts::Status verify_machine_journal_dir(const std::filesystem::path& dir);

namespace detail {
// Uyumluluk #21: AV/EDR, yedek veya dizin ajani dosyayi kisa sure acik tutabilir. Paylasim, erisim ve kilit hatalari
// gecici sayilir; atomik yazim (gecici dosya olusturma ve MoveFileEx) birkac kez artan bekleme ile tekrar denenir.
bool transient_fs_error(unsigned long win32) noexcept;
inline constexpr int kFsRetryAttempts = 5;
// Deneme `attempt` (0'dan) basarisiz olduktan sonraki bekleme: 15, 30, 60, 120 ms (toplam en cok 225 ms).
unsigned long fs_retry_delay_ms(int attempt) noexcept;
// op(): 0 = basarili, aksi Win32 hata kodu. Gecici hatada sleep(ms) ile en cok kFsRetryAttempts deneme. Donus son
// kod (0 = basarili); *attempts = yapilan deneme sayisi.
template <class Op, class Transient, class Sleep>
unsigned long retry_transient_if(Op&& op, Transient&& transient, Sleep&& sleep, int* attempts = nullptr) {
    unsigned long e = 0;
    int n = 0;
    for (; n < kFsRetryAttempts; ++n) {
        e = op();
        if (e == 0 || !transient(e)) {
            ++n;
            break;
        }
        if (n + 1 < kFsRetryAttempts) sleep(fs_retry_delay_ms(n));
    }
    if (attempts) *attempts = n;
    return e;
}
template <class Op, class Sleep>
unsigned long retry_transient(Op&& op, Sleep&& sleep, int* attempts = nullptr) {
    return retry_transient_if(op, [](unsigned long e) { return transient_fs_error(e); }, sleep, attempts);
}
// Okuma icin: yalniz paylasim/kilit ihlali (erisim reddi okumada kalici sayilir; tarama dongulerini yavaslatmaz).
bool transient_read_error(unsigned long win32) noexcept;
// Test edilebilir cekirdek. allowed_owner_sids: kabul edilen sahiplik SID metinleri (or. "S-1-5-18").
// Yazma izinleri her zaman yalniz SYSTEM/Administrators SID'lerinde olabilir.
contracts::Status check_journal_dir_security(const std::filesystem::path& dir, const std::vector<std::string>& allowed_owner_sids);
}  // namespace detail

// %ProgramData%\EcuFps\journal: SYSTEM+Yoneticiler tam, Kullanicilar okuma. Olusturma yonetici ister;
// yukseltmesiz cagrida Denied (acik engel). Farkli yonetici hesabi da ayni dizini kullanir.
// CORE-01: var olan kok/dizin sahiplik+ACL dogrulamasindan gecmezse guvenli reddedilir (tahmin edilmez).
contracts::Result<std::filesystem::path> machine_journal_dir(bool create);

// MachineGuid ozeti (yerel; disa aktarilmaz). Journal baska makinede uygulanmasin diye.
std::string machine_id();

}  // namespace ef::platform
