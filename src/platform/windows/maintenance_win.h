#pragma once
// EF-X02: DISM API (System32 dismapi.dll; SDK basligi olmadigindan imzalar resmi DISM API belgesinden elle).
// Yalniz yukseltilmis broker cagirir. Servis islemi zorla sonlandirilmaz.
#include "core/maintenance/image_health.h"
#include "core/maintenance/standby_purge.h"

namespace ef::platform {

class WinImageServicing final : public core::maintenance::ImageServicingApi {
public:
    ~WinImageServicing() override;
    std::optional<bool> pending_reboot() override;
    contracts::Result<core::maintenance::ImageHealth> check(bool scan) override;
    std::uint32_t restore_local_only() override;
    bool create_restore_point() override;
    explicit WinImageServicing(core::recovery::JournalFs* rp_jobs = nullptr) : rp_jobs_(rp_jobs) {}

private:
    long open();  // HRESULT; 0 = acik
    core::recovery::JournalFs* rp_jobs_ = nullptr;
    void* mod_ = nullptr;
    unsigned session_ = 0;
    bool initialized_ = false, session_open_ = false;
};

// T15: bekleme (standby) listesi toplami (Normal + Reserve + Core oncelik; PDH, yonetici gerektirmez). Okunamazsa hata.
contracts::Result<std::uint64_t> read_standby_bytes();

// T15: yalniz yukseltilmis broker. SeProfileSingleProcessPrivilege + NtSetSystemInformation(MemoryPurgeStandbyList).
// Gercek bosaltma bu surumde bir bilgisayarda DOGRULANMADI (yalniz birim testinde sahte API).
class WinMemoryList final : public core::maintenance::MemoryListApi {
public:
    contracts::Result<std::uint64_t> standby_bytes() override { return read_standby_bytes(); }
    contracts::Status purge_standby() override;
};

}  // namespace ef::platform
