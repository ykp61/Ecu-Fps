// REL-01 (tests/security karsiligi): guvenlik sinirindaki ayristiricilara deterministik bozuk/rastgele girdi.
// Beklenti: cokme yok, sinirlar asilmaz, bozuk girdi reddedilir veya guvenle atlanir.
#include <cmath>
#include <cstdio>
#include <iterator>
#include <optional>
#include <cstdint>
#include <string>
#include <vector>

#include "core/games/cs2.h"
#include "core/insights/history.h"
#include "core/ipc/protocol.h"
#include "core/service/channel_choice.h"
#include "core/service/service_session.h"
#include "core/serialization/json.h"
#include "core/settings/preferences.h"
#include "ef_test.h"
#include "measurement/frame/capture_result.h"

namespace {
struct Rng {
    std::uint64_t s;
    std::uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
};
std::string mutate(const std::string& base, Rng& r, int edits) {
    std::string s = base;
    for (int i = 0; i < edits && !s.empty(); ++i) {
        const auto pos = static_cast<std::size_t>(r.next() % s.size());
        switch (r.next() % 4) {
            case 0: s[pos] = static_cast<char>(r.next() & 0xFF); break;
            case 1: s.erase(pos, 1); break;
            case 2: s.insert(pos, 1, static_cast<char>(r.next() & 0xFF)); break;
            case 3: s.insert(pos, s.substr(0, std::min<std::size_t>(16, s.size()))); break;
        }
    }
    return s;
}
}  // namespace

EF_TEST(security_json_limits_and_mutations) {
    using namespace ef::core::json;
    std::string deep(40, '[');
    deep += std::string(40, ']');
    EF_CHECK(!parse(deep).ok());  // derinlik 32 siniri
    EF_CHECK(!parse("\"" + std::string(5000, 'a') + "\"").ok());  // dize siniri
    std::string many = "[";
    for (int i = 0; i < 10050; ++i) many += "1,";
    many += "1]";
    EF_CHECK(!parse(many).ok());  // oge siniri
    EF_CHECK(!parse(std::string((1u << 20) + 1, ' ')).ok());  // bayt siniri
    EF_CHECK(!parse(R"({"a":1,"a":2})").ok());  // tekrarli anahtar
    Rng r{0x1234567};
    const std::string base = R"({"schema_version":1,"welcome_seen":true,"plan_item_selected":{"plan.vfx":false},"x":[1,2.5,"s",null]})";
    int accepted = 0;
    for (int i = 0; i < 3000; ++i) {
        const auto m = mutate(base, r, 1 + static_cast<int>(r.next() % 6));
        if (parse(m).ok()) ++accepted;
        (void)ef::core::settings::Preferences::parse(m);  // cokme yok
    }
    EF_CHECK(accepted < 3000);  // bozulmalarin cogu reddedilir
}

EF_TEST(security_keyvalues_and_history_mutations) {
    Rng r{0xC0FFEE};
    const std::string kv = "\"video.cfg\"\n{\n\t\"Version\"\t\t\"16\"\n\t\"setting.mat_vsync\"\t\t\"0\"\n\t\"setting.msaa_samples\"\t\t\"4\"\n}\n";
    for (int i = 0; i < 3000; ++i) {
        const auto m = mutate(kv, r, 1 + static_cast<int>(r.next() % 5));
        const auto p = ef::core::games::cs2::parse_video_cfg(m);
        if (!p) {
            EF_CHECK(!ef::core::games::cs2::set_field(m, "setting.mat_vsync", "1"));  // bozuksa asla yazma
        } else if (const auto w = ef::core::games::cs2::set_field(m, "setting.mat_vsync", "1")) {
            EF_CHECK(ef::core::games::cs2::parse_video_cfg(*w).has_value());  // yazilan cikti gecerli kalir
        }
    }
    EF_CHECK(!ef::core::games::cs2::parse_video_cfg(std::string((1u << 20) + 8, '"')));  // boyut siniri
    const std::string hist = R"({"schema_version":1,"dropped_total":0,"entries":[{"when_unix_ms":1,"source":"builtin","title":"t","fingerprint":"f","app_version":"0.1.0","metrics":{"avg_fps":100.5},"exclusions":["x"]}]})";
    for (int i = 0; i < 2000; ++i) {
        const auto h = ef::core::insights::History::parse(mutate(hist, r, 1 + static_cast<int>(r.next() % 6)));
        if (h.ok()) {
            EF_CHECK(h.value().entries().size() <= ef::core::insights::kMaxEntries);  // kayit siniri asilmaz
            EF_CHECK(ef::core::insights::History::parse(h.value().serialize()).ok());  // kabul edilen veri kararli
        }
    }
}

