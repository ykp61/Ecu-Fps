#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "core/serialization/json.h"
#include "ef_test.h"
#include "measurement/frame/capture_result.h"

using namespace ef::measurement;
namespace cm = ef::core::measure;

namespace {

GameCaptureResult sample_result() {
    GameCaptureResult r;
    r.target = cm::TargetError::None;
    r.start.error = FrameSessionError::None;
    r.start.win32 = 0;
    r.summary.swapchain = 0x123456789ull;
    r.summary.render_frames = 3;
    r.summary.displayed_frames = 3;
    r.summary.render_intervals_ms = {16.0, 17.0, 33.5};
    r.summary.runtime = cm::Runtime::Dxgi;
    r.summary.validity = cm::Validity::Valid;
    r.summary.reasons = {"runtime_unknown"};
    r.stats.n = 3;
    r.stats.excluded = 0;
    r.stats.valid_seconds = 0.0665;
    r.stats.avg_fps = 45.1;
    r.stats.low1_fps = 29.8;
    r.stats.low01_fps = 21.4;
    r.stats.p95_ms = 33.5;
    r.stats.p99_ms = 33.5;
    r.stats.p99_9_ms = 46.7;
    r.stats.median_ms = 16.0;
    r.stats.max_ms = 46.7;
    r.stats.pattern = cm::FramePattern::MicroStutter;
    r.stats.sufficient = false;
    r.manifest.schema_version = 1;
    r.manifest.source = "in_game";
    r.manifest.app = "cs2.exe 1.0";
    r.manifest.context = {{"os_build", "22631"}, {"gpu", "test-gpu"}};
    r.manifest.stats_definition = "low1_v1";
    r.manifest.presentmon_version = "2.6.0";
    r.manifest.valid_seconds = 0.0665;
    r.manifest.intervals = 3;
    r.manifest.excluded_intervals = 0;
    r.manifest.events_lost = 0;
    r.manifest.exclusions = {cm::Exclusion::TooFewFrames};
    r.manifest.valid = false;
    r.dropped_presents = 2;
    r.capture_origin_ms = 172345.25;  // v2: QPC kokeni (ms)
    return r;
}

const cm::ProcessIdentity kTarget{4242, 0x0123456789ABCDEFull, "c:\\games\\cs2.exe"};

std::string f32_bytes(float f) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof bits);
    std::string s(4, '\0');
    s[0] = static_cast<char>(bits & 0xFFu);
    s[1] = static_cast<char>((bits >> 8) & 0xFFu);
    s[2] = static_cast<char>((bits >> 16) & 0xFFu);
    s[3] = static_cast<char>((bits >> 24) & 0xFFu);
    return s;
}

}  // namespace

EF_TEST(capture_header_json_round_trip) {
    const auto h = header_from(sample_result(), kTarget);
    EF_CHECK(h.target_pid == 4242u);
    EF_CHECK(h.target_create_time == 0x0123456789ABCDEFull);
    EF_CHECK(h.interval_count == 3 && h.total_bytes == 3 * sizeof(float));
    EF_CHECK(h.start_ok);

    const auto decoded = decode_header(encode_header(h));
    EF_CHECK(decoded.ok());
    const auto& d = decoded.value();
    EF_CHECK(d.target_pid == h.target_pid);
    EF_CHECK(d.target_create_time == h.target_create_time);
    EF_CHECK(d.target_image == h.target_image);
    EF_CHECK(d.target_error == cm::TargetError::None);
    EF_CHECK(d.start_ok && d.start_error == FrameSessionError::None);
    EF_CHECK(d.summary_swapchain == h.summary_swapchain);
    EF_CHECK(d.summary_runtime == cm::Runtime::Dxgi);
    EF_CHECK(d.summary_validity == cm::Validity::Valid);
    EF_CHECK(d.summary_reasons.size() == 1 && d.summary_reasons[0] == "runtime_unknown");
    EF_CHECK(d.stats_n == 3 && d.stats_sufficient == false);
    EF_CHECK(d.avg_fps.has_value() && std::abs(*d.avg_fps - 45.1) < 1e-9);
    EF_CHECK(d.low1_fps.has_value() && std::abs(*d.low1_fps - 29.8) < 1e-9);
    EF_CHECK(d.low01_fps.has_value() && std::abs(*d.low01_fps - 21.4) < 1e-9);
    EF_CHECK(d.p99_9_ms.has_value() && std::abs(*d.p99_9_ms - 46.7) < 1e-9);
    EF_CHECK(d.stats_median_ms == 16.0 && d.stats_max_ms == 46.7);
    EF_CHECK(d.stats_pattern == "micro_stutter");
    EF_CHECK(d.manifest_source == "in_game" && d.manifest_app == "cs2.exe 1.0");
    EF_CHECK(d.manifest_context.size() == 2 && d.manifest_context.at("os_build") == "22631");
    EF_CHECK(d.manifest_exclusions.size() == 1 && d.manifest_exclusions[0] == cm::Exclusion::TooFewFrames);
    EF_CHECK(!d.manifest_valid);
    EF_CHECK(d.dropped_presents == 2);
    EF_CHECK(d.capture_origin_ms.has_value() && *d.capture_origin_ms == 172345.25);
    EF_CHECK(d.interval_count == 3 && d.total_bytes == 12);

    auto back = result_from(d, {16.0, 17.0, 33.5});
    EF_CHECK(back.ok());
    EF_CHECK(back.value().target == cm::TargetError::None);
    EF_CHECK(back.value().start.ok());
    EF_CHECK(back.value().summary.render_intervals_ms.size() == 3);
    EF_CHECK(back.value().summary.render_intervals_ms[2] == 33.5);
    EF_CHECK(back.value().manifest.app == "cs2.exe 1.0");
    EF_CHECK(back.value().dropped_presents == 2);
    EF_CHECK(back.value().capture_origin_ms == d.capture_origin_ms);
}

