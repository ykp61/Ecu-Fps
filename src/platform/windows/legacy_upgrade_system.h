#pragma once
// T3b (ADR-043 ek): eski adli kurulumdan yukseltmenin Win32 tarafi. Saf durum makinesi core/legacy/legacy_upgrade.h,
// giris karari core/legacy/legacy_entry.h. Bu dosyadaki her yazici cagri once upgrade_*_allowed izin listesinden gecer
// ve mevcut kurulum yardimcilarini (install/win_setup_ops.h, install/secure_dir.h, journal_fs.h) kullanir: hizmet
// guvenlik/hata eylemleri, ARP degerleri, kisayol geri okumasi ve korumali DACL'ler yeni kurulumla aynidir.
// GERCEK CIHAZDA DENENMEDI: birim testleri yalniz saf mantigi ve izin listesini dogrular; bu sinif SCM, HKLM,
// Program Files ve ProgramData'ya yazar ve yalniz SYSTEM hizmet baglaminda calisir.

#include <functional>
#include <string>

#include "core/legacy/legacy_upgrade.h"
#include "core/setup/layout.h"

namespace ef::platform {

class WinUpgradeSystem final : public core::legacy::UpgradeSystem {
public:
    WinUpgradeSystem(core::setup::InstallLayout legacy, core::setup::InstallLayout current, core::legacy::UpgradePaths paths,
                     core::legacy::UpgradeNames names, std::wstring version);

    core::legacy::FsStatus apply_machine_acl(const std::string& dir) override;
    core::legacy::FsStatus verify_machine_acl(const std::string& dir) override;
    core::legacy::SvcState service_state(std::wstring_view name) override;
    std::optional<std::string> service_exe(std::wstring_view name) override;
    core::legacy::FsStatus create_service(std::wstring_view name, const std::string& exe) override;
    core::legacy::FsStatus start_service(std::wstring_view name) override;
    core::legacy::FsStatus stop_service(std::wstring_view name) override;
    core::legacy::FsStatus delete_service(std::wstring_view name) override;
    bool service_healthy(std::wstring_view name, std::int64_t timeout_ms) override;
    std::optional<std::string> arp_install_location(std::wstring_view key) override;
    core::legacy::FsStatus write_arp(std::wstring_view key) override;
    core::legacy::FsStatus delete_arp(std::wstring_view key) override;
    bool shortcut_present(const std::string& lnk) override;
    std::optional<std::string> shortcut_target(const std::string& lnk) override;
    core::legacy::FsStatus write_shortcut(const std::string& lnk, const std::string& target) override;
    core::legacy::FsStatus delete_shortcut(const std::string& lnk) override;
    core::legacy::FsStatus apply_install_acl(const std::string& dir) override;
    core::legacy::FsStatus schedule_delete_on_reboot(const std::string& path) override;
    core::legacy::FsStatus restore_legacy_exe() override;

private:
    core::setup::InstallLayout legacy_, cur_;
    core::legacy::UpgradePaths p_;
    core::legacy::UpgradeNames n_;
    std::wstring version_;
};

// Hizmet kancasi (a): ham goruntu yolu eski kurulu EXE ise "--service" bu girisle calisir (eski hizmet adiyla dagitici;
// hizmet ana fonksiyonunda decide_legacy_service_entry + run_legacy_upgrade(LegacyHost); cikis 0/1/5/6). Normal yeni
// hizmet girisi bu fonksiyonu kullanmaz.
bool legacy_service_dispatch_candidate();
int run_legacy_service_host();

// Hizmet kancasi (c): yeni hizmet acilisinda eski artefakt temizligi (NewService). Kayit yoksa hemen doner; kayit klasoru
// ACL'i (SY+BA, kullanici yazamaz) dogrulanmadan hicbir sey yapilmaz; yalniz kalici "healthy" kaydinda temizlik.
// stop_event (HANDLE) isaretlenince yeniden denemeler biter.
void run_legacy_cleanup(void* stop_event, const std::function<void(const std::string&)>& log);

}  // namespace ef::platform
