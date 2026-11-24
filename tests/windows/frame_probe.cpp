// MEAS-01 Windows testi: PresentData ETW kare oturumu.
//   Yukseltilmemis: start AccessDenied donmeli (yukseltme denenmez) ve oturum birakilmamali.
//   Yukseltilmis (veya Performance Log Users): ~2 sn topla, present sayisini yaz, durdur ve
//   oturumun artik var olmadigini ControlTrace QUERY ile dogrula (logman query -ets esdegeri).
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <thread>

#include "measurement/frame/frame_session.h"
#include "platform/windows/elevation.h"

using namespace ef;
using measurement::FrameSessionError;
using measurement::SessionPresence;

namespace {

const char* presence_name(SessionPresence p) {
    switch (p) {
        case SessionPresence::Absent: return "absent";
        case SessionPresence::Present: return "present";
        default: return "unknown";
    }
}

}  // namespace

int wmain() {
    const bool elevated = platform::is_process_elevated();
    std::printf("frame_session: elevated=%d session=%ls\n", elevated ? 1 : 0, measurement::kFrameSessionName);

    // Onek denetimi: bize ait olmayan bir ad reddedilmeli (baska aracin oturumuna dokunulmaz).
    {
        measurement::FrameSession foreign;
        const auto r = foreign.start_realtime(L"PresentMon");
        if (r.ok() || foreign.running()) {
            std::printf("frame_session: FAIL foreign session name accepted\n");
            return 1;
        }
    }

    measurement::FrameSession session;
    const auto start = session.start_realtime();
    if (!start.ok()) {
        if (start.error == FrameSessionError::AccessDenied && !elevated) {
            if (start.win32 != ERROR_ACCESS_DENIED) {
                std::printf("frame_session: FAIL access denied mapped from win32=%lu\n", start.win32);
                return 1;
            }
            unsigned long q = 0;
            const auto p = measurement::query_session(measurement::kFrameSessionName, &q);
            if (p == SessionPresence::Present) {
                std::printf("frame_session: FAIL session exists after denied start\n");
                return 1;
            }
            std::printf("frame_session: access_denied (expected, not elevated)\n");
            return 0;
        }
        std::printf("frame_session: FAIL start error=%d win32=%lu detail=%s\n", static_cast<int>(start.error),
                    start.win32, start.detail.c_str());
        return 1;
    }

    std::printf("frame_session: started (elevated=%d)\n", elevated ? 1 : 0);
    unsigned long q = 0;
    const auto during = measurement::query_session(measurement::kFrameSessionName, &q);
    std::printf("frame_session: query while running=%s (win32=%lu)\n", presence_name(during), q);

    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2)) {
        session.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    session.stop();
    const auto st = session.stats();
    std::printf("frame_session: present_events=%llu events_lost=%u buffers_lost=%u overflowed=%u\n",
                static_cast<unsigned long long>(st.present_events), st.events_lost, st.buffers_lost,
                st.overflowed_presents);

    const auto after = measurement::query_session(measurement::kFrameSessionName, &q);
    std::printf("frame_session: query after stop=%s (win32=%lu)\n", presence_name(after), q);
    if (during != SessionPresence::Present) {
        std::printf("frame_session: FAIL session not visible while running\n");
        return 1;
    }
    if (after != SessionPresence::Absent) {
        std::printf("frame_session: FAIL session still exists after stop (orphan)\n");
        return 1;
    }
    if (session.running()) {
        std::printf("frame_session: FAIL still running after stop\n");
        return 1;
    }
    std::printf("frame_session: ok\n");
    return 0;
}