EF_TEST(capture_origin_v2_null_and_rejections) {
    auto no_origin = sample_result();
    no_origin.capture_origin_ms.reset();  // koken bilinmiyor: null olarak tasinir, uydurulmaz
    const auto h = header_from(no_origin, kTarget);
    EF_CHECK(!h.capture_origin_ms.has_value());
    const auto d = decode_header(encode_header(h));
    EF_CHECK(d.ok() && !d.value().capture_origin_ms.has_value());
    auto back = result_from(d.value(), {16.0, 17.0, 33.5});
    EF_CHECK(back.ok() && !back.value().capture_origin_ms.has_value());

    const auto good = encode_header(header_from(sample_result(), kTarget));
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("capture_origin_ms", ef::core::json::Value::number(-1.0));
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("capture_origin_ms", ef::core::json::Value::string("x"));
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
    {
        auto doc = ef::core::json::parse(good);
        const auto* v = doc.value().find("capture_origin_ms");
        EF_CHECK(v != nullptr);
    }
}

EF_TEST(capture_header_rejects_bad_json_and_fields) {
    const auto good = encode_header(header_from(sample_result(), kTarget));

    EF_CHECK(!decode_header(good.substr(0, good.size() - 3)).ok());  // kesik JSON

    {
        auto doc = ef::core::json::parse(good);
        EF_CHECK(doc.ok());
        doc.value().set("path", ef::core::json::Value::string("C:\\Windows\\x.exe"));  // bilinmeyen alan
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
    {
        auto doc = ef::core::json::parse(good);
        // Yeni surum tahmin edilmez (guncel: v5, spec 2026-11-12 B; bir sonraki surum reddedilir).
        doc.value().set("v", ef::core::json::Value::integer(kCaptureResultVersion + 1));
        const auto r = decode_header(ef::core::json::write(doc.value()));
        EF_CHECK(!r.ok() && r.code() == ef::contracts::ErrorCode::SchemaTooNew);
    }
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("total_bytes", ef::core::json::Value::integer(4));  // interval_count=3 ile tutarsiz
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("interval_count", ef::core::json::Value::integer(1'000'001));  // sayi siniri
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("interval_count", ef::core::json::Value::null());  // eksik/tur hatasi
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
    {
        auto doc = ef::core::json::parse(good);
        auto* start = doc.value().find("start");
        EF_CHECK(start != nullptr);
        auto start_obj = *start;
        start_obj.set("error", ef::core::json::Value::string("other"));  // ok=true ile celiski
        doc.value().set("start", std::move(start_obj));
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("target_error", ef::core::json::Value::string("free_form"));
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
}

EF_TEST(capture_chunk_round_trip_and_limits) {
    std::vector<double> v(kMaxChunkValues);
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<double>(i + 1) * 0.5;
    std::string chunk;
    EF_CHECK(encode_chunk(v.data(), v.size(), chunk).ok());
    EF_CHECK(chunk.size() == kMaxChunkBytes);
    auto decoded = decode_chunk(chunk, v.size());
    EF_CHECK(decoded.ok() && decoded.value().size() == v.size());
    EF_CHECK(decoded.value()[0] == 0.5 && decoded.value()[v.size() - 1] == v.back());

    std::vector<double> too_many(kMaxChunkValues + 1, 1.0);
    std::string untouched = "x";
    EF_CHECK(encode_chunk(too_many.data(), too_many.size(), untouched).code() == ef::contracts::ErrorCode::ParseLimit);
    EF_CHECK(untouched == "x");

    EF_CHECK(decode_chunk(chunk, v.size() - 1).code() == ef::contracts::ErrorCode::ParseLimit);
    EF_CHECK(!decode_chunk(std::string(3, 'x'), 10).ok());   // 4'un kati degil
    EF_CHECK(!decode_chunk(std::string(), 10).ok());         // bos parca
    EF_CHECK(!decode_chunk(chunk + "x", v.size() + 1).ok()); // hizalama
}

EF_TEST(capture_chunk_rejects_bad_intervals) {
    std::string chunk;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const double bad : {nan, inf, -inf, -1.0, 0.0}) {
        EF_CHECK(encode_chunk(&bad, 1, chunk).code() == ef::contracts::ErrorCode::OutOfRange);
        EF_CHECK(chunk.empty());  // basarisiz kodlamada cikti yazilmaz
    }
    const double good = 16.0;
    EF_CHECK(encode_chunk(&good, 1, chunk).ok());

    const float fnan = std::numeric_limits<float>::quiet_NaN();
    const float finf = std::numeric_limits<float>::infinity();
    for (const float bad : {fnan, finf, -finf, -1.0f, 0.0f}) {
        EF_CHECK(decode_chunk(f32_bytes(bad), 1).code() == ef::contracts::ErrorCode::OutOfRange);
    }
    auto ok = decode_chunk(f32_bytes(16.0f), 1);
    EF_CHECK(ok.ok() && ok.value().size() == 1 && ok.value()[0] == 16.0);
}

EF_TEST(capture_result_from_rejects_mismatch) {
    const auto h = header_from(sample_result(), kTarget);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EF_CHECK(!result_from(h, {16.0, 17.0}).ok());  // eksik veri sessizce kabul edilmez
    EF_CHECK(!result_from(h, {16.0, 17.0, 33.5, 1.0}).ok());
    EF_CHECK(!result_from(h, {16.0, 17.0, nan}).ok());
    EF_CHECK(!result_from(h, {16.0, 17.0, -1.0}).ok());

    auto bad_plan = h;
    bad_plan.total_bytes = 4;  // interval_count=3
    EF_CHECK(!result_from(bad_plan, {}).ok());
}

EF_TEST(x18_capture_header_v4_display_intervals_and_outcome) {
    auto r = sample_result();
    r.summary.display_intervals_ms = {8.0, 8.5, 16.0, 7.5};
    r.outcome = cm::CaptureOutcome::Complete;
    const auto h = header_from(r, kTarget);
    EF_CHECK(h.display_interval_count == 4 && h.display_total_bytes == 16);
    const auto good = encode_header(h);
    const auto d = decode_header(good);
    EF_CHECK(d.ok());
    EF_CHECK(d.value().display_interval_count == 4 && d.value().run_outcome == cm::CaptureOutcome::Complete);
    EF_CHECK(!d.value().target_exited_after_s.has_value());
    auto back = result_from(d.value(), {16.0, 17.0, 33.5}, {8.0, 8.5, 16.0, 7.5});
    EF_CHECK(back.ok() && back.value().summary.display_intervals_ms.size() == 4);
    EF_CHECK(back.value().summary.display_intervals_ms[3] == 7.5 && back.value().outcome == cm::CaptureOutcome::Complete);
    EF_CHECK(!result_from(d.value(), {16.0, 17.0, 33.5}).ok());  // ekran verisi eksik: kabul yok
    EF_CHECK(!result_from(d.value(), {16.0, 17.0, 33.5}, {8.0, 8.5, 16.0}).ok());
    EF_CHECK(!result_from(d.value(), {16.0, 17.0, 33.5}, {8.0, 8.5, 16.0, -1.0}).ok());
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("display_total_bytes", ef::core::json::Value::integer(4));
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("run_outcome", ef::core::json::Value::string("finished"));
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("target_exited_after_s", ef::core::json::Value::number(12.0));  // complete + cikis: celiski
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("v", ef::core::json::Value::integer(3));  // eski surum: tam eslesme sart (ayni EXE)
        const auto old = decode_header(ef::core::json::write(doc.value()));
        EF_CHECK(!old.ok() && old.code() == ef::contracts::ErrorCode::InvalidEnum);
    }
}

