#pragma once
// ADR-033 / spec 5: UI tarafinda yonetici kanali secimi (saf). Hizmet yolu yalniz her kosul dogrulandiginda; aksi halde
// bugunku UAC broker yolu. Geri donus YALNIZ hic Request gonderilmeden once: istek gonderildikten sonra yanit yoksa
// sonuc StepResult::unknown kalir, UAC ile yeniden denenmez (cift uygulama yok).
// Uygulama ici Evet/Hayir bu secimden ONCE, bugunku yerinde kalir.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/broker/admin.h"
#include "core/roles.h"

namespace ef::core::service {

struct ServiceProbe {
    bool ui_installed = false;  // UI kurulum yerinden calisiyor
    bool admin_member = false;  // K27: UI hesabi BUILTIN\Administrators uyesi (standart hesap -> UAC broker)
    bool exists = false;        // SCM'de hizmet var
    bool running = false;       // SERVICE_RUNNING
    bool config_ok = false;     // ImagePath == "\"<layout.exe>\" --service"
    std::uint32_t scm_pid = 0, pipe_server_pid = 0;
    bool connected = false;  // pipe istemcisi baglandi (sunucu PID'i == SCM PID'i)
    bool hello_ok = false;   // HelloAck alindi
};

enum class Path { Service, UacBroker };

Path choose_path(const ServiceProbe& p);

// Hizmetin calistirabildigi istekler. VendorInstall HARIC: paket kullanicinin %LOCALAPPDATA% klasorundedir; LocalSystem
// hizmeti o klasoru goremez ve kullanicinin sectigi bir EXE'yi SYSTEM olarak calistirmak istenmez. Bu istek her zaman
// bugunku UAC broker'ina gider (yukseltilmis surec kullanicinin profilini gorur).
bool service_handles(ipc::RequestAction action);

// Denetim 2026-11-14 / sahip karari 3.1: hizmet, UAC ile suzulmus (orta duzey) yonetici belirtecine de hizmet verir.
// Ayni kullanicinin baska bir sureci kurulu UI'yi ele gecirirse hizmet uzerinden UAC'siz SYSTEM islemi yaptirabilir.
// Bu yuzden etkisi en genis islemler her zaman UAC broker'ina gider (Windows tam yukseltilmis onay sorar):
//  - DriverInstall (Windows Update surucu kurulumu) ve VendorInstall (zaten UAC)
//  - DNS sunucusu DEGISTIRME (Fix / Apply). Journal'daki onceki degere geri donus (UndoFix, Restore, Recover,
//    KeepCurrent) hizmette kalir: hedef deger saldirgan secemez, yalniz kayitli anlik goruntudur.
// Gunluk geri alinabilir ac/kapa ayarlari (guc, HAGS, DRS, ...) hizmette UAC'siz kalir.
bool service_handles_op(ipc::RequestAction action, contracts::OperationId op);
// ops listesinin tamami hizmette calisabilir mi (biri bile UAC isterse tum toplu is UAC broker'ina gider).
bool service_handles_ops(ipc::RequestAction action, const std::vector<contracts::SettingOperation>& ops);

// Hizmet yolunda UAC yok: run_admin_batch'in istedigi "baslatma" zaten calisan hizmettir. Hicbir surec baslatmaz.
class NoUacLauncher final : public ElevationLauncher {
public:
    ElevationOutcome launch_elevated(const LaunchArgs& args, std::string& detail) override;
    int calls = 0;  // run_admin_batch cagirdi mi (yalniz sayac; baslatma yok)
};

// Hizmet oturumunun Hello/HelloAck'i kanal secimi sirasinda yapildi. run_admin_batch'in kendi (nonce) Hello'su hizmete
// GONDERILMEZ; yerelde HelloAck ile karsilanir. Sonraki cercevelerin seq'i hizmet oturumunun devamina kaydirilir.
class HandshakenTransport final : public broker::FrameTransport {
public:
    // used_seq: hizmete zaten gonderilmis son seq (Hello = 1).
    HandshakenTransport(std::unique_ptr<broker::FrameTransport> inner, std::uint64_t used_seq)
        : inner_(std::move(inner)), used_seq_(used_seq) {}
    contracts::Status send(const ipc::Frame& f) override;
    contracts::Result<ipc::Frame> receive() override;

private:
    std::unique_ptr<broker::FrameTransport> inner_;
    std::uint64_t used_seq_;
    bool hello_seen_ = false;
    bool ack_pending_ = false;
};

}  // namespace ef::core::service
