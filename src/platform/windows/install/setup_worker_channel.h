#pragma once
// ADR-033 / spec 3-4: kurulum penceresinin (yukseltilmemis) yukseltilmis kurulum iscisini cagirmasi. Uygulama ici
// Evet/Hayir onayindan SONRA cagrilir: tek UAC (ShellElevationLauncher, --role=setup --parent --nonce), sunucu PID'i
// dogrulanmis pipe, Hello, tek istek, isciden gelen ilerleme, son yanit, Bye.
//  - UAC reddi: elevation=Denied, reply yok (hicbir sey yapilmadi).
//  - Istek gonderildikten sonra yanit yok: reply=nullopt (sonuc BILINMIYOR; basari asla varsayilmaz).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "core/roles.h"
#include "core/setup/setup_protocol.h"

namespace ef::platform::install {

struct SetupRun {
    core::ElevationOutcome elevation = core::ElevationOutcome::Failed;
    std::optional<core::setup::SetupReply> reply;
    std::string detail;
    // Istek cercevesi yazilmaya calisildi mi (false: isci istegi hic almadi -> pencere NotStarted gosterir).
    bool request_sent = false;
    // Isci istekten once ciktiysa cikis kodu (or. kExitUntrusted: imza denetimi reddetti).
    std::optional<std::uint32_t> worker_exit;
};

SetupRun run_setup_worker_request(core::setup::SetupKind kind,
                                  const std::function<void(const core::setup::SetupProgress&)>& on_progress);

}  // namespace ef::platform::install