EF_TEST(security_ipc_frame_mutations) {
    using namespace ef::core::ipc;
    const auto good = encode(make_hello("0123456789abcdef0123456789abcdef", 1234, 1));
    EF_CHECK(decode(good).ok());
    Rng r{0xBADF00D};
    for (int i = 0; i < 5000; ++i) {
        const auto m = mutate(good, r, 1 + static_cast<int>(r.next() % 4));
        const auto d = decode(m);
        if (d.ok()) {  // kabul edilen cerceve sinirlar icinde ve yeniden kodlanabilir olmali
            EF_CHECK(d.value().payload.size() <= kMaxPayload);
            const auto again = decode(encode(d.value()));
            EF_CHECK(again.ok() && again.value().type == d.value().type && again.value().seq == d.value().seq &&
                      again.value().payload == d.value().payload);
        }
    }
    // Asiri buyuk yuk bildiren baslik reddedilir (bellek ayirma saldirisi yok).
    std::string big = good;
    // Cerceve: magic(4) | version(2) | type(2) | length(4) | seq(8): uzunluk ofset 8.
    EF_CHECK(big.size() >= 20);
    big[8] = '\xFF', big[9] = '\xFF', big[10] = '\xFF', big[11] = '\x7F';
    MsgType t{};
    std::uint64_t seq = 0;
    EF_CHECK(!parse_header(std::string_view(big).substr(0, 20), t, seq).ok());  // kMaxPayload ustu
    EF_CHECK(!decode(big).ok());
    EF_CHECK(!decode("").ok() && !decode(std::string(3, '\0')).ok());
}

// REL-01: yakalama sonucu kodegi yukseltilmis yardimcidan gelir; bozuk baslik/parca kabul edilmemeli,
// kabul edilen her baslik kare planiyla tutarli ve tum sayilari sonlu olmali.
EF_TEST(security_capture_result_mutations) {
    using namespace ef::measurement;
    namespace cm = ef::core::measure;
    CaptureResultHeader h;
    h.target_pid = 4242;
    h.target_create_time = 0x0123456789ABCDEFull;
    h.target_image = "c:\\games\\cs2.exe";
    h.target_error = cm::TargetError::None;
    h.start_ok = true;
    h.start_error = FrameSessionError::None;
    h.summary_swapchain = 1;
    h.summary_render_frames = 3;
    h.summary_displayed_frames = 3;
    h.summary_runtime = cm::Runtime::Dxgi;
    h.summary_validity = cm::Validity::Valid;
    h.stats_n = 3;
    h.stats_valid_seconds = 0.05;
    h.avg_fps = 45.0;
    h.low1_fps = 30.0;
    h.p95_ms = 33.5;
    h.p99_ms = 33.5;
    h.stats_sufficient = true;
    h.manifest_schema_version = 1;
    h.manifest_source = "in_game";
    h.manifest_app = "cs2.exe";
    h.manifest_stats_definition = "low1_v1";
    h.manifest_presentmon_version = "2.6.0";
    h.manifest_valid_seconds = 0.05;
    h.manifest_intervals = 3;
    h.manifest_valid = true;
    h.capture_origin_ms = 1000.0;
    h.interval_count = 3;
    h.total_bytes = 3 * sizeof(float);

    const std::string base = encode_header(h);
    EF_CHECK(!base.empty());
    EF_CHECK(decode_header(base).ok());  // referans baslik gecerli
    Rng r{0x51C0DE};
    int accepted = 0;
    for (int i = 0; i < 4000; ++i) {
        const auto d = decode_header(mutate(base, r, 1 + static_cast<int>(r.next() % 6)));
        if (!d.ok()) continue;
        ++accepted;
        EF_CHECK(d.value().interval_count <= kMaxIntervalCount);
        EF_CHECK(d.value().total_bytes <= kMaxTotalBytes);
        EF_CHECK(d.value().total_bytes == d.value().interval_count * sizeof(float));  // kare plani tutarli
        EF_CHECK(d.value().target_image.size() <= 4096);
        EF_CHECK(!d.value().avg_fps || std::isfinite(*d.value().avg_fps));
        EF_CHECK(!d.value().low1_fps || std::isfinite(*d.value().low1_fps));
        EF_CHECK(!d.value().p95_ms || std::isfinite(*d.value().p95_ms));
        EF_CHECK(!d.value().p99_ms || std::isfinite(*d.value().p99_ms));
        EF_CHECK(!d.value().capture_origin_ms ||
                  (std::isfinite(*d.value().capture_origin_ms) && *d.value().capture_origin_ms >= 0.0));
        EF_CHECK(decode_header(encode_header(d.value())).ok());  // kabul edilen baslik yeniden kodlanabilir
    }
    EF_CHECK(accepted < 4000);  // bozulmalarin en azindan bir kismi reddedilir

    const double values[] = {16.666, 33.333, 8.0, 1.0};
    std::string chunk;
    EF_CHECK(encode_chunk(values, 4, chunk).ok());
    int chunks = 0;
    for (int i = 0; i < 4000; ++i) {
        const auto m = mutate(chunk, r, 1 + static_cast<int>(r.next() % 5));
        const auto d = decode_chunk(m, 4);
        if (!d.ok()) continue;
        ++chunks;
        EF_CHECK(d.value().size() <= 4);
        for (const double v : d.value()) EF_CHECK(is_valid_interval(v));  // NaN/sonsuz/<=0 sizmaz
        EF_CHECK(d.value().size() * sizeof(float) == m.size());
    }
    EF_CHECK(chunks < 4000);
}