// EF-X18-T04: oyun cikisi IPC uzerinden ayri sonuc olarak tasinir.
EF_TEST(x18_t04_game_exited_survives_ipc) {
    auto r = sample_result();
    r.outcome = cm::CaptureOutcome::GameExited;
    r.target_exited_after_s = 12.5;
    r.manifest.exclusions = {cm::Exclusion::GameExited};
    const auto good = encode_header(header_from(r, kTarget));
    const auto d = decode_header(good);
    EF_CHECK(d.ok() && d.value().run_outcome == cm::CaptureOutcome::GameExited);
    EF_CHECK(d.value().target_exited_after_s && *d.value().target_exited_after_s == 12.5);
    EF_CHECK(d.value().manifest_exclusions.size() == 1 && d.value().manifest_exclusions[0] == cm::Exclusion::GameExited);
    auto back = result_from(d.value(), {16.0, 17.0, 33.5});
    EF_CHECK(back.ok() && back.value().outcome == cm::CaptureOutcome::GameExited && back.value().target_exited_after_s == 12.5);
    auto doc = ef::core::json::parse(good);
    doc.value().set("target_exited_after_s", ef::core::json::Value::null());  // cikis var ama zaman yok
    EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
}

// Duzeltme: dusuk guven dislamalari IPC'de cozulur (onceden gercek kosu capture_failed olurdu). T02 ile ilgili.
EF_TEST(x18_capture_header_low_confidence_exclusions_decode) {
    auto r = sample_result();
    r.manifest.exclusions = {cm::Exclusion::RuntimeMixed, cm::Exclusion::RuntimeOther, cm::Exclusion::EtwEventsLost};
    const auto d = decode_header(encode_header(header_from(r, kTarget)));
    EF_CHECK(d.ok() && d.value().manifest_exclusions.size() == 3);
    EF_CHECK(d.value().manifest_exclusions[0] == cm::Exclusion::RuntimeMixed);
    EF_CHECK(d.value().manifest_exclusions[1] == cm::Exclusion::RuntimeOther);
    EF_CHECK(d.value().manifest_exclusions[2] == cm::Exclusion::EtwEventsLost);
}

