#include "contracts/metrics.h"
#include "core/time/time.h"
#include "ef_test.h"
#include "platform/windows/win_clock.h"

using namespace ef;
using contracts::ErrorCode;

EF_TEST(elapsed_never_negative) {
    auto r = core::elapsed(core::MonoTime{200}, core::MonoTime{100});
    EF_CHECK(!r.ok());
    EF_CHECK(r.code() == ErrorCode::NegativeDuration);
    auto ok = core::elapsed(core::MonoTime{100}, core::MonoTime{350});
    EF_CHECK(ok.ok());
    EF_CHECK(ok.value().ns == 250);
}

EF_TEST(utc_jump_does_not_affect_duration) {
    core::FakeClock c;
    const auto m0 = c.mono_now();
    const auto u0 = c.utc_now();
    c.advance(core::Duration::from_ms(1000));
    c.jump_utc(-3'600'000);  // saat 1 saat geri alindi (DST/NTP)
    const auto d = core::elapsed(m0, c.mono_now());
    EF_CHECK(d.ok());
    EF_CHECK(d.value() == core::Duration::from_ms(1000));
    EF_CHECK(c.utc_now() < u0);  // UTC geri gitti ama sure dogru kaldi
}

EF_TEST(real_windows_clock_is_monotonic) {
    platform::WinClock c;
    core::MonoTime prev = c.mono_now();
    for (int i = 0; i < 10000; ++i) {
        const auto now = c.mono_now();
        EF_CHECK(!(now < prev));
        prev = now;
    }
    const auto u = c.utc_now();
    EF_CHECK(u.unix_ms > 1'700'000'000'000LL);  // 2023 sonrasi makul
}

namespace {
contracts::MetricSample sample(std::int64_t t_ms, contracts::TimeDomain dom = contracts::TimeDomain::AppMonotonic) {
    contracts::MetricSample s;
    s.source = "test";
    s.domain = dom;
    s.t = core::MonoTime{t_ms * 1'000'000};
    s.metric_id = "gpu.busy";
    s.value = contracts::Measurement::of(50.0, contracts::Unit::Percent);
    s.interval = core::Duration::from_ms(100);
    return s;
}
}  // namespace

EF_TEST(stream_counts_loss_and_rejects_backwards) {
    contracts::SampleStream st(core::Duration::from_ms(100));
    EF_CHECK(st.add(sample(0)).ok());
    EF_CHECK(st.add(sample(100)).ok());
    EF_CHECK(st.add(sample(400)).ok());   // 200 ve 300 kayip
    EF_CHECK(st.lost_estimate() == 2);
    auto back = st.add(sample(350));       // geriye gidis
    EF_CHECK(!back.ok());
    EF_CHECK(st.discontinuities() == 1);
    EF_CHECK(st.accepted() == 3);
    EF_CHECK(st.validity(2) == contracts::RunValidity::InvalidClockDiscontinuity);
}

EF_TEST(stream_validity_requires_samples) {
    contracts::SampleStream st(core::Duration::from_ms(100));
    EF_CHECK(st.add(sample(0)).ok());
    EF_CHECK(st.validity(5) == contracts::RunValidity::InvalidInsufficientSamples);
    for (int i = 1; i < 5; ++i) EF_CHECK(st.add(sample(i * 100)).ok());
    EF_CHECK(st.validity(5) == contracts::RunValidity::Valid);
    EF_CHECK(st.lost_estimate() == 0);
}

EF_TEST(unaligned_source_forces_abstain) {
    const auto s = sample(10, contracts::TimeDomain::SourceEtw);
    auto r = contracts::align(s, {});
    EF_CHECK(!r.ok());
    EF_CHECK(r.code() == ErrorCode::NotAligned);
    EF_CHECK(contracts::confidence_after_alignment(r.ok(), contracts::Confidence::High) ==
              contracts::Confidence::Abstain);
    auto r2 = contracts::align(s, {{contracts::TimeDomain::SourceEtw, 5'000'000}});
    EF_CHECK(r2.ok());
    EF_CHECK(r2.value().ns == 15'000'000);
}

EF_TEST(stream_rejects_domain_switch) {
    contracts::SampleStream st(core::Duration::from_ms(100));
    EF_CHECK(st.add(sample(0)).ok());
    EF_CHECK(!st.add(sample(100, contracts::TimeDomain::SourceQpc)).ok());
    EF_CHECK(st.discontinuities() == 1);
}
