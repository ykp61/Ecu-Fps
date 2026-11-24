// REL-01: pipe istemci kimligi ust surecle birebir baglanmali. Yalniz PID karsilastirmasi PID yeniden
// kullanimiyla atlatilabilir; olusturma zamani + oturum + EXE yolu da eslesmeli.
#include <windows.h>

#include <string>

#include "ef_test.h"
#include "platform/windows/elevation.h"
#include "platform/windows/inventory/os_user.h"
#include "platform/windows/pipe.h"

namespace {

ef::platform::PeerIdentity peer() {
    ef::platform::PeerIdentity p;
    p.pid = 4242;
    p.session = 1;
    p.create_time = 133000000000000000ULL;
    p.image = L"C:\\Apps\\EcuFps.exe";
    return p;
}

}  // namespace

EF_TEST(pipe_peer_matches_requires_full_identity) {
    using ef::platform::peer_matches;
    const auto p = peer();
    EF_CHECK(peer_matches(p, 4242, p.create_time, 1, L"C:\\Apps\\EcuFps.exe"));
    EF_CHECK(peer_matches(p, 4242, p.create_time, 1, L"c:\\apps\\ECUFPS.EXE"));  // Windows yolu harf duyarsiz

    EF_CHECK(!peer_matches(p, 4243, p.create_time, 1, p.image));  // baska PID
    // Ayni PID, farkli olusturma zamani: onceki surec olmus, PID yeniden kullanilmis.
    EF_CHECK(!peer_matches(p, 4242, p.create_time + 1, 1, p.image));
    EF_CHECK(!peer_matches(p, 4242, p.create_time, 2, p.image));  // baska oturum
    EF_CHECK(!peer_matches(p, 4242, p.create_time, 1, L"C:\\Apps\\Other.exe"));  // baska EXE
    EF_CHECK(!peer_matches(p, 4242, p.create_time, 1, L"C:\\Apps\\EcuFps.exe.bak"));
    EF_CHECK(!peer_matches(p, 4242, p.create_time, 1, L""));

    auto no_time = p;
    no_time.create_time = 0;  // eksik kimlik eslesmez (yarim dogrulama yok)
    EF_CHECK(!peer_matches(no_time, 4242, 0, 1, p.image));
    auto no_image = p;
    no_image.image.clear();
    EF_CHECK(!peer_matches(no_image, 4242, p.create_time, 1, p.image));
    EF_CHECK(!peer_matches(p, 0, p.create_time, 1, p.image));
}

EF_TEST(pipe_client_identity_reads_process_creation_time) {
    const auto user = ef::platform::inventory::read_user();
    const std::string sid = user.target_user_sid.value.value_or(user.session_user_sid.value.value_or(""));
    EF_CHECK(!sid.empty());
    if (sid.empty()) return;
    const std::string nonce = ef::platform::random_nonce();
    EF_CHECK(!nonce.empty());
    auto srv = ef::platform::PipeServer::create(ef::platform::pipe_name_for(nonce), sid);
    EF_CHECK(srv.ok());
    if (!srv.ok()) return;
    auto cli = ef::platform::PipeClient::connect(ef::platform::pipe_name_for(nonce),
                                                  static_cast<std::uint32_t>(GetCurrentProcessId()), 2000);
    EF_CHECK(cli.ok());
    if (!cli.ok()) return;
    EF_CHECK(srv.value()->accept(2000).ok());
    auto id = srv.value()->client_identity();
    EF_CHECK(id.ok());
    if (!id.ok()) return;
    EF_CHECK(id.value().pid == static_cast<std::uint32_t>(GetCurrentProcessId()));
    EF_CHECK(id.value().create_time != 0);
    DWORD session = 0;
    EF_CHECK(ProcessIdToSessionId(GetCurrentProcessId(), &session) != 0);
    EF_CHECK(id.value().session == static_cast<std::uint32_t>(session));
    EF_CHECK(!id.value().image.empty());
    EF_CHECK(ef::platform::peer_matches(id.value(), id.value().pid, id.value().create_time, id.value().session,
                                          ef::platform::self_exe_path().wstring()));
}
