#pragma once
// SEC-01 platform: yukseltme durumu, kendi EXE yolu, guvenli nonce, UAC ile rol baslatma.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "core/roles.h"

namespace ef::platform {

bool is_process_elevated();
// K27: bu surecin belirtecinde BUILTIN\Administrators (S-1-5-32-544) var mi (herhangi bir oznitelikle; UAC ile suzulmus
// belirtecte deny-only). Yardimci hizmet yalniz bu hesaplara hizmet verir; okunamazsa false (UAC broker yolu).
bool self_admin_member();
std::filesystem::path self_exe_path();
std::string random_nonce();  // BCryptGenRandom, 32 hex

// Denetim 3.3 (K27 acik notu): UAC sinirinda setup dosyasi degistirilemesin. Calisan EXE dosyasi yalniz okuma
// paylasimiyla acik tutulur: tutuldugu surece ayni kullanicinin kodu dosyayi yazamaz, silemez ve YENIDEN ADLANDIRAMAZ
// (calisan goruntu uzerine yazilamaz ama yeniden adlandirilabilirdi; yerine baska dosya konurdu). UAC istemi ve
// yukseltilmis isci bu yoldaki ayni dosyayi gorur. Kilit istek bitene kadar (isci cikana dek) tutulur.
class SelfImageLock {
public:
    SelfImageLock() = default;
    ~SelfImageLock();
    SelfImageLock(SelfImageLock&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
    SelfImageLock& operator=(SelfImageLock&&) = delete;
    SelfImageLock(const SelfImageLock&) = delete;
    SelfImageLock& operator=(const SelfImageLock&) = delete;
    static SelfImageLock acquire(const std::filesystem::path& exe);  // bos yol = self_exe_path()
    bool ok() const noexcept { return h_ != nullptr; }
    unsigned long error() const noexcept { return err_; }

private:
    void* h_ = nullptr;
    unsigned long err_ = 0;
};

// Baslangicta dogrulanan ust surec kimligi: pipe kabulunde PID yeniden kullanimini dislamak icin saklanir.
struct ParentIdentity {
    std::uint32_t pid = 0;
    std::uint64_t create_time = 0;  // FILETIME (100 ns)
    std::uint32_t session = 0;
    std::wstring image;
};

// Ust surecin yasadigini, AYNI EXE oldugunu (tam yol) ve kimligini (PID + olusturma zamani + oturum) dogrular.
// Basarisizlikta nullopt; yarim kimlikle devam edilmez.
std::optional<ParentIdentity> verify_parent_same_exe(std::uint32_t parent_pid);

// Geriye uyumlu ikili kontrol (yalniz yasam + ayni EXE); kanal kuran roller verify_parent_same_exe kullanir.
bool parent_is_same_exe(std::uint32_t parent_pid);

// Ust surec bitene kadar bekler (yetim broker kalmaz). timeout_ms: 0 = sonsuz.
bool wait_parent_exit(std::uint32_t parent_pid, std::uint32_t timeout_ms);

class ShellElevationLauncher final : public core::ElevationLauncher {
public:
    core::ElevationOutcome launch_elevated(const core::LaunchArgs& args, std::string& detail) override;
    // Baslatilan surecin tanitici (SYNCHRONIZE/QUERY); yoksa nullptr. Sahiplik cagirana gecer.
    void* take_process_handle() noexcept {
        void* h = process_;
        process_ = nullptr;
        return h;
    }
    ~ShellElevationLauncher() override;

private:
    void* process_ = nullptr;
};

}  // namespace ef::platform