// Spec 2026-11-12 B (D5): v5 = kare basi GPU mesgul serisi. Sayi 0 (izlenmedi) ya da render araliklariyla ayni.
EF_TEST(d5_capture_v5_gpu_busy_series_round_trip_and_rejections) {
    static_assert(kCaptureResultVersion == 5);
    auto r = sample_result();
    r.summary.render_gpu_busy_ms = {15.25, 0.0, 30.25};  // 0 gecerli olcum: o karede GPU paketi gorulmedi
    const auto h = header_from(r, kTarget);
    EF_CHECK(h.gpu_busy_count == 3 && h.gpu_busy_total_bytes == 12);
    const auto good = encode_header(h);
    const auto d = decode_header(good);
    EF_CHECK(d.ok() && d.value().gpu_busy_count == 3 && d.value().gpu_busy_total_bytes == 12);
    auto back = result_from(d.value(), {16.0, 17.0, 33.5}, {}, {15.25, 0.0, 30.25});
    EF_CHECK(back.ok() && back.value().summary.render_gpu_busy_ms.size() == 3);
    EF_CHECK(back.value().summary.render_gpu_busy_ms[1] == 0.0 && back.value().summary.render_gpu_busy_ms[2] == 30.25);
    // Plan dolu ama veri eksik / fazla / gecersiz: kabul yok.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EF_CHECK(!result_from(d.value(), {16.0, 17.0, 33.5}).ok());
    EF_CHECK(!result_from(d.value(), {16.0, 17.0, 33.5}, {}).ok());
    EF_CHECK(!result_from(d.value(), {16.0, 17.0, 33.5}, {}, {15.25, 0.0}).ok());
    EF_CHECK(!result_from(d.value(), {16.0, 17.0, 33.5}, {}, {15.25, 0.0, 30.25, 1.0}).ok());
    for (const double bad : {nan, inf, -1.0, 86400001.0})
        EF_CHECK(!result_from(d.value(), {16.0, 17.0, 33.5}, {}, {15.25, 0.0, bad}).ok());
    // GPU izlenmedi: plan 0; GPU serisi olmayan sonuc onceki gibi cozulur.
    const auto h0 = header_from(sample_result(), kTarget);
    EF_CHECK(h0.gpu_busy_count == 0 && h0.gpu_busy_total_bytes == 0);
    const auto d0 = decode_header(encode_header(h0));
    EF_CHECK(d0.ok() && result_from(d0.value(), {16.0, 17.0, 33.5}).ok());
    EF_CHECK(result_from(d0.value(), {16.0, 17.0, 33.5}).value().summary.render_gpu_busy_ms.empty());
    EF_CHECK(!result_from(d0.value(), {16.0, 17.0, 33.5}, {}, {1.0, 2.0, 3.0}).ok());  // plansiz veri kabul edilmez
    // Baslik: kare sayisiyla hizasiz ya da bayt plani tutarsiz GPU plani reddedilir; alan eksikse de.
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("gpu_busy_count", ef::core::json::Value::integer(2));
        doc.value().set("gpu_busy_total_bytes", ef::core::json::Value::integer(8));
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("gpu_busy_total_bytes", ef::core::json::Value::integer(16));
        EF_CHECK(!decode_header(ef::core::json::write(doc.value())).ok());
    }
    {
        auto bad_plan = d.value();
        bad_plan.gpu_busy_count = 2;
        bad_plan.gpu_busy_total_bytes = 8;
        EF_CHECK(!result_from(bad_plan, {16.0, 17.0, 33.5}, {}, {1.0, 2.0}).ok());
    }
    {
        auto doc = ef::core::json::parse(good);
        doc.value().set("v", ef::core::json::Value::integer(4));  // onceki surum: tam eslesme sart (ayni EXE)
        const auto old = decode_header(ef::core::json::write(doc.value()));
        EF_CHECK(!old.ok() && old.code() == ef::contracts::ErrorCode::InvalidEnum);
    }
    {
        // GPU alanlari olmayan (v4 bicimli) baslik v5 okuyucuda eksik alan olarak reddedilir.
        const std::string cut = "\"gpu_busy_count\":3,";
        const auto at = good.find(cut);
        EF_CHECK(at != std::string::npos);
        if (at != std::string::npos) {
            std::string missing = good;
            missing.erase(at, cut.size());
            EF_CHECK(!decode_header(missing).ok());
        }
    }

    // Parca: GpuBusy turunde 0 kabul; negatif / NaN / sonsuz / eksi sifir reddedilir. Interval turu sifiri yine reddeder.
    std::string chunk;
    const double busy[3] = {0.0, 4.5, 12.0};
    EF_CHECK(encode_chunk(busy, 3, chunk, ChunkKind::GpuBusy).ok() && chunk.size() == 12);
    const auto dec = decode_chunk(chunk, 3, ChunkKind::GpuBusy);
    EF_CHECK(dec.ok() && dec.value().size() == 3 && dec.value()[0] == 0.0 && dec.value()[1] == 4.5 && dec.value()[2] == 12.0);
    EF_CHECK(decode_chunk(chunk, 3).code() == ef::contracts::ErrorCode::OutOfRange);  // ayni baytlar aralik olarak gecersiz
    std::string untouched;
    EF_CHECK(encode_chunk(busy, 3, untouched).code() == ef::contracts::ErrorCode::OutOfRange && untouched.empty());
    for (const double bad : {nan, inf, -inf, -0.5, 86400001.0}) {
        std::string out;
        EF_CHECK(encode_chunk(&bad, 1, out, ChunkKind::GpuBusy).code() == ef::contracts::ErrorCode::OutOfRange && out.empty());
    }
    const float fnan = std::numeric_limits<float>::quiet_NaN();
    const float finf = std::numeric_limits<float>::infinity();
    for (const float bad : {fnan, finf, -finf, -1.0f, -0.0f})
        EF_CHECK(decode_chunk(f32_bytes(bad), 1, ChunkKind::GpuBusy).code() == ef::contracts::ErrorCode::OutOfRange);
    EF_CHECK(decode_chunk(f32_bytes(0.0f), 1, ChunkKind::GpuBusy).ok());
    EF_CHECK(is_valid_gpu_busy(0.0) && is_valid_gpu_busy(16.6) && !is_valid_gpu_busy(-0.001) && !is_valid_gpu_busy(nan));
}
