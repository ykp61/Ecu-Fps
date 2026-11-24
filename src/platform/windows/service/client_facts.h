#pragma once
// ADR-033 / spec 5: yardimci hizmetin bagli pipe istemcisi ve kendisi hakkindaki gercekleri toplar (salt okuma).
// Karar saf core/service/client_check.h icindedir. Bu oturumda calistirilmaz (LocalSystem hizmet + gercek pipe ister).

#include <filesystem>
#include <memory>

#include "core/service/client_check.h"
#include "core/setup/layout.h"
#include "platform/windows/pipe.h"

namespace ef::platform::service {

// Istemci sureci tanitici (PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE). Baglanti boyunca ACIK tutulur:
// dogrulanan PID baska bir surece yeniden verilemez; istemci kapanirsa oturum ve uzun isler bunu gorur.
class ClientProcess {
public:
    explicit ClientProcess(void* h) : h_(h) {}
    ~ClientProcess();
    ClientProcess(const ClientProcess&) = delete;
    ClientProcess& operator=(const ClientProcess&) = delete;
    void* handle() const noexcept { return h_; }
    bool exited() const;

private:
    void* h_;
};

struct GatheredClient {
    core::service::ClientFacts facts;
    std::unique_ptr<ClientProcess> process;  // null: acilamadi (facts.image_ok false)
};

// GetNamedPipeClientProcessId -> OpenProcess -> QueryFullProcessImageNameW -> goruntu dosyasi kimligi ->
// ImpersonateNamedPipeClient / OpenThreadToken / RevertToSelf (RAII, her yolda geri alinir) -> oturum, INTERACTIVE, SID.
GatheredClient gather_client(const PipeServer& pipe);

// Kurulum yolundaki dosyanin (hizmetin kendi goruntusu DEGIL) kimligi + kurulum klasoru ACL karari. Her baglantida
// yeniden okunur: guncellemeyle degisen EXE de gecer, sonradan bozulan ACL hemen reddedilir.
core::service::ServiceSelf gather_self(const core::setup::InstallLayout& layout);

}  // namespace ef::platform::service