// P9/B2 (ADR-037) FUZZ-SVC-10K: sabit tohumlu 10.000 mutasyonlu cerceve dizisi hizmet ve broker oturumuna verilir.
// Degismezler: cokme/istisna yok; isleyici yalniz Ready oturumda ve izin listesindeki islemle cagrilir; herhangi bir
// Error yanitindan sonra oturum kapali kalir ve isleyici bir daha cagrilmaz; istek ve cerceve sinirlari asilmaz;
// cikis seq'i kesin artar. Ayni tohum her kosuda ayni korpusu uretir (sonuc sayilari ciktiya yazilir).

EF_TEST(security_fuzz_service_sessions_10k) {
    using namespace ef::core;
    using ef::contracts::OperationId;
    using ef::contracts::OperationScope;
    using ef::contracts::SettingOperation;
    const std::string nonce = "0123456789abcdef0123456789abcdef";
    constexpr std::uint32_t pid = 4242;
    const std::vector<SettingOperation> ops = {
        {OperationId::HagsSet, OperationScope::Machine, "hags:hwschmode", "2", true, false},
        {OperationId::GameModeSet, OperationScope::CurrentUser, "gamemode", "on", false, false},
        {OperationId::DnsServersSet, OperationScope::Machine, "dns:{00000000-0000-0000-0000-000000000001}", "1.1.1.1", false, false},
    };
    const ipc::RequestAction actions[] = {ipc::RequestAction::Apply, ipc::RequestAction::Fix, ipc::RequestAction::UndoFix,
                                          ipc::RequestAction::DriverInstall, ipc::RequestAction::VendorInstall, ipc::RequestAction::Restore};
    Rng r{0x5EC0B2B2ull};
    int crashes = 0, svc_calls = 0, brk_calls = 0, svc_ready = 0, frames_fed = 0, decoded = 0;
    for (int iter = 0; iter < 10000; ++iter) {
        // Gecerli bir dizi: Hello + 1..6 istek/uzanti + istege bagli Bye; sonra bayt mutasyonu.
        const bool service_hello = (r.next() & 1) != 0;
        std::vector<std::string> wire;
        std::uint64_t seq = 0;
        wire.push_back(ipc::encode(service_hello ? service::make_service_hello(pid, ++seq) : ipc::make_hello(nonce, pid, ++seq)));
        const int n = 1 + static_cast<int>(r.next() % 6);
        for (int k = 0; k < n; ++k) {
            const auto pick = r.next() % 10;
            if (pick < 7) {
                wire.push_back(ipc::encode(ipc::make_request("r" + std::to_string(k), ops[r.next() % ops.size()], ++seq,
                                                             actions[r.next() % std::size(actions)])));
            } else {
                const auto t = static_cast<ipc::MsgType>(1 + r.next() % 12);
                wire.push_back(ipc::encode(ipc::Frame{t, ++seq, "{}"}));
            }
        }
        if (r.next() & 1) wire.push_back(ipc::encode(ipc::Frame{ipc::MsgType::Bye, ++seq, "{}"}));
        const int edits = static_cast<int>(r.next() % 4);  // 0: mutasyonsuz kontrol dizisi
        for (int e = 0; e < edits; ++e) {
            auto& w = wire[r.next() % wire.size()];
            w = mutate(w, r, 1 + static_cast<int>(r.next() % 3));
        }

        int svc_here = 0, brk_here = 0;
        bool svc_bad_op = false;
        service::ServiceSession svc(pid, [&](const std::string&, ipc::RequestAction a, const SettingOperation& op) {
            ++svc_here;
            if (!service::service_handles_op(a, op.id)) svc_bad_op = true;
            return std::string("{}");
        }, [](const ipc::Frame& in) -> std::optional<ipc::Frame> {
            if (in.type < ipc::MsgType::UpdateBegin || in.type > ipc::MsgType::UpdateHealthy) return std::nullopt;
            return ipc::Frame{ipc::MsgType::UpdateStatus, 0, R"({"state":"error"})"};
        });
        ipc::ServerSession brk(nonce, pid, [&](const std::string&, ipc::RequestAction, const SettingOperation&) {
            ++brk_here;
            return std::string("{}");
        });
        try {
            bool svc_dead = false, brk_dead = false;
            std::uint64_t svc_out = 0, brk_out = 0;
            for (const auto& w : wire) {
                ++frames_fed;
                const auto f = ipc::decode(w);
                if (!f.ok()) continue;  // pipe katmani cozulemeyen cerceveyi oturuma vermez
                ++decoded;
                const int before_svc = svc_here, before_brk = brk_here;
                const bool was_ready = svc.ready();
                const auto so = svc.on_frame(f.value());
                EF_CHECK(so.seq > svc_out);
                svc_out = so.seq;
                if (svc_dead) EF_CHECK(so.type == ipc::MsgType::Error && svc_here == before_svc);
                if (so.type == ipc::MsgType::Error) {
                    svc_dead = true;
                    EF_CHECK(svc.closed());
                }
                if (svc_here != before_svc) EF_CHECK(was_ready && f.value().type == ipc::MsgType::Request);
                const auto bo = brk.on_frame(f.value(), pid);
                EF_CHECK(bo.seq > brk_out);
                brk_out = bo.seq;
                if (brk_dead) EF_CHECK(bo.type == ipc::MsgType::Error && brk_here == before_brk);
                if (bo.type == ipc::MsgType::Error) {
                    brk_dead = true;
                    EF_CHECK(brk.closed());
                }
            }
        } catch (...) {
            ++crashes;
        }
        EF_CHECK(!svc_bad_op);
        EF_CHECK(svc.requests() <= service::kMaxServiceRequests);
        EF_CHECK(brk_here <= ipc::kMaxSessionFrames);
        if (svc.ready() || svc_here > 0) ++svc_ready;
        svc_calls += svc_here;
        brk_calls += brk_here;
    }
    EF_CHECK(crashes == 0);
    EF_CHECK(svc_calls > 0 && brk_calls > 0);  // korpus gecerli yolu da kapsar (yalniz ret degil)
    std::printf("  fuzz_service_10k: seed=0x5EC0B2B2 iters=10000 frames=%d decoded=%d svc_handler=%d brk_handler=%d svc_ready=%d crashes=%d\n",
                frames_fed, decoded, svc_calls, brk_calls, svc_ready, crashes);
}

