#pragma once
// WIN-08 ek: yonetici gerektiren temizlik icin yukseltilmis yardimci (ayni EXE, Role::CleanupWorker).
//  Yardimci: yalniz "--role=cleanup --parent=<pid> --nonce=<32hex>"; yukseltilmis olmali, ust surec ayni EXE,
//            SEC-04 trust politikasi broker ile ayni. Tek istemcili pipe (ACL: oturum kullanicisi), Hello nonce+PID.
//            Once sabit yonetici kategori listesini tarar, plani surec belleğinde tutar; calistirma AYNI planla.
//  Istemci : UAC yalniz kullanici yonetici kategoriyi sectiginde istenir (ShellElevationLauncher).
//            Iptal: istemci Cancel cercevesi yazar; yardimci her dosya isleminden once pipe'i yoklar.
//  Serbest yol/komut yok; istemciye dosya yolu tasinmaz. Rapor/onizleme ASCII JSON.

#include <memory>
#include <string>
#include <vector>

#include "core/cleanup/admin_transfer.h"
#include "core/cleanup/cleanup.h"
#include "core/roles.h"
#include "platform/windows/elevation.h"

namespace ef::platform {

enum class CleanupAdminStatus { Ok, UacDenied, Error };

class CleanupAdminClient;

struct CleanupAdminLaunch {
    CleanupAdminStatus status = CleanupAdminStatus::Error;
    std::unique_ptr<CleanupAdminClient> client;
    std::vector<core::cleanup::CleanupPreview> previews;
    std::string detail;
};

// UAC ile yardimciyi baslatir, sabit yonetici kategorilerini tarar. UAC reddi ayri durum; hic istek yok.
CleanupAdminLaunch launch_cleanup_admin(const std::wstring& volume);

// --role=cleanup: main.cpp'de yukseltme/ust surec/imza kontrolleri yapildiktan sonra cagrilir.
int run_cleanup_worker(const core::LaunchArgs& args, const ParentIdentity& parent);

class CleanupAdminClient {
public:
    ~CleanupAdminClient();
    CleanupAdminClient(const CleanupAdminClient&) = delete;
    CleanupAdminClient& operator=(const CleanupAdminClient&) = delete;

    // Yardimcinin tuttugu plan uzerinden yalniz taranmis yonetici kategorileri. Ilerleme ve iptal tasinir.
    contracts::Result<core::cleanup::CleanupReport> run(const std::vector<core::cleanup::CleanupCategory>& categories,
                                                        const core::cleanup::RunOptions& opt);
    void close();
    bool closed() const noexcept;

private:
    friend CleanupAdminLaunch launch_cleanup_admin(const std::wstring& volume);
    CleanupAdminClient() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ef::platform
