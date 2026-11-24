#pragma once
// ADR-033 / spec 2 ve 4: SetupOps'un gercek Windows uygulamasi. Yalniz yukseltilmis SetupWorker kullanir; her yazma
// geri okunur. Kaynak EXE iscinin kendi goruntusudur: kurucu disinda verilen, yazma paylasimsiz (deny-write) acilmis
// tanitici; hedef klasore yonetici-yalniz ACL kopyadan ONCE uygulanir. Hicbir yol/deger istemciden gelmez.
// Birim testleri sahte kullanir; bu sinif bu oturumda calistirilmaz (UAC, SCM, HKLM, Program Files gerekir).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "core/setup/install_tx.h"
#include "core/setup/layout.h"

namespace ef::platform::install {

class WinSetupOps final : public core::setup::SetupOps {
public:
    // source_image: CreateFileW(GENERIC_READ, FILE_SHARE_READ) ile acilmis kendi goruntusu (HANDLE; sahiplik cagiranda).
    WinSetupOps(core::setup::InstallLayout layout, std::filesystem::path self_image, void* source_image, std::wstring version)
        : l_(std::move(layout)), self_(std::move(self_image)), source_(source_image), version_(std::move(version)) {}

    contracts::Result<core::setup::PriorState> probe() override;
    contracts::Status create_install_dir() override;
    contracts::Status remove_install_dir_if_empty() override;
    contracts::Status stage_and_swap_exe() override;
    contracts::Status restore_previous_exe(const core::setup::PriorState& p) override;
    contracts::Status commit_exe() override;
    contracts::Status write_shortcut() override;
    contracts::Status restore_shortcut(const core::setup::PriorState& p) override;
    contracts::Status remove_shortcut() override;
    contracts::Status write_desktop_shortcut() override;
    contracts::Status restore_desktop_shortcut(const core::setup::PriorState& p) override;
    contracts::Status remove_desktop_shortcut() override;
    contracts::Status write_service() override;
    contracts::Status restore_service(const core::setup::PriorState& p) override;
    contracts::Status delete_service() override;
    contracts::Status start_service() override;
    contracts::Status stop_service() override;
    contracts::Status create_updates_dir() override;
    contracts::Status remove_updates_dir() override;
    contracts::Status write_arp(const core::setup::ArpValues& v) override;
    contracts::Status restore_arp(const core::setup::PriorState& p) override;
    contracts::Status remove_arp() override;
    contracts::Result<bool> machine_journals_clean() override;
    contracts::Status remove_machine_data() override;
    contracts::Result<bool> remove_program_files() override;

    const std::wstring& version() const noexcept { return version_; }

private:
    contracts::Status apply_service_config(const core::setup::ServiceConfig& c, bool create_if_missing);

    core::setup::InstallLayout l_;
    std::filesystem::path self_;
    void* source_;
    std::wstring version_;
};

// Kendi goruntusunu yazma paylasimsiz acar (INVALID_HANDLE_VALUE -> nullptr). Islem boyunca acik tutulur.
void* open_own_image_deny_write(const std::filesystem::path& self_image);

// T3b (ADR-043 ek): ayni SCM / ARP / kisayol yardimcilari adla (eski adli kurulumdan yukseltme ve temizligi). WinSetupOps
// bu fonksiyonlari yeni adlarla cagirir; davranis (geri okuma, hata eylemleri, SID turu, gecikmeli otomatik) aynidir.
struct NamedServiceState {
    bool exists = false;
    std::uint32_t state = 0;  // SERVICE_STOPPED / SERVICE_RUNNING / ...
    std::uint32_t pid = 0;
    std::wstring image_path;
};
contracts::Result<NamedServiceState> query_named_service(std::wstring_view service_name);
contracts::Status ensure_named_service(std::wstring_view service_name, std::wstring_view display_name, const core::setup::ServiceConfig& c,
                                       bool create_if_missing);
contracts::Status start_named_service(std::wstring_view service_name);  // baslat + RUNNING bekle + 1.5 sn sonra hala RUNNING
contracts::Status stop_named_service(std::wstring_view service_name);   // durdur + 30 sn bekle; yoksa ok
contracts::Status delete_named_service(std::wstring_view service_name); // durdur + sil + geri okuma; yoksa ok
contracts::Result<std::optional<core::setup::ArpValues>> read_arp_key(std::wstring_view arp_key);
contracts::Status write_arp_key(std::wstring_view arp_key, const core::setup::ArpValues& v);  // geri okunur
contracts::Status remove_arp_key(std::wstring_view arp_key);
contracts::Result<std::wstring> read_shortcut_target(const std::filesystem::path& lnk);
contracts::Status write_shortcut_link(const std::filesystem::path& lnk, const std::filesystem::path& target);  // geri okunur

// Guncelleme sonrasi (hizmet acilisi, yukseltilmis): bizim ARP kaydinin yalniz DisplayVersion'u calisan surume cekilir; geri okunur.
// Degisiklik yoksa ok + *changed=false. Onceki deger *prior'a yazilir (cagiran kaydeder).
contracts::Status refresh_arp_display_version(const core::setup::InstallLayout& layout, const std::wstring& running_version, bool* changed,
                                              std::wstring* prior);

}  // namespace ef::platform::install