// FUZZ-REQ-10K: Request yuku (decode_request) 10.000 mutasyon; kabul edilen her istek yeniden kodlanip ayni cozulur.
EF_TEST(security_fuzz_decode_request_10k) {
    using namespace ef::core;
    const ef::contracts::SettingOperation op{ef::contracts::OperationId::HagsSet, ef::contracts::OperationScope::Machine,
                                              "hags:hwschmode", "2", true, false};
    const auto base = ipc::make_request("r1", op, 2, ipc::RequestAction::UndoFix).payload;
    Rng r{0xB2DEC0DEull};
    int accepted = 0;
    for (int i = 0; i < 10000; ++i) {
        const auto m = mutate(base, r, 1 + static_cast<int>(r.next() % 6));
        const auto d = ipc::decode_request(m);
        if (!d.ok()) continue;
        ++accepted;
        EF_CHECK(!d.value().request_id.empty() && d.value().request_id.size() <= 64);
        const auto again = ipc::decode_request(ipc::make_request(d.value().request_id, d.value().op, 2, d.value().action).payload);
        EF_CHECK(again.ok() && again.value().action == d.value().action && again.value().op.id == d.value().op.id &&
                  again.value().op.target == d.value().op.target && again.value().op.new_value == d.value().op.new_value);
    }
    EF_CHECK(accepted < 10000);
    std::printf("  fuzz_decode_request_10k: seed=0xB2DEC0DE accepted=%d\n", accepted);
}
